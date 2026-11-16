#include "ally_toast.h"
#include "../../db/database.h"
#include "../../app_state.h"
#include "../../utils/win32_utils.h"
#include "../../i18n/i18n.h"
#include <backends/imgui_impl_win32.h>

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace sc2 {

static inline std::wstring ToWide(const std::string& str) {
    return Win32Utils::Utf8ToWide(str);
}

LRESULT CALLBACK AllyToastWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_NCHITTEST:
            return HTTRANSPARENT; // Click through
        case WM_DESTROY:
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

AllyToastWindow& AllyToastWindow::Instance() {
    static AllyToastWindow instance;
    return instance;
}

void AllyToastWindow::Init(HINSTANCE hInstance) {
    m_hInstance = hInstance;

    WNDCLASSEXW wc = {
        sizeof(wc),
        CS_CLASSDC,
        WndProc,
        0L, 0L,
        hInstance,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        L"SC2AllyToastClass",
        nullptr
    };
    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        L"SC2 Ally Info",
        WS_POPUP,
        0, 0, 520, 72,
        nullptr, nullptr, hInstance, nullptr
    );
}

void AllyToastWindow::Shutdown() {
    Hide();
    if (m_hwnd) {
        SetWindowPos(m_hwnd, nullptr, -10000, -10000, 0, 0,
                     SWP_HIDEWINDOW | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if (m_hInstance) {
        UnregisterClassW(L"SC2AllyToastClass", m_hInstance);
        m_hInstance = nullptr;
    }
}

void AllyToastWindow::ShowAllyInfo(const std::string& ally_name, float duration) {
    if (ally_name.empty()) return;

    m_ally_name = Win32Utils::CleanPlayerName(ally_name);
    m_note = Database::Instance().GetPlayerNote(m_ally_name);

    // Query stats for this player
    int total_dummy = 0;
    auto players = Database::Instance().QueryPlayers(m_ally_name, "total_games", false, 5, 0, total_dummy);
    bool found_exact = false;
    for (const auto& p : players) {
        if (p.name == m_ally_name) {
            m_total_games = p.total_games;
            m_wins = p.wins;
            m_winrate = p.winrate;
            m_apm = p.median_apm;
            if (m_note.empty()) m_note = p.note;
            found_exact = true;
            break;
        }
    }
    if (!found_exact) {
        m_total_games = 0;
        m_wins = 0;
        m_winrate = 0.0;
        m_apm = 0;
    }

    m_remaining_time = duration > 0.0f ? duration : (float)AppState::Instance().hud_duration_seconds;
    m_active = true;

    Render();
}

void AllyToastWindow::ShowTestToast() {
    m_ally_name = "KerriganPro";
    m_note = "Expert with Omega Worms & Mastery 90";
    m_total_games = 18;
    m_wins = 16;
    m_winrate = 88.9;
    m_apm = 175;
    m_remaining_time = (float)AppState::Instance().hud_duration_seconds;
    m_active = true;
    Render();
}

void AllyToastWindow::Hide() {
    m_active = false;
    if (m_hwnd) {
        SetWindowPos(m_hwnd, nullptr, -10000, -10000, 0, 0,
                     SWP_HIDEWINDOW | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(m_hwnd, SW_HIDE);
    }
}

void AllyToastWindow::Toggle(const std::string& ally_name) {
    if (m_active) {
        std::string clean = Win32Utils::CleanPlayerName(ally_name);
        if (!clean.empty() && clean != m_ally_name) {
            ShowAllyInfo(clean);
        } else {
            Hide();
        }
    } else {
        if (!ally_name.empty()) {
            ShowAllyInfo(ally_name);
        } else {
            int total = 0;
            auto replays = Database::Instance().QueryReplays("", 1, 0, total);
            if (!replays.empty() && !replays[0].p2.name.empty()) {
                ShowAllyInfo(replays[0].p2.name);
            } else {
                ShowTestToast();
            }
        }
    }
}

void AllyToastWindow::Refresh() {
    if (m_active) Render();
}

void AllyToastWindow::Update(float delta_time) {
    if (!m_active) return;
    m_remaining_time -= delta_time;
    if (m_remaining_time <= 0.0f) {
        Hide();
    }
}

void AllyToastWindow::Render() {
    if (!m_hwnd) return;

    HWND sc2_hwnd = Win32Utils::FindStarCraftIIWindow();
    HMONITOR hMon = MonitorFromWindow(sc2_hwnd ? sc2_hwnd : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
    float dpi_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(hMon);
    if (dpi_scale <= 0.0f) dpi_scale = 1.0f;

    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(hMon, &mi);
    int mon_x = mi.rcMonitor.left;
    int mon_y = mi.rcMonitor.top;
    int mon_w = mi.rcMonitor.right - mi.rcMonitor.left;
    int mon_h = mi.rcMonitor.bottom - mi.rcMonitor.top;

    const int width = (int)(540 * dpi_scale);
    const int height = (int)(76 * dpi_scale);

    int win_x = mon_x + (mon_w - width) / 2;
    int win_y = mon_y + (int)(mon_h * 0.05f); // Near top center

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
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    if (!hBitmap || !pBits) {
        if (hBitmap) DeleteObject(hBitmap);
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
        return;
    }
    HGDIOBJ hOldBmp = SelectObject(hdcMem, hBitmap);

    // 1. Dark Glass Background
    uint32_t* pixels = (uint32_t*)pBits;
    const auto& state = AppState::Instance();
    const int opacity_percent = std::clamp(state.toast_opacity_percent, 0, 100);
    const uint8_t alpha = static_cast<uint8_t>((255 * opacity_percent + 50) / 100);
    uint8_t r_bg = 12, g_bg = 16, b_bg = 24;
    uint32_t premul_bg = (alpha << 24) |
                         ((uint8_t)((r_bg * alpha) / 255) << 16) |
                         ((uint8_t)((g_bg * alpha) / 255) << 8) |
                         ((uint8_t)((b_bg * alpha) / 255));

    const uint8_t border_alpha = state.toast_show_border ? alpha : 0;
    uint8_t br = 46, bg = 204, bb = 113; // Emerald Glow border
    uint32_t premul_border = (border_alpha << 24) |
                             ((uint8_t)((br * border_alpha) / 255) << 16) |
                             ((uint8_t)((bg * border_alpha) / 255) << 8) |
                             ((uint8_t)((bb * border_alpha) / 255));

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            bool is_border = state.toast_show_border &&
                             (x <= 1 || x >= width - 2 || y <= 1 || y >= height - 2);
            pixels[y * width + x] = is_border ? premul_border : premul_bg;
        }
    }

    SetBkMode(hdcMem, TRANSPARENT);

    HFONT hFontBold = CreateFontW((int)(18 * dpi_scale), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");
    HFONT hFontSmall = CreateFontW((int)(14 * dpi_scale), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");

    int pad_x = (int)(16 * dpi_scale);
    int mid_x = (int)(270 * dpi_scale);
    int top_y1 = (int)(10 * dpi_scale);
    int top_y2 = (int)(36 * dpi_scale);
    int bot_y1 = (int)(38 * dpi_scale);
    int bot_y2 = height - (int)(10 * dpi_scale);

    bool is_zh = (state.language == 0 || state.language_code == "zh_CN");

    // Line 1: Ally Name & Stats
    HGDIOBJ hOldFont = nullptr;
    if (hFontBold) {
        hOldFont = SelectObject(hdcMem, hFontBold);
    }
    SetTextColor(hdcMem, RGB(46, 204, 113));
    std::wstring ally_title = ToWide(TR("ALLY: ")) + ToWide(m_ally_name);
    RECT rcTitle = { pad_x, top_y1, mid_x, top_y2 };
    DrawTextW(hdcMem, ally_title.c_str(), -1, &rcTitle, DT_SINGLELINE | DT_VCENTER);

    std::wostringstream oss;
    if (m_total_games > 0) {
        wchar_t stats_buf[128];
        std::wstring fmt = ToWide(TR("Ally_Toast_Stats_Format"));
        swprintf_s(stats_buf, fmt.c_str(), m_total_games, m_winrate, m_apm);
        oss << stats_buf;
    } else {
        oss << ToWide(TR("New Ally (0 previous matches)"));
    }
    std::wstring stats_w = oss.str();
    SetTextColor(hdcMem, RGB(255, 230, 100)); // Gold
    RECT rcStats = { mid_x, top_y1, width - pad_x, top_y2 };
    DrawTextW(hdcMem, stats_w.c_str(), -1, &rcStats, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);

    // Line 2: Note / Hint
    SelectObject(hdcMem, hFontSmall);
    std::wstring note_w;
    if (!m_note.empty()) {
        note_w = ToWide(TR("Note: ")) + ToWide(m_note);
        SetTextColor(hdcMem, RGB(220, 230, 245));
    } else {
        note_w = ToWide(TR("No custom note (Edit notes in Players tab)"));
        SetTextColor(hdcMem, RGB(130, 150, 175));
    }
    RECT rcNote = { pad_x, bot_y1, width - pad_x, bot_y2 };
    DrawTextW(hdcMem, note_w.c_str(), -1, &rcNote, DT_SINGLELINE | DT_VCENTER);

    // Restore alpha on GDI text pixels and apply the user-selected toast opacity.
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint32_t val = pixels[y * width + x];
            if ((val & 0xFF000000) == 0 && (val & 0x00FFFFFF) != 0) {
                const uint8_t r = static_cast<uint8_t>((val >> 16) & 0xFF);
                const uint8_t g = static_cast<uint8_t>((val >> 8) & 0xFF);
                const uint8_t b = static_cast<uint8_t>(val & 0xFF);
                pixels[y * width + x] = (static_cast<uint32_t>(alpha) << 24) |
                                         (static_cast<uint32_t>((r * alpha + 127) / 255) << 16) |
                                         (static_cast<uint32_t>((g * alpha + 127) / 255) << 8) |
                                         static_cast<uint32_t>((b * alpha + 127) / 255);
            }
        }
    }

    POINT ptPos = { win_x, win_y };
    SIZE sizeWnd = { width, height };
    POINT ptSrc = { 0, 0 };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    SetWindowPos(m_hwnd, HWND_TOPMOST, win_x, win_y, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);

    UpdateLayeredWindow(m_hwnd, hdcScreen, &ptPos, &sizeWnd, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);

    if (hOldFont) {
        SelectObject(hdcMem, hOldFont);
    }
    if (hFontBold) DeleteObject(hFontBold);
    if (hFontSmall) DeleteObject(hFontSmall);
    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

} // namespace sc2
