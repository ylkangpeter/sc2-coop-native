#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
#include <windows.h>
#include "../src/app_state.h"
#include "../src/db/database.h"
#include "../src/utils/hotkey_manager.h"
#include "../src/utils/win32_utils.h"
#include "../src/ui/ui_manager.h"
#include "../src/ui/hud/summary_overlay.h"
#include "../src/ui/hud/ally_toast.h"

using namespace std::chrono_literals;

static int position_calls = 0;
static int z_order_calls = 0;
static WNDPROC original_overlay_proc = nullptr;

static LRESULT CALLBACK ObservePosition(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_WINDOWPOSCHANGING) {
        ++position_calls;
        if (!(reinterpret_cast<WINDOWPOS*>(lp)->flags & SWP_NOZORDER)) ++z_order_calls;
    }
    return CallWindowProcW(original_overlay_proc, hwnd, msg, wp, lp);
}

static LRESULT CALLBACK TestWindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto& hk = sc2::HotkeyManager::Instance();
    switch (msg) {
    case WM_HOTKEY: hk.OnHotkey(static_cast<int>(wp)); return 0;
    case WM_USER_HOTKEY_RECORDED:
        hk.ApplyRecordedHotkey(static_cast<int>(wp), LOWORD(lp), HIWORD(lp)); return 0;
    case WM_USER_HOTKEY_RECORD_CANCELLED: hk.StopRecording(); return 0;
    case WM_USER_HOTKEY_ACTION: hk.DispatchAction(static_cast<sc2::HotkeyAction>(wp)); return 0;
    case WM_USER_DISMISS_OVERLAYS:
        sc2::SummaryOverlay::Instance().Hide(); sc2::AllyToastWindow::Instance().Hide(); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

template<class Predicate>
static bool WaitUntil(Predicate predicate, std::chrono::milliseconds timeout = 2s) {
    const auto end = std::chrono::steady_clock::now() + timeout;
    do {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (predicate()) return true;
        std::this_thread::sleep_for(5ms);
    } while (std::chrono::steady_clock::now() < end);
    return predicate();
}

static void PressFunctionKey(WORD vk) {
    INPUT keys[2]{};
    for (auto& key : keys) { key.type = INPUT_KEYBOARD; key.ki.wVk = vk; }
    keys[1].ki.dwFlags = KEYEVENTF_KEYUP;
    assert(SendInput(2, keys, sizeof(INPUT)) == 2);
}

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        (L"sc2-input-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    auto& db = sc2::Database::Instance();
    assert(db.Open(sc2::Win32Utils::WideToUtf8((root / L"test.db").wstring())));
    auto& state = sc2::AppState::Instance();
    state.enable_logging = false;
    state.hotkey_summary_mod = 0;
    state.hotkey_summary_vk = VK_F20;
    state.hotkey_ally_enabled = false;
    state.hotkey_nav_enabled = false;
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW cls{};
    cls.hInstance = instance;
    cls.lpfnWndProc = TestWindowProc;
    cls.lpszClassName = L"SC2InputRegressionTest";
    assert(RegisterClassW(&cls));
    HWND window = CreateWindowW(cls.lpszClassName, L"test", 0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, instance, nullptr);
    assert(window);
    auto& hk = sc2::HotkeyManager::Instance();
    int global_actions = 0;
    int older = 0;
    int newer = 0;
    hk.SetCallback(sc2::HotkeyAction::ToggleSummary, [&] { ++global_actions; });
    hk.SetCallback(sc2::HotkeyAction::PrevMatch, [&] { ++older; });
    hk.SetCallback(sc2::HotkeyAction::NextMatch, [&] { ++newer; });
    hk.Init(window);
    assert(!hk.IsHookActive() && !hk.IsHotkeyUnavailable(1));
    PressFunctionKey(VK_F20);
    assert(WaitUntil([&] { return global_actions == 1; }));
    assert(!hk.IsHookActive());
    std::cout << "[OK] Real WM_HOTKEY dispatch works with no low-level keyboard hook.\n";

    hk.StartRecording(1);
    assert(hk.IsRecording() && hk.IsHookActive());
    PressFunctionKey(VK_F21);
    assert(WaitUntil([&] { return !hk.IsRecording() && !hk.IsHookActive() && state.hotkey_summary_vk == VK_F21; }));
    WaitUntil([] { return false; }, 250ms);
    PressFunctionKey(VK_F21);
    assert(WaitUntil([&] { return global_actions == 2; }));
    hk.StartRecording(1);
    assert(hk.HandleLowLevelKey(VK_ESCAPE, false, true));
    assert(WaitUntil([&] { return !hk.IsHookActive(); }));
    assert(state.hotkey_summary_vk == VK_F21);
    std::cout << "[OK] Real hook recording, queued application and Escape cancellation release the temporary hook.\n";

    auto& summary = sc2::SummaryOverlay::Instance();
    auto& ally = sc2::AllyToastWindow::Instance();
    summary.Init(instance);
    ally.Init(instance);
    original_overlay_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(summary.GetHwnd(), GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(ObservePosition)));
    assert(original_overlay_proc);
    sc2::ReplayRecord record;
    record.result = "Victory";
    record.map_name = "Void Thrashing";
    summary.ShowMatch(record, false, 60.0f);
    assert(hk.IsHookActive());
    z_order_calls = 0;
    for (int i = 0; i < 30; ++i) summary.Update(1.0f / 60.0f);
    assert(z_order_calls == 0); // Position animation retains the existing Z order.
    position_calls = 0;
    for (int i = 0; i < 600; ++i) summary.Update(1.0f / 60.0f);
    assert(position_calls == 0 && z_order_calls == 0);
    std::cout << "[OK] Animation preserves Z order; 600 stable display updates make no SetWindowPos calls.\n";

    SetWindowPos(summary.GetHwnd(), HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    summary.Update(1.1f);
    assert(GetWindowLongPtrW(summary.GetHwnd(), GWL_EXSTYLE) & WS_EX_TOPMOST);
    WaitUntil([] { return false; }, 50ms); // Drain any earlier foreground notifications.
    const auto before_event = z_order_calls;
    NotifyWinEvent(EVENT_SYSTEM_FOREGROUND, GetDesktopWindow(), OBJID_WINDOW, CHILDID_SELF);
    assert(WaitUntil([&] { return z_order_calls > before_event; }));
    std::cout << "[OK] Lost-topmost recovery and real asynchronous foreground notification delivery work.\n";

    state.hotkey_nav_enabled = true;
    assert(!hk.HandleLowLevelKey('A', false, true));
    assert(hk.HandleLowLevelKey(VK_NEXT, false, true));
    assert(WaitUntil([&] { return older == 1; }));
    assert(hk.HandleLowLevelKey(VK_PRIOR, false, true));
    assert(WaitUntil([&] { return newer == 1; }));
    state.hotkey_summary_vk = VK_NEXT;
    hk.RegisterAll();
    assert(!hk.IsHotkeyUnavailable(1));
    assert(!hk.HandleLowLevelKey(VK_NEXT, false, true)); // Preserve explicit assignment.
    state.hotkey_summary_vk = VK_F21;
    hk.RegisterAll();
    ally.ShowTestToast();
    summary.Hide();
    summary.Update(1.0f);
    assert(!summary.IsActive() && hk.IsHookActive()); // Ally toast still needs Escape.
    assert(!hk.HandleLowLevelKey(VK_ESCAPE, false, true)); // Not consumed for the game.
    assert(WaitUntil([&] { return !ally.IsActive() && !hk.IsHookActive(); }));
    assert(!hk.HandleLowLevelKey(VK_NEXT, false, true));
    std::cout << "[OK] Page navigation and Escape survive; the last closing overlay removes the hook.\n";

    HWND blocker = CreateWindowW(L"STATIC", L"blocker", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    hk.UnregisterAll();
    assert(RegisterHotKey(blocker, 9001, 0, VK_F21));
    hk.RegisterAll();
    assert(hk.IsHotkeyUnavailable(1) && !hk.IsHookActive());
    UnregisterHotKey(blocker, 9001);
    hk.RegisterAll();
    assert(!hk.IsHotkeyUnavailable(1));
    hk.StartRecording(1);
    hk.Shutdown();
    summary.ShowMatch(record);
    assert(!hk.IsHookActive()); // Overlay callbacks after shutdown must not reinstall hooks.
    SetWindowLongPtrW(summary.GetHwnd(), GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_overlay_proc));
    summary.Shutdown();
    ally.Shutdown();
    DestroyWindow(blocker);
    DestroyWindow(window);
    UnregisterClassW(cls.lpszClassName, instance);
    db.Close();
    std::filesystem::remove_all(root);
    std::cout << "[OK] Shortcut conflicts are reported; shutdown removes recording and overlay hooks.\n";
}
