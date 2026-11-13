#pragma once

#include "../../db/database.h"

namespace sc2 {

class AppState;

class TabStats {
public:
    static void Render(AppState& state);
    static void InvalidateCache() { s_cache_valid = false; }

private:
    static int s_sub_tab;
    static bool s_cache_valid;
    static StatsFilter s_last_filter;
    static StatsSummary s_cached_summary;
};

} // namespace sc2
