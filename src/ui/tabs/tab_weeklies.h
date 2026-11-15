#pragma once

#include <unordered_map>
#include "../../db/database.h"

namespace sc2 {

class AppState;

class TabWeeklies {
public:
    static void Render(AppState& state);

private:
    static char s_search_buf[128];
    static uint64_t s_last_revision;
    static int s_locate_target_week;
    static std::unordered_map<int, Database::WeeklyRecord> s_cached_records;
};

} // namespace sc2
