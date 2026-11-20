#pragma once

#include <string_view>
#include <vector>
#include <cstdint>

namespace sc2 {

struct ObjectiveDeaths {
    int count = 0;
    int last_loop = 0;
};

struct ReplayObjectives {
    ObjectiveDeaths bosses;
    ObjectiveDeaths trains;
};

bool CanEstimateReplayDuration(std::string_view map, bool victory, int recorded_seconds);

int EstimateReplayDuration(int recorded_seconds, std::string_view base_build, std::string_view map,
                           bool victory, const std::vector<uint8_t>& game_events,
                           const ReplayObjectives& objectives);

} // namespace sc2
