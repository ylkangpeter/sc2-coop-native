#pragma once

#include <vector>
#include <string>
#include "../../replay/replay_types.h"

namespace sc2 {

class AppState;

class TabGames {
public:
    static void Render(AppState& state);

private:
    static int s_current_page;
    static const int kPageSize = 25;
    static uint64_t s_last_revision;
    static std::string s_last_search;
    static int s_last_page;
    static int s_cached_total_records;
    static std::vector<ReplayRecord> s_cached_replays;
};

} // namespace sc2
