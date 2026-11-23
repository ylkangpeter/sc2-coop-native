#pragma once

#include <string_view>
#include <vector>
#include <cstdint>
#include <array>
#include <utility>

namespace sc2 {

struct DroneOrder { int loop; uint32_t target; };

struct ObjectiveDeaths {
    int count = 0;
    int last_loop = 0;
};

struct ReplayObjectives {
    ObjectiveDeaths bosses;
    ObjectiveDeaths trains;
};

struct LobbyAction { int loop; int control; };
struct GameSignals {
    std::vector<int> dialogue;
    std::vector<std::pair<int, unsigned int>> achievements;
    int last_order = -1;
    int last_exit = -1;
    std::vector<DroneOrder> drones;
    std::vector<LobbyAction> lobby_actions;
};

GameSignals DecodeGameSignals(int recorded_seconds, std::string_view base_build,
                              const std::vector<uint8_t>& bytes, std::array<int, 2> swann_users,
                              bool read_lobby);
int MatchEndLoop(int recorded_seconds, std::string_view map, bool victory,
                 const GameSignals& signals, const ReplayObjectives& objectives);
int MatchDuration(int recorded_seconds, std::string_view map, bool victory,
                  const GameSignals& signals, const ReplayObjectives& objectives);

bool CanEstimateReplayDuration(std::string_view map, bool victory, int recorded_seconds);

int EstimateReplayDuration(int recorded_seconds, std::string_view base_build, std::string_view map,
                           bool victory, const std::vector<uint8_t>& game_events,
                           const ReplayObjectives& objectives);

} // namespace sc2
