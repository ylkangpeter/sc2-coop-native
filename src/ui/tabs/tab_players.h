#pragma once

#include <string>
#include <vector>
#include "../../db/database.h"

namespace sc2 {

class AppState;

class TabPlayers {
public:
    static void Render(AppState& state);

private:
    static int s_current_page;
    static std::string s_sort_column;
    static bool s_sort_ascending;
    static const int kPageSize = 25;

    static uint64_t s_last_revision;
    static std::string s_last_search;
    static std::string s_last_sort_col;
    static bool s_last_sort_asc;
    static int s_last_page;
    static int s_cached_total_records;
    static std::vector<PlayerRecord> s_cached_players;
};

} // namespace sc2
