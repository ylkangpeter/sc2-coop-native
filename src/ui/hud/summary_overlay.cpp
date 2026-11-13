#include "summary_overlay.h"
#include "../../db/database.h"
#include "../../app_state.h"
#include "../../utils/win32_utils.h"
#include "../../data/sc2_constants.h"
#include "../../replay/replay_parser.h"
#include <stb/stb_image.h>

#include <algorithm>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <cmath>

namespace sc2 {

static std::wstring ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring w(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &w[0], len);
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

static std::wstring FormatSeconds(float sec) {
    int total_s = static_cast<int>(sec + 0.5f);
    int m = total_s / 60;
    int s = total_s % 60;
    wchar_t buf[16];
    wsprintfW(buf, L"%02d:%02d", m, s);
    return buf;
}

static std::wstring FormatChartValue(float val) {
    wchar_t buf[32];
    if (val >= 1000.0f) {
        int thousands = (int)(val / 1000.0f);
        int rem = ((int)val) % 1000;
        wsprintfW(buf, L"%d,%03d", thousands, rem);
    } else {
        wsprintfW(buf, L"%d", (int)(val + 0.5f));
    }
    return buf;
}

LRESULT CALLBACK SummaryOverlay::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                SummaryOverlay::Instance().Hide();
                return 0;
            }
            break;
        case WM_DESTROY:
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

SummaryOverlay& SummaryOverlay::Instance() {
    static SummaryOverlay instance;
    return instance;
}

void SummaryOverlay::Init(HINSTANCE hInstance) {
    m_hInstance = hInstance;

    // Load Eurostile font from assets folder relative to executable or current directory
    wchar_t exe_path[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    std::wstring font_path = exe_path;
    auto pos = font_path.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        font_path = font_path.substr(0, pos + 1) + L"assets\\Eurostile-Reg.otf";
        AddFontResourceExW(font_path.c_str(), FR_PRIVATE, nullptr);
    }
    AddFontResourceExW(L"assets\\Eurostile-Reg.otf", FR_PRIVATE, nullptr);

    WNDCLASSEXW wc = {
        sizeof(wc),
        CS_HREDRAW | CS_VREDRAW,
        WndProc,
        0L, 0L,
        hInstance,
        nullptr,
        LoadCursor(nullptr, IDC_ARROW),
        nullptr,
        nullptr,
        L"SC2SummaryOverlayClass",
        nullptr
    };
    RegisterClassExW(&wc);

    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);

    m_overlay_h = screen_h;
    m_overlay_w = (int)(screen_h * 1.0f); // 100vh width, matching original CSS
    if (m_overlay_w > screen_w) m_overlay_w = screen_w;

    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wc.lpszClassName,
        L"SC2 Match Summary Overlay",
        WS_POPUP,
        screen_w, 0, m_overlay_w, m_overlay_h,
        nullptr, nullptr, hInstance, nullptr
    );

    // Create 45-degree rotated font for chart X-axis labels
    LOGFONTW lf = {};
    lf.lfHeight = -(int)(m_overlay_h * 0.013f);
    if (lf.lfHeight > -11) lf.lfHeight = -11;
    lf.lfWeight = FW_NORMAL;
    lf.lfEscapement = 450; // 45 degrees counterclockwise
    lf.lfOrientation = 450;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Eurostile");
    m_hFontRotated = CreateFontIndirectW(&lf);
}

void SummaryOverlay::Shutdown() {
    Hide();
    if (m_hFontRotated) {
        DeleteObject(m_hFontRotated);
        m_hFontRotated = nullptr;
    }
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if (m_hInstance) {
        UnregisterClassW(L"SC2SummaryOverlayClass", m_hInstance);
        m_hInstance = nullptr;
    }
}

void SummaryOverlay::ShowMatch(const ReplayRecord& rec, bool is_best_time, float duration) {
    m_current_record = rec;
    m_is_best_time = is_best_time;
    m_total_duration = duration > 0.0f ? duration : (float)AppState::Instance().hud_duration_seconds;
    m_remaining_time = m_total_duration;

    // If unit details or time series are empty and replay file exists, parse on demand
    if (m_current_record.p1.units.empty() && !m_current_record.file_path.empty()) {
        std::wstring wpath = ToWide(m_current_record.file_path);
        auto full_rec = ReplayParser::ParseFile(wpath);
        if (full_rec.has_value()) {
            m_current_record = full_rec.value();
            Database::Instance().SaveReplay(m_current_record);
        }
    }

    // Refresh cache of recent matches if empty
    if (m_cached_recent.empty()) {
        int total = 0;
        m_cached_recent = Database::Instance().QueryReplays("", 30, 0, total);
    }
    for (size_t i = 0; i < m_cached_recent.size(); ++i) {
        if (m_cached_recent[i].file_path == rec.file_path) {
            m_browse_index = static_cast<int>(i);
            break;
        }
    }

    // Start slide-in animation from right edge
    m_anim_state = OverlayAnimState::SlidingIn;
    // If it was already partially in, keep current m_anim_t
    if (m_anim_t <= 0.0f) m_anim_t = 0.01f;

    Render();
    UpdatePosition();
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
}

void SummaryOverlay::ShowLatest() {
    int total = 0;
    auto list = Database::Instance().QueryReplays("", 30, 0, total);
    if (!list.empty()) {
        m_cached_recent = list;
        m_browse_index = 0;
        ShowMatch(m_cached_recent[0], false, (float)AppState::Instance().hud_duration_seconds);
    } else {
        ReplayRecord demo;
        demo.result = "Victory";
        demo.map_name = "Dead of Night";
        demo.form_length = "22:15";
        demo.difficulty = "Brutal";
        demo.bonus_earned = 1;
        demo.bonus_total = 1;
        demo.p1.name = "Player 1";
        demo.p1.commander = "Raynor";
        demo.p1.prestige = 1;
        demo.p1.prestige_name = "Backwater Sheriff";
        demo.p1.apm = 165;
        demo.p1.kills = 480;
        demo.p1.masteries = { 30, 0, 0, 30, 30, 0 };
        demo.p1.units = {
            { "Marine", 85, 20, 260, 0.54f },
            { "Firebat", 24, 4, 140, 0.29f },
            { "Medic", 18, 2, 0, 0.0f }
        };
        demo.p2.name = "Player 2";
        demo.p2.commander = "Artanis";
        demo.p2.prestige = 0;
        demo.p2.prestige_name = "Hierarch of the Daelaam";
        demo.p2.apm = 142;
        demo.p2.kills = 320;
        demo.p2.masteries = { 30, 0, 30, 0, 30, 0 };
        demo.p2.units = {
            { "Zealot", 50, 15, 180, 0.56f },
            { "Dragoon", 28, 6, 110, 0.34f },
            { "Immortal", 8, 1, 30, 0.10f }
        };
        demo.enemy_comp = "Swarm Hosts & Mutalisks";
        demo.amon_units = {
            { "Infested Terran", 400, 350, 350, 0.44f },
            { "Aberration", 50, 45, 45, 0.22f },
            { "Choker", 35, 30, 30, 0.15f }
        };
        demo.duration_seconds = 1335;
        for (int t = 0; t <= 1335; t += 30) {
            float frac = (float)t / 1335.0f;
            demo.p1_series.time_stamps.push_back((float)t);
            demo.p1_series.army_value.push_back(frac * 4200.0f);
            demo.p1_series.supply_used.push_back(frac * 160.0f);
            demo.p1_series.kill_count.push_back(frac * 480.0f);
            demo.p1_series.collection_rate.push_back(800.0f + frac * 1200.0f);

            demo.p2_series.time_stamps.push_back((float)t);
            demo.p2_series.army_value.push_back(frac * 3600.0f);
            demo.p2_series.supply_used.push_back(frac * 140.0f);
            demo.p2_series.kill_count.push_back(frac * 320.0f);
            demo.p2_series.collection_rate.push_back(700.0f + frac * 1000.0f);
        }
        ShowMatch(demo, false, (float)AppState::Instance().hud_duration_seconds);
    }
}

void SummaryOverlay::ShowPrevious() {
    if (m_cached_recent.empty()) {
        int total = 0;
        m_cached_recent = Database::Instance().QueryReplays("", 30, 0, total);
    }
    if (m_cached_recent.empty()) return;

    if (m_browse_index + 1 < static_cast<int>(m_cached_recent.size())) {
        m_browse_index++;
        ShowMatch(m_cached_recent[m_browse_index], false, (float)AppState::Instance().hud_duration_seconds);
    }
}

void SummaryOverlay::ShowNext() {
    if (m_cached_recent.empty()) {
        int total = 0;
        m_cached_recent = Database::Instance().QueryReplays("", 30, 0, total);
    }
    if (m_cached_recent.empty()) return;

    if (m_browse_index > 0) {
        m_browse_index--;
        ShowMatch(m_cached_recent[m_browse_index], false, (float)AppState::Instance().hud_duration_seconds);
    }
}

void SummaryOverlay::Toggle() {
    if (m_anim_state == OverlayAnimState::Visible || m_anim_state == OverlayAnimState::SlidingIn) {
        Hide();
    } else {
        ShowLatest();
    }
}

void SummaryOverlay::Hide() {
    if (m_anim_state != OverlayAnimState::Hidden) {
        m_anim_state = OverlayAnimState::SlidingOut;
    }
}

void SummaryOverlay::Update(float delta_time) {
    if (m_anim_state == OverlayAnimState::Hidden) return;

    if (m_anim_state == OverlayAnimState::SlidingIn) {
        m_anim_t += delta_time / 0.35f; // Slide in over 350ms
        if (m_anim_t >= 1.0f) {
            m_anim_t = 1.0f;
            m_anim_state = OverlayAnimState::Visible;
        }
        UpdatePosition();
    } else if (m_anim_state == OverlayAnimState::Visible) {
        m_remaining_time -= delta_time;
        if (m_remaining_time <= 0.0f) {
            Hide();
        }
    } else if (m_anim_state == OverlayAnimState::SlidingOut) {
        m_anim_t -= delta_time / 0.30f; // Slide out over 300ms
        if (m_anim_t <= 0.0f) {
            m_anim_t = 0.0f;
            m_anim_state = OverlayAnimState::Hidden;
            if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE);
        } else {
            UpdatePosition();
        }
    }
}

void SummaryOverlay::UpdatePosition() {
    if (!m_hwnd) return;
    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);

    m_overlay_h = screen_h;
    m_overlay_w = (int)(screen_h * 1.0f);
    if (m_overlay_w > screen_w) m_overlay_w = screen_w;

    // Cubic ease-out when sliding in, cubic ease-in when sliding out
    float progress = m_anim_t;
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;

    float ease_factor;
    if (m_anim_state == OverlayAnimState::SlidingIn || m_anim_state == OverlayAnimState::Visible) {
        ease_factor = 1.0f - std::pow(1.0f - progress, 3.0f);
    } else {
        ease_factor = std::pow(progress, 2.0f);
    }

    int cur_x = screen_w - (int)(m_overlay_w * ease_factor);
    SetWindowPos(m_hwnd, HWND_TOPMOST, cur_x, 0, m_overlay_w, m_overlay_h,
                 SWP_NOACTIVATE | SWP_NOSIZE);
}

void SummaryOverlay::Render() {
    if (!m_hwnd) return;

    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    m_overlay_h = screen_h;
    m_overlay_w = (int)(screen_h * 1.0f);
    if (m_overlay_w > screen_w) m_overlay_w = screen_w;

    const int width = m_overlay_w;
    const int height = m_overlay_h;

    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) return;

    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    if (!hdcMem) {
        ReleaseDC(nullptr, hdcScreen);
        return;
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // Top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    if (!hBitmap || !pBits) {
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
        return;
    }
    HGDIOBJ hOldBmp = SelectObject(hdcMem, hBitmap);

    // 1. Premultiplied Background Fill with Horizontal Gradient Mask
    // Matches original CSS: mask-image: linear-gradient(90deg, rgba(0,0,0,0), rgba(0,0,0,0.8)...)
    // Left 22% ramps smoothly from transparent to kTargetAlpha (~59% opacity), giving higher transparency
    uint32_t* pixels = (uint32_t*)pBits;
    float ramp_w = width * 0.22f;
    const uint8_t kTargetAlpha = 150; // Higher transparency as requested (desktop & game visible behind)

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint8_t a = 0;
            if (x < ramp_w) {
                float t = (float)x / ramp_w;
                // smoothstep curve
                a = (uint8_t)(kTargetAlpha * (t * t * (3.0f - 2.0f * t)));
            } else {
                a = kTargetAlpha;
            }

            // Pure black translucent background, no borders, no shadows
            // Premultiplied: rgb * a / 255 = 0
            pixels[y * width + x] = ((uint32_t)a << 24);
        }
    }

    SetBkMode(hdcMem, TRANSPARENT);

    // Dynamic font sizes matching original CSS vh units (Eurostile)
    const wchar_t* kFontFamily = L"Eurostile";
    HFONT hFontTitle = CreateFontW((int)(height * 0.026f), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, kFontFamily);
    HFONT hFontPlayerName = CreateFontW((int)(height * 0.025f), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, kFontFamily);
    HFONT hFontPrestige = CreateFontW((int)(height * 0.016f), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, kFontFamily);
    HFONT hFontMap = CreateFontW((int)(height * 0.017f), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, kFontFamily);
    HFONT hFontMastery = CreateFontW((int)(height * 0.015f), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, kFontFamily);
    HFONT hFontUnit = CreateFontW((int)(height * 0.016f), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, kFontFamily);
    HFONT hFontUnitBold = CreateFontW((int)(height * 0.016f), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, kFontFamily);
    HFONT hFontChartTitle = CreateFontW((int)(height * 0.018f), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, kFontFamily);
    HFONT hFontAxis = CreateFontW((int)(height * 0.013f), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, kFontFamily);

    const auto& r = m_current_record;

    // Helper: Draw Left Chart with Grid Lines, Angled X Timestamps, and Dual Curves
    auto DrawChart = [&](int cx, int cy, int cw, int ch,
                        const std::wstring& title,
                        const std::vector<float>& times1, const std::vector<float>& vals1,
                        const std::vector<float>& times2, const std::vector<float>& vals2,
                        COLORREF color1, COLORREF color2) {
        // Plot drawing region inside chart
        int px = cx + 55;
        int py = cy + 28;
        int pw = cw - 70;
        int ph = ch - 58;

        // Chart Title (Centered above plot, Eurostile bold #DDDDDD)
        SelectObject(hdcMem, hFontChartTitle);
        SetTextColor(hdcMem, RGB(225, 230, 240));
        RECT rcTitle = { px, cy, px + pw, cy + 24 };
        DrawTextW(hdcMem, title.c_str(), -1, &rcTitle, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        float max_time = r.duration_seconds > 0 ? (float)r.duration_seconds : 900.0f;
        if (max_time < 60.0f) max_time = 60.0f;

        float max_val = 10.0f;
        for (float v : vals1) if (v > max_val) max_val = v;
        for (float v : vals2) if (v > max_val) max_val = v;

        // Clean round Y intervals matching reference screenshot
        if (max_val > 10000.0f) max_val = std::ceil(max_val / 4000.0f) * 4000.0f;
        else if (max_val > 4000.0f) max_val = std::ceil(max_val / 2000.0f) * 2000.0f;
        else if (max_val > 1000.0f) max_val = std::ceil(max_val / 1000.0f) * 1000.0f;
        else if (max_val > 400.0f) max_val = std::ceil(max_val / 200.0f) * 200.0f;
        else if (max_val > 100.0f) max_val = std::ceil(max_val / 50.0f) * 50.0f;
        else max_val = std::ceil(max_val / 20.0f) * 20.0f;
        if (max_val < 40.0f) max_val = 40.0f;

        // Draw horizontal grid lines & Y labels (4 intervals, color #333333)
        SelectObject(hdcMem, hFontAxis);
        SetTextColor(hdcMem, RGB(170, 170, 170));
        HPEN hPenGrid = CreatePen(PS_SOLID, 1, RGB(55, 55, 55));
        HGDIOBJ hOldPen = SelectObject(hdcMem, hPenGrid);

        for (int i = 0; i <= 4; ++i) {
            float frac = (float)i / 4.0f;
            int ly = py + ph - (int)(frac * ph);
            float cur_val = frac * max_val;

            MoveToEx(hdcMem, px, ly, nullptr);
            LineTo(hdcMem, px + pw, ly);

            std::wstring lbl = FormatChartValue(cur_val);
            RECT rcLbl = { cx + 2, ly - 8, px - 6, ly + 8 };
            DrawTextW(hdcMem, lbl.c_str(), -1, &rcLbl, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }

        // Draw vertical grid lines & X labels (every 100s, e.g. 00:00, 01:40, 03:20... matching original)
        float time_step = 100.0f;
        if (max_time > 2400.0f) time_step = 200.0f;
        else if (max_time < 600.0f) time_step = 60.0f;

        for (float cur_time = 0.0f; cur_time <= max_time + 0.1f; cur_time += time_step) {
            int lx = px + (int)((cur_time / max_time) * pw);
            if (lx > px + pw) break;

            MoveToEx(hdcMem, lx, py, nullptr);
            LineTo(hdcMem, lx, py + ph);

            // 45-degree angled X timestamp text
            std::wstring t_lbl = FormatSeconds(cur_time);
            if (m_hFontRotated) {
                SelectObject(hdcMem, m_hFontRotated);
                SetTextColor(hdcMem, RGB(170, 170, 170));
                TextOutW(hdcMem, lx - 8, py + ph + 24, t_lbl.c_str(), (int)t_lbl.length());
            } else {
                SelectObject(hdcMem, hFontAxis);
                SetTextColor(hdcMem, RGB(170, 170, 170));
                RECT rcTLbl = { lx - 20, py + ph + 4, lx + 20, py + ph + 18 };
                DrawTextW(hdcMem, t_lbl.c_str(), -1, &rcTLbl, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }
        SelectObject(hdcMem, hOldPen);
        DeleteObject(hPenGrid);

        // Draw Data Polylines
        auto PlotSeries = [&](const std::vector<float>& times, const std::vector<float>& vals, COLORREF col) {
            if (times.empty() || vals.empty()) return;
            std::vector<POINT> pts;
            pts.reserve(times.size());
            for (size_t k = 0; k < times.size() && k < vals.size(); ++k) {
                float t = times[k];
                float v = vals[k];
                int sx = px + (int)((t / max_time) * pw);
                int sy = py + ph - (int)((v / max_val) * ph);
                if (sx < px) sx = px;
                if (sx > px + pw) sx = px + pw;
                if (sy < py) sy = py;
                if (sy > py + ph) sy = py + ph;
                pts.push_back({ sx, sy });
            }
            if (pts.size() >= 2) {
                HPEN hPen = CreatePen(PS_SOLID, 2, col);
                HGDIOBJ hOldPen2 = SelectObject(hdcMem, hPen);
                Polyline(hdcMem, pts.data(), static_cast<int>(pts.size()));
                SelectObject(hdcMem, hOldPen2);
                DeleteObject(hPen);
            }
        };

        COLORREF c_p1 = RGB(0, 128, 248); // Original P1 Cyan/Blue #0080F8
        COLORREF c_p2 = RGB(0, 213, 50);  // Original P2 Green #00D532
        PlotSeries(times1, vals1, c_p1);
        PlotSeries(times2, vals2, c_p2);
    };

    // ==========================================
    // 2. RENDER LEFT COLUMN (4 LINE CHARTS)
    // ==========================================
    const auto& s1 = r.p1_series;
    const auto& s2 = r.p2_series;
    int chart_w = (int)(height * 0.38f);
    int chart_h = (int)((height - height * 0.10f) / 4);
    int chart_x = width - (int)(height * 0.88f);
    int top_margin = (int)(height * 0.045f);

    DrawChart(chart_x, top_margin, chart_w, chart_h, L"Army value",
              s1.time_stamps, s1.army_value, s2.time_stamps, s2.army_value, RGB(0, 128, 248), RGB(0, 213, 50));
    DrawChart(chart_x, top_margin + chart_h, chart_w, chart_h, L"Supply used",
              s1.time_stamps, s1.supply_used, s2.time_stamps, s2.supply_used, RGB(0, 128, 248), RGB(0, 213, 50));
    DrawChart(chart_x, top_margin + chart_h * 2, chart_w, chart_h, L"Kill count",
              s1.time_stamps, s1.kill_count, s2.time_stamps, s2.kill_count, RGB(0, 128, 248), RGB(0, 213, 50));
    DrawChart(chart_x, top_margin + chart_h * 3, chart_w, chart_h, L"Resource collection rate",
              s1.time_stamps, s1.collection_rate, s2.time_stamps, s2.collection_rate, RGB(0, 128, 248), RGB(0, 213, 50));

    // ==========================================
    // 3. RENDER RIGHT COLUMN (TOP BANNER & STATS)
    // ==========================================
    int rw = (int)(height * 0.48f);
    int rx = width - rw - (int)(height * 0.015f);

    // --- Row 1: Player Names and Victory Banner ---
    bool is_win = (r.result == "Victory");
    COLORREF color_result = is_win ? RGB(225, 225, 225) : RGB(231, 76, 60);

    SelectObject(hdcMem, hFontPlayerName);
    SetTextColor(hdcMem, RGB(0, 128, 248));
    std::wstring p1_name_w = ToWide(Win32Utils::CleanPlayerName(r.p1.name));
    RECT rcName1 = { rx, (int)(height * 0.038f), rx + rw / 3, (int)(height * 0.072f) };
    DrawTextW(hdcMem, p1_name_w.c_str(), -1, &rcName1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdcMem, hFontTitle);
    SetTextColor(hdcMem, color_result);
    std::wstring result_w = is_win ? (r.is_weekly ? L"Weekly Victory!" : L"Victory!") : L"Defeat!";
    RECT rcRes = { rx + rw / 4, (int)(height * 0.035f), rx + rw * 3 / 4, (int)(height * 0.072f) };
    DrawTextW(hdcMem, result_w.c_str(), -1, &rcRes, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdcMem, hFontPlayerName);
    SetTextColor(hdcMem, RGB(0, 213, 50));
    std::wstring p2_name_w = ToWide(Win32Utils::CleanPlayerName(r.p2.name));
    RECT rcName2 = { rx + rw * 2 / 3, (int)(height * 0.038f), rx + rw, (int)(height * 0.072f) };
    DrawTextW(hdcMem, p2_name_w.c_str(), -1, &rcName2, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

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
    FillRect(hdcMem, &rcBar1, hBrushP1);
    RECT rcBar2 = { rx + p1_bw, bar_y, rx + rw, bar_y + bar_h };
    FillRect(hdcMem, &rcBar2, hBrushP2);
    DeleteObject(hBrushP1);
    DeleteObject(hBrushP2);

    // Alpha-blend commander portrait into kill share bar
    auto BlendCommanderImage = [&](const std::string& cmdr, const RECT& rc, bool flip_h) {
        if (cmdr.empty()) return;
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
    };

    BlendCommanderImage(r.p1.commander, rcBar1, false);
    BlendCommanderImage(r.p2.commander, rcBar2, true);

    SelectObject(hdcMem, hFontPlayerName);
    SetTextColor(hdcMem, RGB(255, 255, 255));
    int pct1 = (int)(p1_frac * 100.0f + 0.5f);
    int pct2 = 100 - pct1;
    std::wstring pct1_str = std::to_wstring(pct1) + L"%";
    std::wstring pct2_str = std::to_wstring(pct2) + L"%";

    RECT rcPct1 = { rx + 12, bar_y, rx + p1_bw - 8, bar_y + bar_h };
    DrawTextW(hdcMem, pct1_str.c_str(), -1, &rcPct1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT rcPct2 = { rx + p1_bw + 8, bar_y, rx + rw - 12, bar_y + bar_h };
    DrawTextW(hdcMem, pct2_str.c_str(), -1, &rcPct2, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdcMem, hFontMap);
    RECT rcKillsCenter = { rx, bar_y, rx + rw, bar_y + bar_h };
    DrawTextW(hdcMem, L"kills", -1, &rcKillsCenter, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // --- Row 3: Subheader (Commanders, Map Name, Duration, Bonus, APM, Difficulty) ---
    int sub_y = bar_y + bar_h + (int)(height * 0.008f);

    SelectObject(hdcMem, hFontMap);
    SetTextColor(hdcMem, RGB(220, 220, 220));

    // Left: Commander & APM
    std::wstring p1_cmdr_w = ToWide(r.p1.commander);
    RECT rcCmdr1 = { rx, sub_y, rx + 160, sub_y + 20 };
    DrawTextW(hdcMem, p1_cmdr_w.c_str(), -1, &rcCmdr1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    std::wstring p1_apm_w = std::to_wstring(r.p1.apm) + L" APM";
    RECT rcApm1 = { rx, sub_y + 22, rx + 160, sub_y + 42 };
    DrawTextW(hdcMem, p1_apm_w.c_str(), -1, &rcApm1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Right: Commander & APM
    std::wstring p2_cmdr_w = ToWide(r.p2.commander);
    RECT rcCmdr2 = { rx + rw - 160, sub_y, rx + rw, sub_y + 20 };
    DrawTextW(hdcMem, p2_cmdr_w.c_str(), -1, &rcCmdr2, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    std::wstring p2_apm_w = std::to_wstring(r.p2.apm) + L" APM";
    RECT rcApm2 = { rx + rw - 160, sub_y + 22, rx + rw, sub_y + 42 };
    DrawTextW(hdcMem, p2_apm_w.c_str(), -1, &rcApm2, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    // Center: Map Name [MM:SS] [b_earned/b_total] & Difficulty
    int b_earned = r.bonus_earned;
    int b_total = r.bonus_total > 0 ? r.bonus_total : 3;
    std::wstring map_str = ToWide(r.map_name) + L" [" + ToWide(r.form_length) + L"] [" +
                           std::to_wstring(b_earned) + L"/" + std::to_wstring(b_total) + L"]";
    RECT rcMapCenter = { rx + 140, sub_y, rx + rw - 140, sub_y + 20 };
    DrawTextW(hdcMem, map_str.c_str(), -1, &rcMapCenter, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    std::wstring diff_str = ToWide(r.difficulty);
    RECT rcDiff = { rx + 140, sub_y + 22, rx + rw - 140, sub_y + 42 };
    DrawTextW(hdcMem, diff_str.c_str(), -1, &rcDiff, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // --- Helper: Draw Section for P1, P2, and Amon ---
    auto DrawSection = [&](int card_y, const std::string& cmdr_name, const std::string& player_name,
                           int prestige_idx, const std::string& prestige_str,
                           const std::vector<int>& masteries,
                           const std::vector<UnitStat>& units,
                           int p_kills,
                           COLORREF theme_col, bool is_amon, const std::string& enemy_comp_name) {
        // Name (Large, Bold)
        SelectObject(hdcMem, hFontPlayerName);
        SetTextColor(hdcMem, theme_col);
        std::wstring title_str = is_amon ? L"Amon" : ToWide(Win32Utils::CleanPlayerName(player_name));
        RECT rcName = { rx, card_y, rx + rw, card_y + 26 };
        DrawTextW(hdcMem, title_str.c_str(), -1, &rcName, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Prestige Title / Enemy Comp Name
        SelectObject(hdcMem, hFontPrestige);
        std::wstring sub_title;
        if (is_amon) {
            sub_title = enemy_comp_name.empty() ? L"Ravaging Infestation" : ToWide(enemy_comp_name);
        } else {
            if (!prestige_str.empty()) sub_title = ToWide(prestige_str);
            else {
                auto pit = kPrestiges.find(cmdr_name);
                if (pit != kPrestiges.end()) {
                    if (prestige_idx == 1) sub_title = ToWide(pit->second.p1);
                    else if (prestige_idx == 2) sub_title = ToWide(pit->second.p2);
                    else if (prestige_idx == 3) sub_title = ToWide(pit->second.p3);
                    else sub_title = ToWide(pit->second.p0);
                }
            }
        }
        SetTextColor(hdcMem, theme_col);
        RECT rcPres = { rx, card_y + 28, rx + rw, card_y + 48 };
        DrawTextW(hdcMem, sub_title.c_str(), -1, &rcPres, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        int cur_y = card_y + 52;

        // 6 Masteries (P1 and P2 only, 6 lines)
        if (!is_amon) {
            auto m_it = kCommanderMasteries.find(cmdr_name);
            if (m_it != kCommanderMasteries.end()) {
                SelectObject(hdcMem, hFontMastery);
                for (int m_idx = 0; m_idx < 6; ++m_idx) {
                    int pts = (m_idx < (int)masteries.size()) ? masteries[m_idx] : 0;
                    std::wstring mname = ToWide(m_it->second[m_idx]);

                    COLORREF mcol = (pts > 0) ? RGB(255, 255, 255) : RGB(120, 130, 145);
                    SetTextColor(hdcMem, mcol);

                    RECT rcPts = { rx, cur_y + m_idx * 18, rx + 24, cur_y + (m_idx + 1) * 18 };
                    std::wstring pts_str = std::to_wstring(pts);
                    DrawTextW(hdcMem, pts_str.c_str(), -1, &rcPts, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

                    RECT rcMName = { rx + 30, cur_y + m_idx * 18, rx + rw, cur_y + (m_idx + 1) * 18 };
                    DrawTextW(hdcMem, mname.c_str(), -1, &rcMName, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                }
                cur_y += 6 * 18 + 6;
            }
        }

        // Units Breakdown Table
        // Headers: Unit | kills | created | lost
        SelectObject(hdcMem, hFontUnit);
        SetTextColor(hdcMem, RGB(255, 255, 255));
        RECT rcHKills = { rx + (int)(rw * 0.40f), cur_y, rx + (int)(rw * 0.65f), cur_y + 18 };
        DrawTextW(hdcMem, L"kills", -1, &rcHKills, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SetTextColor(hdcMem, RGB(0, 213, 50)); // Green header
        RECT rcHCreated = { rx + (int)(rw * 0.70f), cur_y, rx + (int)(rw * 0.84f), cur_y + 18 };
        DrawTextW(hdcMem, L"created", -1, &rcHCreated, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SetTextColor(hdcMem, RGB(255, 51, 51)); // Red header
        RECT rcHLost = { rx + (int)(rw * 0.86f), cur_y, rx + rw, cur_y + 18 };
        DrawTextW(hdcMem, L"lost", -1, &rcHLost, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        cur_y += 20;

        // Preprocess units:
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

        // Render Unit Rows (up to 5 top units)
        int max_rows = 5;
        HBRUSH hBrushTheme = CreateSolidBrush(theme_col);
        for (int i = 0; i < max_rows && i < (int)proc_units.size(); ++i) {
            const auto& u = proc_units[i];
            int row_y = cur_y + i * 22;

            // Unit Name
            SelectObject(hdcMem, hFontUnit);
            SetTextColor(hdcMem, theme_col);
            std::wstring uname = ToWide(u.name);
            RECT rcUName = { rx, row_y, rx + (int)(rw * 0.38f), row_y + 20 };
            DrawTextW(hdcMem, uname.c_str(), -1, &rcUName, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            // Proportional Kill Bar: bg_width = 50vh * u.kills / total_kills (matching original main.js:573)
            int kbar_x = rx + (int)(rw * 0.38f);
            int kbar_w = (int)(rw * 0.30f);
            int kbar_h = 16;
            int fill_w = (int)(height * 0.50f * ((float)u.kills / match_kills));
            if (u.kills > 0 && fill_w < 6) fill_w = 6;
            if (fill_w > kbar_w) fill_w = kbar_w;

            RECT rcBarFill = { kbar_x, row_y + 2, kbar_x + fill_w, row_y + 2 + kbar_h };
            FillRect(hdcMem, &rcBarFill, hBrushTheme);

            // Kills Text: e.g. "37% | 307" (percentage relative to player total kills)
            SelectObject(hdcMem, hFontUnitBold);
            SetTextColor(hdcMem, RGB(255, 255, 255));
            int pct = (int)(((float)u.kills / p_tot) * 100.0f + 0.5f);
            std::wstring ktext = std::to_wstring(pct) + L"% | " + std::to_wstring(u.kills);
            RECT rcKText = { kbar_x + 6, row_y, kbar_x + kbar_w - 6, row_y + 20 };
            DrawTextW(hdcMem, ktext.c_str(), -1, &rcKText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            // Created Count
            SelectObject(hdcMem, hFontUnit);
            SetTextColor(hdcMem, RGB(0, 213, 50));
            bool hide_counts = (u.name.find("Top Bar") != std::string::npos);
            std::wstring ctext = (hide_counts || u.created <= 0) ? L"-" : std::to_wstring(u.created);
            RECT rcCText = { rx + (int)(rw * 0.70f), row_y, rx + (int)(rw * 0.84f), row_y + 20 };
            DrawTextW(hdcMem, ctext.c_str(), -1, &rcCText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            // Lost Count
            SetTextColor(hdcMem, RGB(255, 51, 51));
            std::wstring ltext = (hide_counts || u.lost <= 0) ? L"-" : std::to_wstring(u.lost);
            RECT rcLText = { rx + (int)(rw * 0.86f), row_y, rx + rw, row_y + 20 };
            DrawTextW(hdcMem, ltext.c_str(), -1, &rcLText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        DeleteObject(hBrushTheme);
    };

    // Calculate Y offsets dynamically for sections
    int p1_y = sub_y + 52;
    int p2_y = p1_y + (int)(height * 0.28f);
    int amon_y = p2_y + (int)(height * 0.28f);

    // Draw P1 (Blue #0080F8)
    DrawSection(p1_y, r.p1.commander, r.p1.name, r.p1.prestige, r.p1.prestige_name,
                r.p1.masteries, r.p1.units, p1_kills, RGB(0, 128, 248), false, "");

    // Draw P2 (Green #00D532)
    DrawSection(p2_y, r.p2.commander, r.p2.name, r.p2.prestige, r.p2.prestige_name,
                r.p2.masteries, r.p2.units, p2_kills, RGB(0, 213, 50), false, "");

    // Draw Amon (Red #FF3333)
    DrawSection(amon_y, "", "", 0, "",
                {}, r.amon_units, 0, RGB(255, 51, 51), true, r.enemy_comp);

    // 4. Alpha Post-Processing Pass:
    // GDI DrawTextW/Polyline/MoveToEx/FillRect writes RGB with Alpha=0 into 32-bit DIBSection.
    // For UpdateLayeredWindow(ULW_ALPHA), restore proper pre-multiplied alpha channel
    // for all drawn foreground elements (text, grid lines, polylines, labels).
    for (int i = 0; i < width * height; ++i) {
        uint32_t px = pixels[i];
        uint8_t r_c = (px >> 16) & 0xFF;
        uint8_t g_c = (px >> 8) & 0xFF;
        uint8_t b_c = px & 0xFF;
        uint8_t max_c = std::max({ r_c, g_c, b_c });
        if (max_c > 0) {
            uint8_t a = (max_c < 180) ? (uint8_t)std::min(255, (int)max_c + 120) : 255;
            if (a < max_c) a = max_c;
            pixels[i] = ((uint32_t)a << 24) | (r_c << 16) | (g_c << 8) | b_c;
        }
    }

    if (!m_dump_path.empty()) {
        std::ofstream out(m_dump_path, std::ios::binary);
        if (out.is_open()) {
            out.write((const char*)&width, sizeof(width));
            out.write((const char*)&height, sizeof(height));
            out.write((const char*)pixels, width * height * 4);
            out.close();
        }
        m_dump_path.clear();
    }

    // Apply per-pixel alpha blending to layered window with synchronized screen position
    float progress = m_anim_t;
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    float ease_factor = (m_anim_state == OverlayAnimState::SlidingIn || m_anim_state == OverlayAnimState::Visible)
                            ? (1.0f - std::pow(1.0f - progress, 3.0f))
                            : std::pow(progress, 2.0f);
    int cur_x = screen_w - (int)(m_overlay_w * ease_factor);

    POINT ptZero = { 0, 0 };
    POINT ptDst = { cur_x, 0 };
    SIZE sz = { width, height };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(m_hwnd, hdcScreen, &ptDst, &sz, hdcMem, &ptZero, RGB(0, 0, 0), &blend, ULW_ALPHA);

    // Cleanup GDI objects
    DeleteObject(hFontTitle);
    DeleteObject(hFontPlayerName);
    DeleteObject(hFontPrestige);
    DeleteObject(hFontMap);
    DeleteObject(hFontMastery);
    DeleteObject(hFontUnit);
    DeleteObject(hFontUnitBold);
    DeleteObject(hFontChartTitle);
    DeleteObject(hFontAxis);

    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

} // namespace sc2
