#pragma once

#include <unordered_map>
#include "../../db/database.h"

namespace sc2 {

class AppState;

class TabWeeklies {
public:
    static void Render(AppState& state);
    static void InvalidateCache() { s_cache_valid = false; }

private:
    static char s_search_buf[128];
    static bool s_cache_valid;
    static int s_locate_target_week;
    static std::unordered_map<int, Database::WeeklyRecord> s_cached_records;
};

} // namespace sc2
