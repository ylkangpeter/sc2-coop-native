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
enum class Signal { Other, Order, Dialogue, Achievement, Leave, Retarget, Control };

struct LayoutNode { Kind kind; int bits; int64_t offset; size_t first; size_t count; };
enum class Field { Ignore, Ability, Target, Control, EventType, Choice };
struct LayoutChild { int tag; uint16_t type; Field field = Field::Ignore; };
struct ActionFields { int64_t values[6] = {}; };
struct EventLayout { int event; uint16_t type; Signal signal; };
struct GameLayout { uint16_t delta; uint16_t user; uint16_t event; std::span<const EventLayout> events; };
struct BuildLayout { int build; size_t layout; };

#include "game_event_protocol.inc"

class LayoutReader {
public:
    explicit LayoutReader(const std::vector<uint8_t>& bytes) : buffer_(bytes) {}

    bool Done() const { return buffer_.Done(); }
    void Align() { buffer_.ByteAlign(); }

    int64_t Read(uint16_t type, bool capture, ActionFields* action = nullptr, Field field = Field::Ignore) {
        const auto& node = kNodes[type];
        switch (node.kind) {
            case Kind::Integer:
                if (action && field != Field::Ignore) return action->values[static_cast<int>(field)] = ReadInteger(node);
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
                if (Bits(1) != 0) return Read(kChildren[node.first].type, capture, action);
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
                    const auto& child = kChildren[node.first + index];
                    const auto value = Read(child.type, capture && index == 0, action, child.field);
                    if (index == 0) first = value;
                }
                return first;
            }
            case Kind::Choice: {
                const auto tag = ReadInteger(node);
                if (action && field == Field::Choice) action->values[static_cast<int>(field)] = tag;
                for (size_t index = 0; index < node.count; ++index) {
                    const auto& child = kChildren[node.first + index];
                    if (tag == child.tag) return Read(child.type, capture, action);
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

bool DecodeGame(const GameLayout& layout, const std::vector<uint8_t>& bytes,
                int max_loop, std::array<int, 2> swann_users, bool read_lobby, GameSignals& signals) {
    LayoutReader reader(bytes);
    int loop = 0;
    std::array<bool, 16> drone_order{};
    int64_t control_offset = 0;
    bool lobby_calibrated = false;
    try {
        while (!reader.Done()) {
            const int64_t delta = reader.Read(layout.delta, true);
            if (delta < 0 || delta > max_loop - loop) return false;
            loop += static_cast<int>(delta);
            const auto user = reader.Read(layout.user, true);
            const auto event = reader.Read(layout.event, true);
            const auto spec = std::lower_bound(layout.events.begin(), layout.events.end(), event,
                [](const EventLayout& entry, int64_t id) { return entry.event < id; });
            if (spec == layout.events.end() || spec->event != event) return false;
            const bool swann = user >= 0 && user < 16 && (user == swann_users[0] || user == swann_users[1]);
            const bool command = swann && (spec->signal == Signal::Order || spec->signal == Signal::Retarget);
            const bool lobby = read_lobby && spec->signal == Signal::Control && user == 0;
            ActionFields action;
            const auto value = reader.Read(spec->type, spec->signal == Signal::Achievement, command || lobby ? &action : nullptr);
            auto get = [&](Field field) { return action.values[static_cast<int>(field)]; };
            if (command) {
                if (spec->signal == Signal::Order) drone_order[user] = get(Field::Ability) == 2536;
                if (drone_order[user] && get(Field::Target) > 0) {
                    signals.drones.push_back({loop, static_cast<uint32_t>(get(Field::Target))});
                }
            }
            if (lobby) {
                if (loop == 0 && get(Field::EventType) == 3 && get(Field::Choice) == 3) {
                    control_offset = 129 - get(Field::Control);
                    lobby_calibrated = true;
                }
                const auto control = get(Field::Control) + control_offset;
                if (lobby_calibrated && loop > 0 && get(Field::Choice) && control >= 41 && control <= 124 &&
                    (signals.lobby_actions.empty() || signals.lobby_actions.back().loop != loop)) {
                    signals.lobby_actions.push_back({loop, static_cast<int>(control)});
                }
            }
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
                default: break;
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

GameSignals DecodeGameSignals(int recorded_seconds, std::string_view base_build, const std::vector<uint8_t>& game_events,
                              std::array<int, 2> swann_users, bool read_lobby) {
    if (game_events.empty() || recorded_seconds <= 0 ||
        recorded_seconds >= std::numeric_limits<int>::max() / kLoopsPerSecond - 2) return {};
    if (base_build.starts_with("Base")) base_build.remove_prefix(4);
    if (base_build.empty()) return {};
    int build = 0;
    const auto [end, error] = std::from_chars(base_build.data(), base_build.data() + base_build.size(), build);
    if (error != std::errc{} || end != base_build.data() + base_build.size() ||
        build < kBuilds[0].build || build > kBuilds[std::size(kBuilds) - 1].build + 5000) return {};

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

    for (size_t candidate = 0; candidate < 4; ++candidate) {
        GameSignals signals;
        const auto layout = kBuilds[order[candidate]].layout;
        if (!DecodeGame(kLayouts[layout], game_events, max_loop, swann_users, read_lobby, signals)) continue;
        return signals;
    }
    return {};
}

int MatchEndLoop(int recorded_seconds, std::string_view map, bool victory,
                 const GameSignals& signals, const ReplayObjectives& objectives) {
    if (!CanEstimateReplayDuration(map, victory, recorded_seconds)) return -1;
    const int tick = SelectEndLoop(LowerAscii(map), victory, signals, objectives);
    if (tick <= 0 || tick > int64_t(recorded_seconds) * kLoopsPerSecond ||
        (signals.last_exit >= 0 && tick > signals.last_exit + kLoopsPerSecond)) return -1;
    return tick;
}

int MatchDuration(int recorded_seconds, std::string_view map, bool victory,
                  const GameSignals& signals, const ReplayObjectives& objectives) {
    const int tick = MatchEndLoop(recorded_seconds, map, victory, signals, objectives);
    if (tick < 0) return recorded_seconds;
    const int estimated = (tick + kLoopsPerSecond / 2) / kLoopsPerSecond;
    return estimated > 0 && estimated <= recorded_seconds ? estimated : recorded_seconds;
}

int EstimateReplayDuration(int recorded_seconds, std::string_view base_build, std::string_view map,
                           bool victory, const std::vector<uint8_t>& game_events, const ReplayObjectives& objectives) {
    if (!CanEstimateReplayDuration(map, victory, recorded_seconds)) return recorded_seconds;
    return MatchDuration(recorded_seconds, map, victory,
                         DecodeGameSignals(recorded_seconds, base_build, game_events, {-1, -1}, false), objectives);
}

} // namespace sc2
