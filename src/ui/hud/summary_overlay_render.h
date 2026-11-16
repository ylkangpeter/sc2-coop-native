#pragma once

#include "summary_overlay_types.h"
#include "../../replay/replay_types.h"

namespace sc2 {

void BlendCommanderImage(uint32_t* pixels, int width, int height,
                         const std::string& cmdr, const RECT& rc, bool flip_h);

void DrawSummaryBanner(HDC hdc, uint32_t* pixels, int width, int height,
                       const SummaryOverlayFonts& fonts,
                       const ReplayRecord& r, int rx, int rw);

void DrawSummarySection(HDC hdc, const SummaryOverlayFonts& fonts,
                        int rx, int rw, int height, int total_kills,
                        int card_y, const std::string& cmdr_name, const std::string& player_name,
                        int prestige_idx, const std::string& prestige_str,
                        const std::vector<int>& masteries,
                        const std::vector<UnitStat>& units,
                        int p_kills,
                        COLORREF theme_col, bool is_amon, const std::string& enemy_comp_name);

} // namespace sc2
