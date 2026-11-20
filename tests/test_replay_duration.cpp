#include "replay/replay_duration.h"
#include "replay/replay_parser.h"
#include "replay/mpq_reader.h"
#include "utils/win32_utils.h"

#include <algorithm>
#include <cassert>
#include <iostream>

namespace {

class GameEvents {
public:
    void Add(int tick, int event, uint32_t value, int value_bits) {
        const int delta = tick - previous_;
        assert(delta >= 0 && delta < (1 << 14));
        const bool extended = delta >= 64;
        Write(extended, 2);
        Write(delta, extended ? 14 : 6);
        Write(0, 5); // issuing player ID
        Write(event, 7);
        Write(value, value_bits);
        bit_ = 0;
        previous_ = tick;
    }

    void Victory(int tick) {
        for (uint32_t achievement = 693; achievement <= 696; ++achievement) Add(tick, 60, achievement, 16);
    }

    std::vector<uint8_t> bytes;

private:
    void Write(uint32_t value, int bits) {
        while (bits > 0) {
            if (bit_ == 0) bytes.push_back(0);
            const int count = std::min(bits, 8 - bit_);
            const uint32_t chunk = (value >> (bits - count)) & ((1u << count) - 1u);
            bytes.back() |= static_cast<uint8_t>(chunk << bit_);
            bit_ = (bit_ + count) % 8;
            bits -= count;
        }
    }

    int bit_ = 0;
    int previous_ = 0;
};

int Audit() {
    std::string path;
    while (std::getline(std::cin, path)) {
        if (!path.empty() && path.back() == '\r') path.pop_back();
        const auto wide = sc2::Win32Utils::Utf8ToWide(path);
        nlohmann::json metadata;
        const auto record = sc2::ReplayParser::ParseFile(wide);
        if (!record || !sc2::MpqReader::ReadJson(wide, "replay.gamemetadata.json", metadata)) {
            std::cout << "DURATION\t" << -1 << '\t' << -1 << '\n';
            continue;
        }
        std::cout << "DURATION\t" << metadata.value("Duration", 0) << '\t' << record->duration_seconds << '\n';
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--audit") return Audit();

    using sc2::EstimateReplayDuration;
    GameEvents events;
    events.Add(940, 48, 0, 32);
    events.Victory(960);
    events.Add(1920, 101, 0, 4);
    assert(EstimateReplayDuration(120, "Base97563", "Rifts to Korhal", true, events.bytes, {}) == 60);
    assert(EstimateReplayDuration(120, "Base98370", "Rifts to Korhal", true, events.bytes, {}) == 60);
    assert(EstimateReplayDuration(120, "Base97563", "Rifts to Korhal", false, events.bytes, {}) == 120);
    assert(EstimateReplayDuration(120, "Base97563", "Rifts to Korhal", true, {}, {}) == 120);
    assert(EstimateReplayDuration(120, "unknown", "Rifts to Korhal", true, events.bytes, {}) == 120);
    events.bytes.push_back(0xff);
    assert(EstimateReplayDuration(120, "Base97563", "Rifts to Korhal", true, events.bytes, {}) == 120);

    GameEvents later;
    later.Victory(1936);
    assert(EstimateReplayDuration(120, "Base97563", "Rifts to Korhal", true, later.bytes, {}) == 120);

    GameEvents arcade;
    arcade.Add(1700, 48, 0, 32);
    arcade.Add(1920, 101, 0, 4);
    const sc2::ReplayObjectives objectives{{21, 1600}, {17, 1616}};
    assert(EstimateReplayDuration(120, "Base97563", "[MM] Part and Parcel - Protoss", false, arcade.bytes, objectives) == 100);
    assert(EstimateReplayDuration(120, "Base97563", "[MM] Oblivion Express - Zerg", false, arcade.bytes, objectives) == 101);
    assert(EstimateReplayDuration(120, "Base97563", "[MM] Oblivion Express - Zerg", false, arcade.bytes, {}) == 120);
    std::cout << "Replay duration selection and fallback tests passed.\n";
}
