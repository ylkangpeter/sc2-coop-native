#pragma once

#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <string>
#include <vector>
#include <mutex>
#include "../replay/replay_types.h"

#define WM_TRAYICON (WM_USER + 100)
#define WM_USER_NEW_MATCH (WM_USER + 201)
#define ID_TRAY_SHOW 1001
#define ID_TRAY_SCAN 1002
#define ID_TRAY_EXIT 1003

namespace sc2 {

class UIManager {
public:
    static UIManager& Instance();

    bool Init(HINSTANCE hInstance, int nCmdShow);
    void Run();
    void Shutdown();

    HWND GetHwnd() const { return m_hwnd; }
    ID3D11Device* GetDevice() const { return m_pd3dDevice; }

    void CreateTrayIcon();
    void RemoveTrayIcon();
    void ShowTrayMenu();

    void PostNewMatch(const ReplayRecord& rec, bool is_best_time);
    void ProcessPendingMatches();

private:
    UIManager() = default;
    ~UIManager() = default;

    bool CreateDeviceD3D(HWND hWnd);
    void CleanupDeviceD3D();
    void CreateRenderTarget();
    void CleanupRenderTarget();
    void SetupTheme();
    void LoadFonts();

    static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd = nullptr;
    WNDCLASSEXW m_wc = {};
    ID3D11Device* m_pd3dDevice = nullptr;
    ID3D11DeviceContext* m_pd3dDeviceContext = nullptr;
    IDXGISwapChain* m_pSwapChain = nullptr;
    ID3D11RenderTargetView* m_mainRenderTargetView = nullptr;
    bool m_minimized = false;

    NOTIFYICONDATAW m_nid = {};
    bool m_tray_active = false;

    struct PendingMatch {
        ReplayRecord record;
        bool is_best_time = false;
    };
    std::mutex m_match_mutex;
    std::vector<PendingMatch> m_pending_matches;
};

} // namespace sc2
