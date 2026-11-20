#include "replay_duration.h"
#include "bit_buffer.h"

#include <array>
#include <algorithm>
#include <bit>
#include <charconv>
#include <cstdlib>
#include <limits>
#include <span>
#include <string>

namespace sc2 {
namespace {

constexpr int kLoopsPerSecond = 16;

enum class Kind { Integer, Blob, BitArray, Optional, Array, Struct, Choice };
enum class Signal { Other, Order, Dialogue, Achievement, Leave };

struct LayoutNode { Kind kind; int bits; int64_t offset; size_t first; size_t count; };
struct LayoutChild { int tag; uint16_t type; };
struct EventLayout { int event; uint16_t type; Signal signal; };
struct GameLayout { uint16_t delta; uint16_t user; uint16_t event; std::span<const EventLayout> events; };
struct BuildLayout { int build; size_t layout; };

#include "game_event_protocol.inc"

class LayoutReader {
public:
    explicit LayoutReader(const std::vector<uint8_t>& bytes) : buffer_(bytes) {}

    bool Done() const { return buffer_.Done(); }
    void Align() { buffer_.ByteAlign(); }

    int64_t Read(uint16_t type, bool capture) {
        const auto& node = kNodes[type];
        switch (node.kind) {
            case Kind::Integer:
                if (capture) return ReadInteger(node);
                Skip(node.bits);
                break;
            case Kind::Blob: {
                const auto length = ReadInteger(node);
                Align();
                if (length < 0 || static_cast<uint64_t>(length) > buffer_.RemainingBytes()) Fail();
                Skip(static_cast<size_t>(length) * 8);
                break;
            }
            case Kind::BitArray: {
                const auto length = ReadInteger(node);
                if (length < 0) Fail();
                Skip(static_cast<size_t>(length));
                break;
            }
            case Kind::Optional:
                if (Bits(1) != 0) return Read(kChildren[node.first].type, capture);
                break;
            case Kind::Array: {
                const auto length = ReadInteger(node);
                if (length < 0 || length > 65536) Fail();
                for (int64_t i = 0; i < length; ++i) Read(kChildren[node.first].type, false);
                break;
            }
            case Kind::Struct: {
                int64_t first = 0;
                for (size_t index = 0; index < node.count; ++index) {
                    const auto value = Read(kChildren[node.first + index].type, capture && index == 0);
                    if (index == 0) first = value;
                }
                return first;
            }
            case Kind::Choice: {
                const auto tag = ReadInteger(node);
                for (size_t index = 0; index < node.count; ++index) {
                    const auto& child = kChildren[node.first + index];
                    if (tag == child.tag) return Read(child.type, capture);
                }
                Fail();
            }
        }
        return 0;
    }

private:
    [[noreturn]] static void Fail() { throw std::runtime_error("Invalid game event layout"); }

    uint32_t Bits(int bits) {
        if (bits > 32 || static_cast<size_t>(bits) > buffer_.RemainingBits()) Fail();
        return buffer_.ReadBits(bits);
    }

    int64_t ReadInteger(const LayoutNode& node) {
        const auto value = Bits(node.bits);
        return node.offset + value;
    }

    void Skip(size_t bits) {
        if (!buffer_.SkipBits(bits)) Fail();
    }

    BitBuffer buffer_;
};

struct GameSignals {
    std::vector<int> dialogue;
    std::vector<std::pair<int, unsigned int>> achievements;
    int last_order = -1;
    int last_exit = -1;
};

bool DecodeGame(const GameLayout& layout, const std::vector<uint8_t>& bytes,
                int max_loop, GameSignals& signals) {
    LayoutReader reader(bytes);
    int loop = 0;
    try {
        while (!reader.Done()) {
            const int64_t delta = reader.Read(layout.delta, true);
            if (delta < 0 || delta > max_loop - loop) return false;
            loop += static_cast<int>(delta);
            reader.Read(layout.user, false);
            const auto event = reader.Read(layout.event, true);
            const auto spec = std::lower_bound(layout.events.begin(), layout.events.end(), event,
                [](const EventLayout& entry, int64_t id) { return entry.event < id; });
            if (spec == layout.events.end() || spec->event != event) return false;
            const auto value = reader.Read(spec->type, spec->signal == Signal::Achievement);
            reader.Align();

            switch (spec->signal) {
                case Signal::Order: signals.last_order = loop; break;
                case Signal::Dialogue: signals.dialogue.push_back(loop); break;
                case Signal::Leave: signals.last_exit = loop; break;
                case Signal::Achievement:
                    if (value >= 693 && value <= 696) {
                        if (signals.achievements.empty() || signals.achievements.back().first != loop) {
                            signals.achievements.emplace_back(loop, 0);
                        }
                        signals.achievements.back().second |= 1u << (value - 693);
                    }
                    break;
                case Signal::Other: break;
            }
        }
    } catch (const std::runtime_error&) {
        return false;
    }
    return true;
}

std::string LowerAscii(std::string_view value) {
    std::string lower(value);
    for (char& c : lower) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return lower;
}

bool IsParts(std::string_view title) { return title.starts_with("[mm] part and parcel"); }
bool IsTrains(std::string_view title) { return title.starts_with("[mm] oblivion express"); }

int SelectEndLoop(std::string_view title, bool victory, const GameSignals& game, const ReplayObjectives& objectives) {
    if (victory && !title.starts_with("[mm]")) {
        int achievement_fallback = 0;
        for (const auto& [tick, ids] : game.achievements) {
            if (std::popcount(ids) < 3) continue;
            const auto later = std::upper_bound(game.dialogue.begin(), game.dialogue.end(), tick);
            if (later != game.dialogue.begin() && tick - *std::prev(later) <= 3 * kLoopsPerSecond) return tick;
            if (achievement_fallback == 0) achievement_fallback = tick;
        }
        if (achievement_fallback > 0) return achievement_fallback;
    }

    if (game.dialogue.empty()) return 0;
    const int last_dialogue = game.dialogue.back();
    const auto completed = [&](const ObjectiveDeaths& deaths, int count, int delay) {
        return deaths.count == count && last_dialogue >= deaths.last_loop &&
               last_dialogue - deaths.last_loop <= delay * kLoopsPerSecond;
    };
    if (IsParts(title) && completed(objectives.bosses, 21, 25)) return objectives.bosses.last_loop;
    if (IsTrains(title) && completed(objectives.trains, 17, 35)) return objectives.trains.last_loop;

    if (victory && last_dialogue >= 120 * kLoopsPerSecond && game.last_order >= 0) {
        const int gap = last_dialogue - game.last_order;
        if (gap >= -5 * kLoopsPerSecond && gap <= 25 * kLoopsPerSecond) return last_dialogue;
    }
    return 0;
}

} // namespace

bool CanEstimateReplayDuration(std::string_view map, bool victory, int recorded_seconds) {
    if (recorded_seconds <= 0) return false;
    const auto title = LowerAscii(map);
    return victory || IsParts(title) || IsTrains(title);
}

int EstimateReplayDuration(int recorded_seconds, std::string_view base_build, std::string_view map,
                           bool victory, const std::vector<uint8_t>& game_events,
                           const ReplayObjectives& objectives) {
    if (game_events.empty() || !CanEstimateReplayDuration(map, victory, recorded_seconds) ||
        recorded_seconds >= std::numeric_limits<int>::max() / kLoopsPerSecond - 2) return recorded_seconds;

    if (base_build.starts_with("Base")) base_build.remove_prefix(4);
    if (base_build.empty()) return recorded_seconds;
    int build = 0;
    const auto [end, error] = std::from_chars(base_build.data(), base_build.data() + base_build.size(), build);
    if (error != std::errc{} || end != base_build.data() + base_build.size() ||
        build < kBuilds[0].build || build > kBuilds[std::size(kBuilds) - 1].build + 5000) return recorded_seconds;

    // Regional builds can fall between published schemas. Try nearby layouts in
    // version order and accept a layout only after decoding the entire stream.
    std::array<size_t, std::size(kBuilds)> order{};
    for (size_t index = 0; index < order.size(); ++index) order[index] = index;
    const auto closer = [build](size_t left, size_t right) {
        return std::pair{std::abs(kBuilds[left].build - build), kBuilds[left].build} <
               std::pair{std::abs(kBuilds[right].build - build), kBuilds[right].build};
    };
    std::partial_sort(order.begin(), order.begin() + 4, order.end(), closer);
    const int max_loop = (recorded_seconds + 2) * kLoopsPerSecond;
    const auto title = LowerAscii(map);
    for (size_t candidate = 0; candidate < 4; ++candidate) {
        GameSignals signals;
        const auto layout = kBuilds[order[candidate]].layout;
        if (!DecodeGame(kLayouts[layout], game_events, max_loop, signals)) continue;
        const int tick = SelectEndLoop(title, victory, signals, objectives);
        if (tick <= 0 || (signals.last_exit >= 0 && tick > signals.last_exit + kLoopsPerSecond)) return recorded_seconds;
        const int estimated = (tick + kLoopsPerSecond / 2) / kLoopsPerSecond;
        return estimated > 0 && estimated <= recorded_seconds ? estimated : recorded_seconds;
    }
    return recorded_seconds;
}

} // namespace sc2
