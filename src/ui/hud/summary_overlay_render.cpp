#include "summary_overlay_render.h"
#include "../../utils/win32_utils.h"
#include "../../i18n/i18n.h"
#include "../../data/sc2_constants.h"
#include <stb_image.h>
#include <algorithm>

namespace sc2 {

namespace {

inline std::wstring ToWide(const std::string& str) {
    return Win32Utils::Utf8ToWide(str);
}

} // namespace

void BlendCommanderImage(uint32_t* pixels, int width, int height,
                         const std::string& cmdr, const RECT& rc, bool flip_h) {
    if (cmdr.empty() || !pixels) return;
    std::string p1 = "assets/Commanders/" + cmdr + ".png";
    std::string p2 = "dist/sc2_coop/assets/Commanders/" + cmdr + ".png";
    int iw = 0, ih = 0, ich = 0;
    unsigned char* img = stbi_load(p1.c_str(), &iw, &ih, &ich, 4);
    if (!img) img = stbi_load(p2.c_str(), &iw, &ih, &ich, 4);
    if (!img) return;

    int dst_h = rc.bottom - rc.top;
    int dst_w = (int)((float)iw * dst_h / ih);
    if (dst_w > 0 && dst_h > 0) {
        int x_start = flip_h ? (rc.right - dst_w) : rc.left;
        for (int dy = 0; dy < dst_h; ++dy) {
            int py = rc.top + dy;
            if (py < 0 || py >= height) continue;
            int sy = (dy * ih) / dst_h;
            for (int dx = 0; dx < dst_w; ++dx) {
                int px = x_start + dx;
                if (px < rc.left || px >= rc.right || px < 0 || px >= width) continue;
                int sx = flip_h ? (iw - 1 - (dx * iw) / dst_w) : ((dx * iw) / dst_w);

                int s_idx = (sy * iw + sx) * 4;
                uint8_t sr = img[s_idx];
                uint8_t sg = img[s_idx + 1];
                uint8_t sb = img[s_idx + 2];
                uint8_t sa = img[s_idx + 3];
                if (sa == 0) continue;

                float fa = (sa / 255.0f) * 0.45f;
                uint32_t bg_col = pixels[py * width + px];
                uint8_t bgr = (bg_col >> 16) & 0xFF;
                uint8_t bgg = (bg_col >> 8) & 0xFF;
                uint8_t bgb = bg_col & 0xFF;

                uint8_t out_r = (uint8_t)(sr * fa + bgr * (1.0f - fa));
                uint8_t out_g = (uint8_t)(sg * fa + bgg * (1.0f - fa));
                uint8_t out_b = (uint8_t)(sb * fa + bgb * (1.0f - fa));
                pixels[py * width + px] = (0xFF << 24) | (out_r << 16) | (out_g << 8) | out_b;
            }
        }
    }
    stbi_image_free(img);
}

void DrawSummaryBanner(HDC hdc, uint32_t* pixels, int width, int height,
                       const SummaryOverlayFonts& fonts,
                       const ReplayRecord& r, int rx, int rw) {
    // --- Row 1: Player Names, Victory Banner & Match Timestamp ---
    bool is_win = (r.result == "Victory");
    COLORREF color_result = is_win ? RGB(225, 225, 225) : RGB(231, 76, 60);

    std::wstring p1_name_w = ToWide(Win32Utils::CleanPlayerName(r.p1.name));
    std::wstring p2_name_w = ToWide(Win32Utils::CleanPlayerName(r.p2.name));

    SelectObject(hdc, fonts.hFontPlayerName);
    SIZE szName1 = { 0, 0 };
    GetTextExtentPoint32W(hdc, p1_name_w.c_str(), (int)p1_name_w.length(), &szName1);
    SIZE szName2 = { 0, 0 };
    GetTextExtentPoint32W(hdc, p2_name_w.c_str(), (int)p2_name_w.length(), &szName2);

    std::wstring result_w;
    if (is_win) {
        result_w = r.is_weekly ? ToWide(TR("Weekly Victory!")) : ToWide(TR("Victory!"));
    } else if (r.result == "Tie") {
        result_w = ToWide(TR("Tie!"));
    } else {
        result_w = ToWide(TR("Defeat!"));
    }
    SelectObject(hdc, fonts.hFontTitle);
    SIZE szRes = { 0, 0 };
    GetTextExtentPoint32W(hdc, result_w.c_str(), (int)result_w.length(), &szRes);

    std::wstring date_text;
    std::wstring clock_text;
    const size_t date_separator = r.game_date.find(' ');
    if (date_separator == std::string::npos) {
        date_text = ToWide(r.game_date);
    } else {
        date_text = ToWide(r.game_date.substr(0, date_separator));
        clock_text = ToWide(r.game_date.substr(date_separator + 1));
    }
    const bool has_timestamp = !date_text.empty() || !clock_text.empty();
    SIZE szDate = { 0, 0 };
    SIZE szClock = { 0, 0 };
    if (has_timestamp) {
        SelectObject(hdc, fonts.hFontTimestamp);
        if (!date_text.empty()) {
            GetTextExtentPoint32W(hdc, date_text.c_str(), (int)date_text.length(), &szDate);
        }
        if (!clock_text.empty()) {
            GetTextExtentPoint32W(hdc, clock_text.c_str(), (int)clock_text.length(), &szClock);
        }
    }
    const int timestamp_width = std::max(szDate.cx, szClock.cx) + 12;

    int row1_top = (int)(height * 0.035f);
    int row1_bot = (int)(height * 0.072f);
    int name_top = (int)(height * 0.038f);

    int gap = (int)(height * 0.010f);
    if (gap < 8) gap = 8;

    int total_mid_w = szRes.cx;
    if (has_timestamp) {
        total_mid_w += gap + timestamp_width;
    }

    int mid_start = (rx + rw / 2) - total_mid_w / 2;
    int min_left = rx + szName1.cx + 10;
    int max_right = rx + rw - szName2.cx - 10;
    if (mid_start < min_left) mid_start = min_left;
    if (mid_start + total_mid_w > max_right) {
        mid_start = max_right - total_mid_w;
        if (mid_start < min_left) mid_start = min_left;
    }

    // P1 Name (Blue, left-aligned)
    SelectObject(hdc, fonts.hFontPlayerName);
    SetTextColor(hdc, RGB(0, 128, 248));
    int p1_r = std::max(rx + rw / 4, mid_start - 6);
    RECT rcName1 = { rx, name_top, p1_r, row1_bot };
    DrawTextW(hdc, p1_name_w.c_str(), -1, &rcName1, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    // Result Text (Victory! / Defeat! / Weekly Victory!)
    SelectObject(hdc, fonts.hFontTitle);
    SetTextColor(hdc, color_result);
    RECT rcRes = { mid_start, row1_top, mid_start + szRes.cx, row1_bot };
    DrawTextW(hdc, result_w.c_str(), -1, &rcRes, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Match date/time directly to the right of Result. The two-line block is
    // vertically centered against the single-line result text.
    if (has_timestamp) {
        int time_x = mid_start + szRes.cx + gap;
        SelectObject(hdc, fonts.hFontTimestamp);
        SetTextColor(hdc, RGB(255, 190, 70));
        const int timestamp_line_h = std::max(12, (int)(height * 0.0145f));
        const int timestamp_top = (row1_top + row1_bot - timestamp_line_h * 2) / 2;
        RECT rcDate = { time_x, timestamp_top, time_x + timestamp_width,
                        timestamp_top + timestamp_line_h };
        RECT rcClock = { time_x, timestamp_top + timestamp_line_h,
                         time_x + timestamp_width, timestamp_top + timestamp_line_h * 2 };
        if (!date_text.empty()) {
            DrawTextW(hdc, date_text.c_str(), -1, &rcDate, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        if (!clock_text.empty()) {
            DrawTextW(hdc, clock_text.c_str(), -1, &rcClock, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    // P2 Name (Green, right-aligned)
    SelectObject(hdc, fonts.hFontPlayerName);
    SetTextColor(hdc, RGB(0, 213, 50));
    int p2_l = has_timestamp ? std::min(rx + rw * 3 / 4, mid_start + total_mid_w + 6)
                             : (rx + rw * 2 / 3);
    RECT rcName2 = { p2_l, name_top, rx + rw, row1_bot };
    DrawTextW(hdc, p2_name_w.c_str(), -1, &rcName2, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    // --- Row 2: Kill Share Bar ---
    int bar_y = (int)(height * 0.076f);
    int bar_h = (int)(height * 0.040f);
    int p1_kills = r.p1.kills;
    int p2_kills = r.p2.kills;
    if (p1_kills <= 0 && p2_kills <= 0) {
        for (const auto& u : r.p1.units) p1_kills += u.kills;
        for (const auto& u : r.p2.units) p2_kills += u.kills;
    }
    int total_kills = p1_kills + p2_kills;
    float p1_frac = 0.5f;
    if (total_kills > 0) p1_frac = (float)p1_kills / (float)total_kills;
    else if (r.p1.apm + r.p2.apm > 0) p1_frac = (float)r.p1.apm / (float)(r.p1.apm + r.p2.apm);

    int p1_bw = (int)(rw * p1_frac);
    if (p1_bw < 12) p1_bw = 12;
    if (p1_bw > rw - 12) p1_bw = rw - 12;

    HBRUSH hBrushP1 = CreateSolidBrush(RGB(0, 128, 248));
    HBRUSH hBrushP2 = CreateSolidBrush(RGB(0, 213, 50));
    RECT rcBar1 = { rx, bar_y, rx + p1_bw, bar_y + bar_h };
    FillRect(hdc, &rcBar1, hBrushP1);
    RECT rcBar2 = { rx + p1_bw, bar_y, rx + rw, bar_y + bar_h };
    FillRect(hdc, &rcBar2, hBrushP2);
    DeleteObject(hBrushP1);
    DeleteObject(hBrushP2);

    BlendCommanderImage(pixels, width, height, r.p1.commander, rcBar1, false);
    BlendCommanderImage(pixels, width, height, r.p2.commander, rcBar2, true);

    SelectObject(hdc, fonts.hFontPlayerName);
    SetTextColor(hdc, RGB(255, 255, 255));
    int pct1 = (int)(p1_frac * 100.0f + 0.5f);
    int pct2 = 100 - pct1;
    std::wstring pct1_str = std::to_wstring(pct1) + L"%";
    std::wstring pct2_str = std::to_wstring(pct2) + L"%";

    RECT rcPct1 = { rx + 12, bar_y, rx + p1_bw - 8, bar_y + bar_h };
    DrawTextW(hdc, pct1_str.c_str(), -1, &rcPct1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT rcPct2 = { rx + p1_bw + 8, bar_y, rx + rw - 12, bar_y + bar_h };
    DrawTextW(hdc, pct2_str.c_str(), -1, &rcPct2, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, fonts.hFontMap);
    RECT rcKillsCenter = { rx, bar_y, rx + rw, bar_y + bar_h };
    std::wstring kills_center = ToWide(TR("kills"));
    DrawTextW(hdc, kills_center.c_str(), -1, &rcKillsCenter, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // --- Row 3: Subheader (Commanders, Map Name, Duration, Bonus, APM, Difficulty) ---
    int sub_y = bar_y + bar_h + (int)(height * 0.008f);

    SelectObject(hdc, fonts.hFontMap);
    SetTextColor(hdc, RGB(220, 220, 220));

    // Left: Commander & APM
    std::wstring p1_cmdr_w = ToWide(TR(r.p1.commander.c_str()));
    std::string p1_lvl = r.p1.FormatLevel();
    if (!p1_lvl.empty()) {
        p1_cmdr_w += L" " + ToWide(p1_lvl);
    }
    int side_w = std::max(160, (int)(rw * 0.32f));
    RECT rcCmdr1 = { rx, sub_y, rx + side_w, sub_y + 20 };
    DrawTextW(hdc, p1_cmdr_w.c_str(), -1, &rcCmdr1, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    std::wstring p1_apm_w = std::to_wstring(r.p1.apm) + L" APM";
    RECT rcApm1 = { rx, sub_y + 22, rx + side_w, sub_y + 42 };
    DrawTextW(hdc, p1_apm_w.c_str(), -1, &rcApm1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Right: Commander & APM
    std::wstring p2_cmdr_w = ToWide(TR(r.p2.commander.c_str()));
    std::string p2_lvl = r.p2.FormatLevel();
    if (!p2_lvl.empty()) {
        p2_cmdr_w = ToWide(p2_lvl) + L" " + p2_cmdr_w;
    }
    RECT rcCmdr2 = { rx + rw - side_w, sub_y, rx + rw, sub_y + 20 };
    DrawTextW(hdc, p2_cmdr_w.c_str(), -1, &rcCmdr2, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    std::wstring p2_apm_w = std::to_wstring(r.p2.apm) + L" APM";
    RECT rcApm2 = { rx + rw - side_w, sub_y + 22, rx + rw, sub_y + 42 };
    DrawTextW(hdc, p2_apm_w.c_str(), -1, &rcApm2, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    // Center: Map Name [MM:SS] [b_earned/b_total] & Difficulty
    int b_earned = r.bonus_earned;
    int b_total = r.bonus_total > 0 ? r.bonus_total : 3;
    std::wstring map_str = ToWide(TR(r.map_name.c_str())) + L" [" + ToWide(r.form_length) + L"] [" +
                           std::to_wstring(b_earned) + L"/" + std::to_wstring(b_total) + L"]";
    int center_inset = std::max(120, side_w - 20);
    RECT rcMapCenter = { rx + center_inset, sub_y, rx + rw - center_inset, sub_y + 20 };
    DrawTextW(hdc, map_str.c_str(), -1, &rcMapCenter, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    std::wstring diff_str = ToWide(TR(r.difficulty.c_str()));
    RECT rcDiff = { rx + center_inset, sub_y + 22, rx + rw - center_inset, sub_y + 42 };
    DrawTextW(hdc, diff_str.c_str(), -1, &rcDiff, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawSummarySection(HDC hdc, const SummaryOverlayFonts& fonts,
                        int rx, int rw, int height, int total_kills,
                        int card_y, const std::string& cmdr_name, const std::string& player_name,
                        int prestige_idx, const std::string& prestige_str,
                        const std::vector<int>& masteries,
                        const std::vector<UnitStat>& units,
                        int p_kills,
                        COLORREF theme_col, bool is_amon, const std::string& enemy_comp_name) {
    // Name (Large, Bold)
    SelectObject(hdc, fonts.hFontPlayerName);
    SetTextColor(hdc, theme_col);
    std::wstring title_str = is_amon ? ToWide(TR("Amon")) : ToWide(Win32Utils::CleanPlayerName(player_name));
    RECT rcName = { rx, card_y, rx + rw, card_y + 26 };
    DrawTextW(hdc, title_str.c_str(), -1, &rcName, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Prestige Title / Enemy Comp Name
    SelectObject(hdc, fonts.hFontPrestige);
    std::wstring sub_title;
    if (is_amon) {
        std::string comp_to_trans = enemy_comp_name.empty() ? "Ravaging Infestation" : enemy_comp_name;
        sub_title = ToWide(TR(comp_to_trans.c_str()));
    } else {
        if (!prestige_str.empty()) {
            sub_title = ToWide(TR(prestige_str.c_str()));
        } else {
            auto pit = kPrestiges.find(cmdr_name);
            if (pit != kPrestiges.end()) {
                const char* p_raw = nullptr;
                if (prestige_idx == 1) p_raw = pit->second.p1;
                else if (prestige_idx == 2) p_raw = pit->second.p2;
                else if (prestige_idx == 3) p_raw = pit->second.p3;
                else p_raw = pit->second.p0;
                if (p_raw) sub_title = ToWide(TR(p_raw));
            }
        }
    }
    SetTextColor(hdc, theme_col);
    RECT rcPres = { rx, card_y + 28, rx + rw, card_y + 48 };
    DrawTextW(hdc, sub_title.c_str(), -1, &rcPres, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    int cur_y = card_y + 52;

    // 6 Masteries (P1 and P2 only, 6 lines)
    if (!is_amon) {
        auto m_it = kCommanderMasteries.find(cmdr_name);
        if (m_it != kCommanderMasteries.end()) {
            SelectObject(hdc, fonts.hFontMastery);
            for (int m_idx = 0; m_idx < 6; ++m_idx) {
                int pts = (m_idx < (int)masteries.size()) ? masteries[m_idx] : 0;
                std::wstring mname = ToWide(TR(m_it->second[m_idx]));

                COLORREF mcol = (pts > 0) ? RGB(255, 255, 255) : RGB(120, 130, 145);
                SetTextColor(hdc, mcol);

                RECT rcPts = { rx, cur_y + m_idx * 18, rx + 24, cur_y + (m_idx + 1) * 18 };
                std::wstring pts_str = std::to_wstring(pts);
                DrawTextW(hdc, pts_str.c_str(), -1, &rcPts, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

                RECT rcMName = { rx + 30, cur_y + m_idx * 18, rx + rw, cur_y + (m_idx + 1) * 18 };
                DrawTextW(hdc, mname.c_str(), -1, &rcMName, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
            cur_y += 6 * 18 + 6;
        }
    }

    // Units Breakdown Table
    SelectObject(hdc, fonts.hFontUnit);
    SetTextColor(hdc, RGB(255, 255, 255));
    RECT rcHKills = { rx + (int)(rw * 0.40f), cur_y, rx + (int)(rw * 0.65f), cur_y + 18 };
    std::wstring kills_th = ToWide(TR("kills"));
    DrawTextW(hdc, kills_th.c_str(), -1, &rcHKills, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SetTextColor(hdc, RGB(0, 213, 50)); // Green header
    RECT rcHCreated = { rx + (int)(rw * 0.70f), cur_y, rx + (int)(rw * 0.84f), cur_y + 18 };
    std::wstring created_th = ToWide(TR("created"));
    DrawTextW(hdc, created_th.c_str(), -1, &rcHCreated, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SetTextColor(hdc, RGB(255, 51, 51)); // Red header
    RECT rcHLost = { rx + (int)(rw * 0.86f), cur_y, rx + rw, cur_y + 18 };
    std::wstring lost_th = ToWide(TR("lost"));
    DrawTextW(hdc, lost_th.c_str(), -1, &rcHLost, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    cur_y += 20;

    // Preprocess units
    std::vector<UnitStat> proc_units = units;
    for (auto& u : proc_units) {
        u.name = FormatFriendlyUnitName(u.name);
    }
    UnitStat* dehaka_ptr = nullptr;
    for (auto& u : proc_units) {
        if (u.name == "Dehaka") {
            dehaka_ptr = &u;
            break;
        }
    }
    if (dehaka_ptr) {
        for (auto& u : proc_units) {
            if (u.name == "Zweihaka") {
                u.created = dehaka_ptr->created;
                u.lost = dehaka_ptr->lost;
            }
        }
    }
    std::sort(proc_units.begin(), proc_units.end(), [](const UnitStat& a, const UnitStat& b) {
        if (a.kills != b.kills) return a.kills > b.kills;
        if (a.created != b.created) return a.created > b.created;
        return a.lost > b.lost;
    });

    int p_tot = p_kills;
    if (p_tot <= 0) {
        for (const auto& u : proc_units) p_tot += u.kills;
        if (p_tot <= 0) p_tot = 1;
    }

    int match_kills = (total_kills > 0) ? total_kills : p_tot;

    // Render Unit Rows
    int max_rows = 5;
    HBRUSH hBrushTheme = CreateSolidBrush(theme_col);
    for (int i = 0; i < max_rows && i < (int)proc_units.size(); ++i) {
        const auto& u = proc_units[i];
        int row_y = cur_y + i * 22;

        // Unit Name
        SelectObject(hdc, fonts.hFontUnit);
        SetTextColor(hdc, theme_col);
        std::wstring uname = ToWide(TR(u.name.c_str()));
        RECT rcUName = { rx, row_y, rx + (int)(rw * 0.38f), row_y + 20 };
        DrawTextW(hdc, uname.c_str(), -1, &rcUName, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Proportional Kill Bar
        int kbar_x = rx + (int)(rw * 0.38f);
        int kbar_w = (int)(rw * 0.30f);
        int kbar_h = 16;
        int fill_w = (int)(height * 0.50f * ((float)u.kills / match_kills));
        if (u.kills > 0 && fill_w < 6) fill_w = 6;
        if (fill_w > kbar_w) fill_w = kbar_w;

        RECT rcBarFill = { kbar_x, row_y + 2, kbar_x + fill_w, row_y + 2 + kbar_h };
        FillRect(hdc, &rcBarFill, hBrushTheme);

        // Kills Text
        SelectObject(hdc, fonts.hFontUnitBold);
        SetTextColor(hdc, RGB(255, 255, 255));
        int pct = (int)(((float)u.kills / p_tot) * 100.0f + 0.5f);
        std::wstring ktext = std::to_wstring(pct) + L"% | " + std::to_wstring(u.kills);
        RECT rcKText = { kbar_x + 6, row_y, kbar_x + kbar_w - 6, row_y + 20 };
        DrawTextW(hdc, ktext.c_str(), -1, &rcKText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Created Count
        SelectObject(hdc, fonts.hFontUnit);
        SetTextColor(hdc, RGB(0, 213, 50));
        bool hide_counts = (u.name.find("Top Bar") != std::string::npos || u.name.find("面板") != std::string::npos);
        std::wstring ctext = (hide_counts || u.created <= 0) ? L"-" : std::to_wstring(u.created);
        RECT rcCText = { rx + (int)(rw * 0.70f), row_y, rx + (int)(rw * 0.84f), row_y + 20 };
        DrawTextW(hdc, ctext.c_str(), -1, &rcCText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Lost Count
        SetTextColor(hdc, RGB(255, 51, 51));
        std::wstring ltext = (hide_counts || u.lost <= 0) ? L"-" : std::to_wstring(u.lost);
        RECT rcLText = { rx + (int)(rw * 0.86f), row_y, rx + rw, row_y + 20 };
        DrawTextW(hdc, ltext.c_str(), -1, &rcLText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    DeleteObject(hBrushTheme);
}

} // namespace sc2
