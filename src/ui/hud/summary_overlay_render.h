#pragma once

#include "summary_overlay_types.h"
#include "../../replay/replay_types.h"

namespace sc2 {

inline int SummaryHeaderLineHeight(int height) {
    const int scaled = static_cast<int>(height * 0.023f);
    return scaled > 20 ? scaled : 20;
}
inline int SummaryHeaderHeight(int height, bool /*best_time*/ = false) {
    return SummaryHeaderLineHeight(height) * 2 + 8;
}

void BlendCommanderImage(uint32_t* pixels, int width, int height,
                         const std::string& cmdr, const RECT& rc, bool flip_h);

void DrawSummaryBanner(HDC hdc, uint32_t* pixels, int width, int height,
                       const SummaryOverlayFonts& fonts,
                       const ReplayRecord& r, int rx, int rw, bool is_best_time, bool show_session, int session_wins, int session_games,
                       const std::string& random_commander, const std::string& random_prestige);

void DrawSummarySection(HDC hdc, const SummaryOverlayFonts& fonts,
                        int rx, int rw, int height, int total_kills,
                        int card_y, const std::string& cmdr_name, const std::string& player_name,
                        int prestige_idx, const std::string& prestige_str,
                        const std::vector<int>& masteries,
                        const std::vector<UnitStat>& units,
                        const std::vector<Contribution>& contributions,
                        int p_kills,
                        COLORREF theme_col, bool is_amon, const std::string& enemy_comp_name);

} // namespace sc2
