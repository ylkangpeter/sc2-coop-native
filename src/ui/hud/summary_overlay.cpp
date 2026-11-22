#include "summary_overlay.h"
#include "summary_overlay_types.h"
#include "summary_overlay_render.h"
#include "../../db/database.h"
#include "../../app_state.h"
#include "../../utils/win32_utils.h"
#include "../../utils/diagnostics.h"
#include "../../data/sc2_constants.h"
#include "../../replay/replay_parser.h"
#include "../../i18n/i18n.h"
#include "../../utils/hotkey_manager.h"
#include <stb/stb_image.h>

#include <algorithm>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <cmath>

namespace sc2 {

static constexpr UINT WM_REFRESH_OVERLAY_TOPMOST = WM_APP + 1;

static inline std::wstring ToWide(const std::string& str) {
    return Win32Utils::Utf8ToWide(str);
}

LRESULT CALLBACK SummaryOverlay::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_REFRESH_OVERLAY_TOPMOST:
            SummaryOverlay::Instance().m_topmost_message_pending = false;
            SummaryOverlay::Instance().EnsureTopmost(true);
            return 0;
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
        m_font_path = font_path.substr(0, pos + 1) + L"assets\\Eurostile-Reg.otf";
        AddFontResourceExW(m_font_path.c_str(), FR_PRIVATE, nullptr);
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

    HWND sc2_hwnd = Win32Utils::FindStarCraftIIWindow();
    HMONITOR hMon = MonitorFromWindow(sc2_hwnd ? sc2_hwnd : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(hMon, &mi);
    int mon_w = mi.rcMonitor.right - mi.rcMonitor.left;
    int mon_h = mi.rcMonitor.bottom - mi.rcMonitor.top;

    m_overlay_h = mon_h;
    m_overlay_w = (int)(mon_h * 1.0f); // 100vh width, matching original CSS
    if (m_overlay_w > mon_w) m_overlay_w = mon_w;

    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wc.lpszClassName,
        L"SC2 Match Summary Overlay",
        WS_POPUP,
        mi.rcMonitor.left + mon_w, mi.rcMonitor.top, m_overlay_w, m_overlay_h,
        nullptr, nullptr, hInstance, nullptr
    );
    if (!m_hwnd) {
        LogDiagnostic("summary", "CreateWindowEx failed; error=" + std::to_string(GetLastError()));
    }

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
    StopForegroundWatch();
    m_anim_state = OverlayAnimState::Hidden;
    m_anim_t = 0.0f;
    if (!m_font_path.empty()) {
        RemoveFontResourceExW(m_font_path.c_str(), FR_PRIVATE, nullptr);
        m_font_path.clear();
    }
    RemoveFontResourceExW(L"assets\\Eurostile-Reg.otf", FR_PRIVATE, nullptr);
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
    HotkeyManager::Instance().UpdateContext();
}

void SummaryOverlay::ShowMatch(const ReplayRecord& rec, bool is_best_time, float duration) {
    m_current_record = rec;
    m_is_best_time = is_best_time;
    m_total_duration = duration > 0.0f ? duration : (float)AppState::Instance().hud_duration_seconds;
    m_remaining_time = m_total_duration;

    // Details are intentionally loaded separately from the history list. Prefer the
    // compact database cache, then fall back to parsing the original replay.
    bool details_loaded = false;
    if (!m_current_record.file_path.empty()) {
        details_loaded = Database::Instance().LoadReplayDetails(m_current_record);
    }
    if (!details_loaded && m_current_record.p1.units.empty() && !m_current_record.file_path.empty()) {
        std::wstring wpath = Win32Utils::Utf8ToWide(m_current_record.file_path);
        try {
            auto full_rec = ReplayParser::ParseFile(wpath);
            if (full_rec.has_value()) {
                m_current_record = full_rec.value();
                Database::Instance().SaveReplay(m_current_record);
            }
        } catch (const std::exception& e) {
            std::cerr << "SummaryOverlay fallback parse error: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "SummaryOverlay fallback parse unknown error" << std::endl;
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
    UpdatePosition(true);
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    m_topmost_check_elapsed = 0.0f;
    StartForegroundWatch();
    HotkeyManager::Instance().UpdateContext();
    LogDiagnostic("summary", "show requested; visible=" + std::to_string(IsWindowVisible(m_hwnd)) +
        "; duration=" + std::to_string(m_total_duration));
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
        demo.p1.commander_level = 15;
        demo.p1.commander_mastery_level = 523;
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
        demo.p2.commander_level = 15;
        demo.p2.commander_mastery_level = 90;
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
    } else if (m_anim_state == OverlayAnimState::SlidingOut) {
        m_anim_state = OverlayAnimState::SlidingIn;
        if (m_hwnd) {
            UpdatePosition(true);
            ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        }
    } else {
        ShowLatest();
    }
}

void SummaryOverlay::ToggleMatch(const ReplayRecord& rec) {
    if ((m_anim_state == OverlayAnimState::Visible || m_anim_state == OverlayAnimState::SlidingIn) &&
        m_current_record.file_path == rec.file_path) {
        Hide();
    } else {
        ShowMatch(rec);
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
            LogDiagnostic("summary", "slide-in complete");
        }
        UpdatePosition();
    } else if (m_anim_state == OverlayAnimState::Visible) {
        m_remaining_time -= delta_time;
        if (m_remaining_time <= 0.0f) {
            Hide();
        } else {
            // Foreground notifications do immediate recovery. Once a second,
            // check for missed events or another topmost game window overtaking
            // us, and only change Z order if recovery is actually needed.
            m_topmost_check_elapsed += delta_time;
            if (m_topmost_check_elapsed >= 1.0f) {
                m_topmost_check_elapsed = 0.0f;
                EnsureTopmost();
            }
        }
    } else if (m_anim_state == OverlayAnimState::SlidingOut) {
        m_anim_t -= delta_time / 0.30f; // Slide out over 300ms
        if (m_anim_t <= 0.0f) {
            m_anim_t = 0.0f;
            m_anim_state = OverlayAnimState::Hidden;
            StopForegroundWatch();
            HotkeyManager::Instance().UpdateContext();
            if (m_hwnd) {
                SetWindowPos(m_hwnd, nullptr, -10000, -10000, 0, 0,
                             SWP_HIDEWINDOW | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                ShowWindow(m_hwnd, SW_HIDE);
            }
        } else {
            UpdatePosition();
        }
    }
}

void SummaryOverlay::UpdatePosition(bool force_show) {
    if (!m_hwnd) return;
    HWND sc2_hwnd = Win32Utils::FindStarCraftIIWindow();
    HMONITOR hMon = MonitorFromWindow(sc2_hwnd ? sc2_hwnd : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(hMon, &mi);
    int mon_x = mi.rcMonitor.left;
    int mon_y = mi.rcMonitor.top;
    int mon_w = mi.rcMonitor.right - mi.rcMonitor.left;
    int mon_h = mi.rcMonitor.bottom - mi.rcMonitor.top;

    m_overlay_h = mon_h;
    m_overlay_w = (int)(mon_h * 1.0f);
    if (m_overlay_w > mon_w) m_overlay_w = mon_w;

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

    int cur_x = mon_x + mon_w - (int)(m_overlay_w * ease_factor);
    int cur_y = mon_y;
    UINT flags = SWP_NOACTIVATE | SWP_NOZORDER;
    if (force_show) flags = SWP_NOACTIVATE | SWP_SHOWWINDOW;
    SetWindowPos(m_hwnd, HWND_TOPMOST, cur_x, cur_y, m_overlay_w, m_overlay_h, flags);
}

void SummaryOverlay::StartForegroundWatch() {
    if (!m_foreground_hook) {
        m_foreground_hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
            nullptr, ForegroundChanged, 0, 0, WINEVENT_OUTOFCONTEXT);
    }
}

void SummaryOverlay::StopForegroundWatch() {
    if (m_foreground_hook) {
        UnhookWinEvent(m_foreground_hook);
        m_foreground_hook = nullptr;
    }
    m_topmost_message_pending = false;
}

void CALLBACK SummaryOverlay::ForegroundChanged(HWINEVENTHOOK, DWORD, HWND hwnd, LONG, LONG, DWORD, DWORD) {
    auto& overlay = Instance();
    // Out-of-context foreground events are asynchronous and do not intercept
    // input. Coalesce them onto our own queue; never do window work in callbacks.
    if (!overlay.IsActive() || !overlay.m_hwnd || hwnd == overlay.m_hwnd ||
        overlay.m_topmost_message_pending) return;
    overlay.m_topmost_message_pending = true;
    if (!PostMessageW(overlay.m_hwnd, WM_REFRESH_OVERLAY_TOPMOST, 0, 0)) {
        overlay.m_topmost_message_pending = false;
    }
}

void SummaryOverlay::EnsureTopmost(bool force) {
    if (!IsActive() || !m_hwnd || !IsWindowVisible(m_hwnd)) return;
    bool needed = force || !(GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST);
    HWND game = Win32Utils::FindStarCraftIIWindow();
    if (!needed && game && game == GetForegroundWindow()) {
        // Being topmost is not sufficient if SC2 is itself above us in the
        // topmost group. Stop as soon as either window is reached.
        HWND current = GetTopWindow(nullptr);
        for (int count = 0; current && count < 512; ++count, current = GetWindow(current, GW_HWNDNEXT)) {
            if (current == m_hwnd) break;
            if (current == game) { needed = true; break; }
        }
    }
    if (needed) {
        SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void SummaryOverlay::Render() {
    if (!m_hwnd) return;

    HWND sc2_hwnd = Win32Utils::FindStarCraftIIWindow();
    HMONITOR hMon = MonitorFromWindow(sc2_hwnd ? sc2_hwnd : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(hMon, &mi);
    int mon_x = mi.rcMonitor.left;
    int mon_y = mi.rcMonitor.top;
    int mon_w = mi.rcMonitor.right - mi.rcMonitor.left;
    int mon_h = mi.rcMonitor.bottom - mi.rcMonitor.top;

    m_overlay_h = mon_h;
    m_overlay_w = (int)(mon_h * 1.0f);
    if (m_overlay_w > mon_w) m_overlay_w = mon_w;

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
    HGDIOBJ hOldFont = (HGDIOBJ)GetCurrentObject(hdcMem, OBJ_FONT);

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

    // Dynamic font sizes matching original CSS vh units (Eurostile / Microsoft YaHei)
    bool is_zh = (AppState::Instance().language == 0 || AppState::Instance().language_code == "zh_CN");
    const wchar_t* kFontFamily = is_zh ? L"Microsoft YaHei" : L"Eurostile";
    SummaryOverlayFonts fonts;
    fonts.hFontTitle = CreateFontW((int)(height * 0.026f), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, kFontFamily);
    fonts.hFontPlayerName = CreateFontW((int)(height * 0.025f), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, kFontFamily);
    fonts.hFontTimestamp = CreateFontW((int)(height * 0.0145f), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, kFontFamily);
    fonts.hFontPrestige = CreateFontW((int)(height * 0.016f), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, kFontFamily);
    fonts.hFontMap = CreateFontW((int)(height * 0.017f), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, kFontFamily);
    fonts.hFontMastery = CreateFontW((int)(height * 0.015f), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, kFontFamily);
    fonts.hFontUnit = CreateFontW((int)(height * 0.016f), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, kFontFamily);
    fonts.hFontUnitBold = CreateFontW((int)(height * 0.016f), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, kFontFamily);
    fonts.hFontChartTitle = CreateFontW((int)(height * 0.018f), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, kFontFamily);
    fonts.hFontAxis = CreateFontW((int)(height * 0.013f), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, kFontFamily);
    fonts.hFontRotated = m_hFontRotated;

    const auto& r = m_current_record;

    // 2. Render Left Column (4 Line Charts)
    const auto& s1 = r.p1_series;
    const auto& s2 = r.p2_series;
    int chart_w = (int)(height * 0.38f);
    int chart_h = (int)((height - height * 0.10f) / 4);
    int chart_x = width - (int)(height * 0.88f);
    int top_margin = (int)(height * 0.045f);

    float dur = (float)r.duration_seconds;
    DrawSummaryChart(hdcMem, fonts, chart_x, top_margin, chart_w, chart_h, ToWide(TR("Army value")),
                     dur, s1.time_stamps, s1.army_value, s2.time_stamps, s2.army_value, RGB(0, 128, 248), RGB(0, 213, 50));
    DrawSummaryChart(hdcMem, fonts, chart_x, top_margin + chart_h, chart_w, chart_h, ToWide(TR("Supply used")),
                     dur, s1.time_stamps, s1.supply_used, s2.time_stamps, s2.supply_used, RGB(0, 128, 248), RGB(0, 213, 50));
    DrawSummaryChart(hdcMem, fonts, chart_x, top_margin + chart_h * 2, chart_w, chart_h, ToWide(TR("Kill count")),
                     dur, s1.time_stamps, s1.kill_count, s2.time_stamps, s2.kill_count, RGB(0, 128, 248), RGB(0, 213, 50));
    DrawSummaryChart(hdcMem, fonts, chart_x, top_margin + chart_h * 3, chart_w, chart_h, ToWide(TR("Resource collection rate")),
                     dur, s1.time_stamps, s1.collection_rate, s2.time_stamps, s2.collection_rate, RGB(0, 128, 248), RGB(0, 213, 50));

    // 3. Render Right Column (Top Banner & Sections)
    int rw = (int)(height * 0.48f);
    int rx = width - rw - (int)(height * 0.015f);

    DrawSummaryBanner(hdcMem, pixels, width, height, fonts, r, rx, rw);

    int bar_y = (int)(height * 0.076f);
    int bar_h = (int)(height * 0.040f);
    int sub_y = bar_y + bar_h + (int)(height * 0.008f);

    int p1_kills = r.p1.kills;
    int p2_kills = r.p2.kills;
    if (p1_kills <= 0 && p2_kills <= 0) {
        for (const auto& u : r.p1.units) p1_kills += u.kills;
        for (const auto& u : r.p2.units) p2_kills += u.kills;
    }
    int total_kills = p1_kills + p2_kills;

    int p1_y = sub_y + 52;
    int p2_y = p1_y + (int)(height * 0.28f);
    int amon_y = p2_y + (int)(height * 0.28f);

    // Draw P1 (Blue #0080F8)
    DrawSummarySection(hdcMem, fonts, rx, rw, height, total_kills,
                       p1_y, r.p1.commander, r.p1.name, r.p1.prestige, r.p1.prestige_name,
                       r.p1.masteries, r.p1.units, p1_kills, RGB(0, 128, 248), false, "");

    // Draw P2 (Green #00D532)
    DrawSummarySection(hdcMem, fonts, rx, rw, height, total_kills,
                       p2_y, r.p2.commander, r.p2.name, r.p2.prestige, r.p2.prestige_name,
                       r.p2.masteries, r.p2.units, p2_kills, RGB(0, 213, 50), false, "");

    // Draw Amon (Red #FF3333)
    DrawSummarySection(hdcMem, fonts, rx, rw, height, total_kills,
                       amon_y, "", "", 0, "",
                       {}, r.amon_units, 0, RGB(255, 51, 51), true, r.enemy_comp);

    // 4. Alpha Post-Processing Pass
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
    int cur_x = mon_x + mon_w - (int)(m_overlay_w * ease_factor);
    int cur_y = mon_y;

    POINT ptZero = { 0, 0 };
    POINT ptDst = { cur_x, cur_y };
    SIZE sz = { width, height };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    if (!UpdateLayeredWindow(m_hwnd, hdcScreen, &ptDst, &sz, hdcMem, &ptZero, RGB(0, 0, 0), &blend, ULW_ALPHA)) {
        LogDiagnostic("summary", "UpdateLayeredWindow failed; error=" + std::to_string(GetLastError()));
    }

    // Cleanup GDI objects
    if (hOldFont) {
        SelectObject(hdcMem, hOldFont);
    }

    DeleteObject(fonts.hFontTitle);
    DeleteObject(fonts.hFontPlayerName);
    DeleteObject(fonts.hFontTimestamp);
    DeleteObject(fonts.hFontPrestige);
    DeleteObject(fonts.hFontMap);
    DeleteObject(fonts.hFontMastery);
    DeleteObject(fonts.hFontUnit);
    DeleteObject(fonts.hFontUnitBold);
    DeleteObject(fonts.hFontChartTitle);
    DeleteObject(fonts.hFontAxis);

    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

} // namespace sc2
