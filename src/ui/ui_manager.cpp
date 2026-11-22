#include "ui_manager.h"
#include "frame_renderer.h"
#include "../app_state.h"
#include "../i18n/i18n.h"
#include "../db/database.h"
#include "texture_loader.h"
#include "hud/summary_overlay.h"
#include "hud/ally_toast.h"
#include "../utils/hotkey_manager.h"
#include "../utils/win32_utils.h"
#include "../utils/diagnostics.h"
#include "../replay/replay_watcher.h"

#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>
#include <implot.h>
#include <iostream>
#include <filesystem>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace sc2 {

UIManager& UIManager::Instance() {
    static UIManager instance;
    return instance;
}

LRESULT WINAPI UIManager::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
        case WM_ACTIVATEAPP:
            if (!wParam && HotkeyManager::Instance().IsRecording()) HotkeyManager::Instance().StopRecording();
            break;
        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) {
                UIManager::Instance().m_minimized = true;
                return 0;
            }
            UIManager::Instance().m_minimized = false;
            if (UIManager::Instance().m_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
                UIManager::Instance().CleanupRenderTarget();
                UIManager::Instance().m_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
                UIManager::Instance().CreateRenderTarget();
            }
            return 0;

        case WM_DPICHANGED: {
            RECT* const prcNewWindow = (RECT*)lParam;
            SetWindowPos(hWnd,
                         nullptr,
                         prcNewWindow->left,
                         prcNewWindow->top,
                         prcNewWindow->right - prcNewWindow->left,
                         prcNewWindow->bottom - prcNewWindow->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);

            float new_dpi_scale = (float)LOWORD(wParam) / 96.0f;
            UIManager::Instance().OnDpiChanged(new_dpi_scale);
            return 0;
        }

        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
                return 0;
            break;

        case WM_TRAYICON:
            if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
                ShowWindow(hWnd, SW_RESTORE);
                SetForegroundWindow(hWnd);
            } else if (lParam == WM_RBUTTONUP) {
                UIManager::Instance().ShowTrayMenu();
            }
            return 0;

        case WM_HOTKEY:
            HotkeyManager::Instance().OnHotkey((int)wParam);
            return 0;

        case WM_USER_NEW_MATCH:
            UIManager::Instance().ProcessPendingMatches();
            return 0;

        case WM_USER_HOTKEY_ACTION:
            HotkeyManager::Instance().DispatchAction(static_cast<HotkeyAction>(wParam));
            return 0;

        case WM_USER_HOTKEY_RECORDED:
            HotkeyManager::Instance().ApplyRecordedHotkey(static_cast<int>(wParam), (UINT)LOWORD(lParam), (UINT)HIWORD(lParam));
            return 0;

        case WM_USER_HOTKEY_RECORD_CANCELLED:
            HotkeyManager::Instance().StopRecording();
            return 0;

        case WM_USER_DISMISS_OVERLAYS:
            SummaryOverlay::Instance().Hide();
            AllyToastWindow::Instance().Hide();
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_TRAY_SHOW:
                    ShowWindow(hWnd, SW_RESTORE);
                    SetForegroundWindow(hWnd);
                    break;
                case ID_TRAY_SCAN:
                    AppState::Instance().StartScanAsync();
                    break;
                case ID_TRAY_EXIT:
                    AppState::Instance().quit_requested = true;
                    DestroyWindow(hWnd);
                    break;
            }
            return 0;

        case WM_CLOSE:
            if (HotkeyManager::Instance().IsRecording()) HotkeyManager::Instance().StopRecording();
            if (AppState::Instance().minimize_to_tray) {
                ShowWindow(hWnd, SW_HIDE);
                return 0;
            }
            DestroyWindow(hWnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

bool UIManager::CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
                                                featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
                                                &m_pSwapChain, &m_pd3dDevice, &featureLevel, &m_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED) {
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags,
                                            featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
                                            &m_pSwapChain, &m_pd3dDevice, &featureLevel, &m_pd3dDeviceContext);
    }
    if (res != S_OK) return false;

    CreateRenderTarget();
    return true;
}

void UIManager::CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (m_pSwapChain) { m_pSwapChain->Release(); m_pSwapChain = nullptr; }
    if (m_pd3dDeviceContext) { m_pd3dDeviceContext->Release(); m_pd3dDeviceContext = nullptr; }
    if (m_pd3dDevice) { m_pd3dDevice->Release(); m_pd3dDevice = nullptr; }
}

void UIManager::CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    m_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    m_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &m_mainRenderTargetView);
    pBackBuffer->Release();
}

void UIManager::CleanupRenderTarget() {
    if (m_mainRenderTargetView) {
        m_mainRenderTargetView->Release();
        m_mainRenderTargetView = nullptr;
    }
}

void UIManager::SetupTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    style.WindowRounding = 6.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.FramePadding = ImVec2(6.0f, 4.0f);

    colors[ImGuiCol_Text]                  = ImVec4(0.92f, 0.94f, 0.98f, 1.00f);
    colors[ImGuiCol_TextDisabled]          = ImVec4(0.50f, 0.55f, 0.65f, 1.00f);
    colors[ImGuiCol_WindowBg]              = ImVec4(0.08f, 0.10f, 0.14f, 1.00f);
    colors[ImGuiCol_ChildBg]               = ImVec4(0.10f, 0.12f, 0.17f, 0.90f);
    colors[ImGuiCol_PopupBg]               = ImVec4(0.09f, 0.11f, 0.16f, 0.98f);
    colors[ImGuiCol_Border]                = ImVec4(0.20f, 0.25f, 0.35f, 0.50f);
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]               = ImVec4(0.14f, 0.17f, 0.24f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.20f, 0.25f, 0.35f, 1.00f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.24f, 0.30f, 0.42f, 1.00f);
    colors[ImGuiCol_TitleBg]               = ImVec4(0.08f, 0.10f, 0.14f, 1.00f);
    colors[ImGuiCol_TitleBgActive]         = ImVec4(0.12f, 0.15f, 0.22f, 1.00f);
    colors[ImGuiCol_MenuBarBg]             = ImVec4(0.10f, 0.12f, 0.17f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.06f, 0.08f, 0.11f, 0.60f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.22f, 0.28f, 0.38f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.30f, 0.38f, 0.50f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.40f, 0.50f, 0.65f, 1.00f);
    colors[ImGuiCol_CheckMark]             = ImVec4(0.00f, 0.65f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab]            = ImVec4(0.00f, 0.65f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.20f, 0.75f, 1.00f, 1.00f);
    colors[ImGuiCol_Button]                = ImVec4(0.15f, 0.20f, 0.30f, 1.00f);
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.22f, 0.30f, 0.45f, 1.00f);
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.00f, 0.55f, 0.95f, 1.00f);
    colors[ImGuiCol_Header]                = ImVec4(0.16f, 0.22f, 0.32f, 1.00f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.24f, 0.32f, 0.46f, 1.00f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.00f, 0.55f, 0.95f, 1.00f);
    colors[ImGuiCol_Separator]             = ImVec4(0.20f, 0.25f, 0.35f, 0.50f);
    colors[ImGuiCol_TableHeaderBg]         = ImVec4(0.12f, 0.15f, 0.22f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]     = ImVec4(0.20f, 0.25f, 0.35f, 0.60f);
    colors[ImGuiCol_TableBorderLight]      = ImVec4(0.16f, 0.20f, 0.28f, 0.40f);
    colors[ImGuiCol_TableRowBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(0.10f, 0.12f, 0.18f, 0.50f);

    if (m_dpi_scale > 1.0f) {
        style.ScaleAllSizes(m_dpi_scale);
    }
}

void UIManager::OnDpiChanged(float new_dpi_scale) {
    if (new_dpi_scale <= 0.0f) new_dpi_scale = 1.0f;
    float prev_scale = m_dpi_scale;
    m_dpi_scale = new_dpi_scale;

    ImGuiStyle& style = ImGui::GetStyle();
    if (prev_scale > 0.0f) {
        style.ScaleAllSizes(new_dpi_scale / prev_scale);
    }

    ImGuiIO& io = ImGui::GetIO();
    if (m_initial_dpi_scale > 0.0f) {
        io.FontGlobalScale = new_dpi_scale / m_initial_dpi_scale;
    }
}

void UIManager::LoadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    // Try to load Microsoft YaHei or SimHei from Windows fonts directory
    const char* font_paths[] = {
        "C:\\Windows\\Fonts\\msyh.ttc",
        "C:\\Windows\\Fonts\\simhei.ttf",
        "C:\\Windows\\Fonts\\msyhl.ttc",
        "C:\\Windows\\Fonts\\segoeui.ttf",
    };

    float font_size = 16.0f * (m_dpi_scale > 0.0f ? m_dpi_scale : 1.0f);

    ImFont* font = nullptr;
    for (const char* p : font_paths) {
        if (GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES) {
            font = io.Fonts->AddFontFromFileTTF(p, font_size, nullptr, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
            if (font) break;
        }
    }

    if (!font) {
        io.Fonts->AddFontDefault();
    }
}

void UIManager::CreateTrayIcon() {
    ZeroMemory(&m_nid, sizeof(m_nid));
    m_nid.cbSize = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd = m_hwnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAYICON;
    m_nid.hIcon = LoadIconW(m_wc.hInstance, MAKEINTRESOURCEW(1));
    if (!m_nid.hIcon) {
        m_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    wcsncpy_s(m_nid.szTip, L"SC2 Co-op Native", _TRUNCATE);
    Shell_NotifyIconW(NIM_ADD, &m_nid);
    m_tray_active = true;
}

void UIManager::RemoveTrayIcon() {
    if (m_tray_active) {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_tray_active = false;
    }
}

void UIManager::PostNewMatch(const ReplayRecord& rec, bool is_best_time) {
    {
        std::lock_guard<std::mutex> lock(m_match_mutex);
        m_pending_matches.push_back({rec, is_best_time});
    }
    if (m_hwnd) {
        if (!PostMessageW(m_hwnd, WM_USER_NEW_MATCH, 0, 0)) {
            LogDiagnostic("ui", "PostMessage failed; error=" + std::to_string(GetLastError()));
        }
    }
}

void UIManager::ProcessPendingMatches() {
    std::vector<PendingMatch> matches;
    {
        std::lock_guard<std::mutex> lock(m_match_mutex);
        matches = std::move(m_pending_matches);
    }
    for (const auto& m : matches) {
        LogDiagnostic("ui", std::string("processing match; auto_show_summary=") +
            (AppState::Instance().auto_show_summary ? "1" : "0"));
        if (AppState::Instance().auto_show_summary) {
            SummaryOverlay::Instance().ShowMatch(m.record, m.is_best_time, (float)AppState::Instance().hud_duration_seconds);
        }
        bool p2_is_me = (!m.record.p2.handle.empty() && m.record.file_path.find(m.record.p2.handle) != std::string::npos) ||
                        (!m.record.p2.handle.empty() && AppState::Instance().HasUserHandle(m.record.p2.handle));
        std::string ally_name = p2_is_me ? m.record.p1.name : m.record.p2.name;
        if (AppState::Instance().show_ally_hud && !ally_name.empty()) {
            AllyToastWindow::Instance().ShowAllyInfo(ally_name, (float)AppState::Instance().hud_duration_seconds);
        }
    }
}

void UIManager::ShowTrayMenu() {
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    InsertMenuW(hMenu, 0, MF_BYPOSITION | MF_STRING, ID_TRAY_SHOW, Win32Utils::Utf8ToWide(TR("Open SC2 Co-op Native")).c_str());
    InsertMenuW(hMenu, 1, MF_BYPOSITION | MF_STRING, ID_TRAY_SCAN, Win32Utils::Utf8ToWide(TR("Rescan Replays Now")).c_str());
    InsertMenuW(hMenu, 2, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    InsertMenuW(hMenu, 3, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT, Win32Utils::Utf8ToWide(TR("Exit")).c_str());

    SetForegroundWindow(m_hwnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(hMenu);
}

bool UIManager::Init(HINSTANCE hInstance, int nCmdShow) {
    m_wc = { sizeof(m_wc), CS_CLASSDC, WndProc, 0L, 0L, hInstance,
             LoadIconW(hInstance, MAKEINTRESOURCEW(1)), nullptr, nullptr, nullptr,
             L"SC2CoopNativeClass", nullptr };
    RegisterClassExW(&m_wc);

    HMONITOR hPrimaryMon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    float dpi_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(hPrimaryMon);
    if (dpi_scale <= 0.0f) dpi_scale = 1.0f;
    m_dpi_scale = dpi_scale;
    m_initial_dpi_scale = dpi_scale;

    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(hPrimaryMon, &mi);
    int mon_w = mi.rcWork.right - mi.rcWork.left;
    int mon_h = mi.rcWork.bottom - mi.rcWork.top;

    int win_w = (int)(1380 * dpi_scale);
    int win_h = (int)(860 * dpi_scale);
    if (win_w > (int)(mon_w * 0.95f)) win_w = (int)(mon_w * 0.95f);
    if (win_h > (int)(mon_h * 0.95f)) win_h = (int)(mon_h * 0.95f);

    int win_x = mi.rcWork.left + (mon_w - win_w) / 2;
    int win_y = mi.rcWork.top + (mon_h - win_h) / 2;

    m_hwnd = CreateWindowW(m_wc.lpszClassName, L"StarCraft II Co-op Native v1.0.19",
                           WS_OVERLAPPEDWINDOW, win_x, win_y, win_w, win_h,
                           nullptr, nullptr, m_wc.hInstance, nullptr);

    if (!CreateDeviceD3D(m_hwnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(m_wc.lpszClassName, m_wc.hInstance);
        return false;
    }

    CreateTrayIcon();

    if (AppState::Instance().start_minimized) {
        ShowWindow(m_hwnd, SW_HIDE);
    } else {
        ShowWindow(m_hwnd, nCmdShow);
        UpdateWindow(m_hwnd);
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    SetupTheme();
    LoadFonts();

    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplDX11_Init(m_pd3dDevice, m_pd3dDeviceContext);

    TextureLoader::Instance().Init(m_pd3dDevice);

    // Initialize HUD Overlay and Hotkey subsystems
    SummaryOverlay::Instance().Init(hInstance);
    AllyToastWindow::Instance().Init(hInstance);
    HotkeyManager::Instance().Init(m_hwnd);

    // Configure hotkey callbacks
    auto& hk = HotkeyManager::Instance();
    hk.SetCallback(HotkeyAction::ToggleSummary, []() {
        SummaryOverlay::Instance().Toggle();
    });
    hk.SetCallback(HotkeyAction::PrevMatch, []() {
        SummaryOverlay::Instance().ShowPrevious();
    });
    hk.SetCallback(HotkeyAction::NextMatch, []() {
        SummaryOverlay::Instance().ShowNext();
    });
    hk.SetCallback(HotkeyAction::AllyInfo, []() {
        AllyToastWindow::Instance().Toggle();
    });

    // Start background real-time replay watcher for automated post-game popups
    auto& watcher = ReplayWatcher::Instance();
    watcher.SetOnNewMatchCallback([this](const ReplayRecord& rec, bool is_best_time) {
        PostNewMatch(rec, is_best_time);
    });
    watcher.Start();
    LogDiagnostic("ui", std::string("started build ") + __DATE__ + " " + __TIME__);

    return true;
}

void UIManager::Shutdown() {
    ReplayWatcher::Instance().Stop();
    HotkeyManager::Instance().Shutdown();
    AllyToastWindow::Instance().Shutdown();
    SummaryOverlay::Instance().Shutdown();

    RemoveTrayIcon();

    TextureLoader::Instance().Shutdown();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(m_hwnd);
    UnregisterClassW(m_wc.lpszClassName, m_wc.hInstance);
}

void UIManager::Run() {
    auto& state = AppState::Instance();

    MSG msg;
    ZeroMemory(&msg, sizeof(msg));

    auto last_frame_time = std::chrono::high_resolution_clock::now();

    while (msg.message != WM_QUIT && !state.quit_requested) {
        if (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            continue;
        }

        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - last_frame_time).count();
        last_frame_time = now;

        // Update HUD and Overlay fade timers
        ProcessPendingMatches();
        SummaryOverlay::Instance().Update(dt);
        AllyToastWindow::Instance().Update(dt);

        if (m_minimized || (m_hwnd && !IsWindowVisible(m_hwnd))) {
            MsgWaitForMultipleObjectsEx(0, nullptr, 25, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            continue;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        FrameRenderer::RenderFrame();

        // Rendering D3D11
        ImGui::Render();
        const float clear_color[4] = { 0.08f, 0.10f, 0.14f, 1.00f };
        m_pd3dDeviceContext->OMSetRenderTargets(1, &m_mainRenderTargetView, nullptr);
        m_pd3dDeviceContext->ClearRenderTargetView(m_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        m_pSwapChain->Present(1, 0); // VSync 60fps
    }
}

} // namespace sc2
