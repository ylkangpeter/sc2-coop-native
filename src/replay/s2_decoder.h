#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include "bit_buffer.h"
#include "replay_types.h"

namespace sc2 {

struct DecodedPlayer {
    std::string name;
    std::string race;
    std::string handle;
    std::string region; // "NA", "EU", "KR", "CN"
    int player_id = 0;
    bool is_observer = false;
    int result = 0; // 1 = Win, 2 = Loss
};

struct DecodedSlot {
    std::string commander;
    int commander_level = 15;
    int prestige = 0;
    std::string prestige_name;
    int brutal_plus = 0;
    int difficulty = 0; // 1=Casual, 2=Normal, 3=Hard, 4=Brutal
    std::vector<int> masteries = {0, 0, 0, 0, 0, 0};
    std::vector<int> retry_mutators;
};

struct DecodedChatMessage {
    int gameloop = 0;
    double time_seconds = 0.0;
    int player_id = 0;
    std::string text;
};

struct TrackerSummary {
    PlayerTimeSeries p1_series;
    PlayerTimeSeries p2_series;
    std::vector<UnitStat> p1_units;
    std::vector<UnitStat> p2_units;
    std::vector<UnitStat> amon_units;
    std::string enemy_comp;
    int p1_kills = 0;
    int p2_kills = 0;
};

class S2Decoder {
public:
    // Decode player details from replay.details
    static std::vector<DecodedPlayer> DecodeDetails(const std::vector<uint8_t>& details_bytes);

    // Decode commander, prestige and brutal plus slots from replay.initData
    static std::vector<DecodedSlot> DecodeInitDataSlots(const std::vector<uint8_t>& initdata_bytes,
                                                        std::vector<std::string>& out_cache_handles,
                                                        std::vector<std::string>& out_mutators,
                                                        bool& out_is_weekly);

    static std::vector<DecodedSlot> DecodeInitDataSlots(const std::vector<uint8_t>& initdata_bytes,
                                                        std::vector<std::string>& out_cache_handles) {
        std::vector<std::string> dummy_m;
        bool dummy_w = false;
        return DecodeInitDataSlots(initdata_bytes, out_cache_handles, dummy_m, dummy_w);
    }

    // Extract 6 mastery talent allocation integers for a given commander from initdata
    static std::vector<int> ExtractMasteries(const std::vector<uint8_t>& initdata_bytes,
                                            const std::string& commander);

    // Decode tracker events stream (player stats, unit born/init/died, enemy waves)
    static TrackerSummary DecodeTrackerEvents(const std::vector<uint8_t>& tracker_bytes,
                                              int p1_total_kills, int p2_total_kills,
                                              const std::string& p1_cmdr, const std::string& p2_cmdr);

    // Decode in-game chat messages from replay.message.events
    static std::vector<DecodedChatMessage> DecodeMessageEvents(const std::vector<uint8_t>& msg_bytes);

private:
    static void SkipType(BitBuffer& buf);
};

} // namespace sc2
