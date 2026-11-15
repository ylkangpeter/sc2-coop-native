#pragma once

#include "../../db/database.h"

namespace sc2 {

class AppState;

class TabStats {
public:
    static void Render(AppState& state);

private:
    static int s_sub_tab;
    static uint64_t s_last_revision;
    static StatsFilter s_last_filter;
    static StatsSummary s_cached_summary;
};

} // namespace sc2
