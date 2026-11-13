#include <iostream>
#include <cassert>
#include "../src/ui/hud/summary_overlay.h"
#include "../src/ui/hud/ally_toast.h"
#include "../src/utils/hotkey_manager.h"
#include "../src/replay/replay_watcher.h"
#include "../src/db/database.h"
#include "../src/app_state.h"

int main() {
    std::cout << "========================================\n";
    std::cout << "TEST: HUD Overlay, Hotkeys & Watcher\n";
    std::cout << "========================================\n";

    HINSTANCE hInstance = GetModuleHandleW(nullptr);

    // 1. Initialize Database
    std::remove("test_hud.db");
    bool db_ok = sc2::Database::Instance().Open("test_hud.db");
    assert(db_ok);

    // Insert mock matches
    sc2::ReplayRecord r1;
    r1.file_path = "C:\\Replays\\game1.SC2Replay";
    r1.map_name = "Dead of Night";
    r1.result = "Victory";
    r1.difficulty = "Brutal+1";
    r1.form_length = "18:49";
    r1.duration_seconds = 1129;
    r1.p1.name = "MyPlayer";
    r1.p1.commander = "Stukov";
    r1.p1.prestige = 0;
    r1.p1.apm = 145;
    r1.p1.kills = 1200;
    r1.p2.name = "AllyPro";
    r1.p2.commander = "Karax";
    r1.p2.prestige = 0;
    r1.p2.apm = 95;
    r1.p2.kills = 650;
    r1.mutators = { "Speed Freaks", "Mag-nificent" };
    sc2::Database::Instance().SaveReplay(r1);

    sc2::ReplayRecord r2 = r1;
    r2.file_path = "C:\\Replays\\game2.SC2Replay";
    r2.map_name = "Void Thrashing";
    r2.result = "Defeat";
    r2.form_length = "12:10";
    r2.duration_seconds = 730;
    sc2::Database::Instance().SaveReplay(r2);

    std::cout << "[OK] Saved sample matches to database.\n";

    // 2. Test Summary Overlay
    auto& summary = sc2::SummaryOverlay::Instance();
    summary.Init(hInstance);
    std::cout << "[OK] SummaryOverlay initialized.\n";

    summary.ShowMatch(r1, true, 5.0f);
    assert(summary.IsActive());
    std::cout << "[OK] SummaryOverlay::ShowMatch active.\n";

    // Test Navigation
    summary.ShowPrevious();
    assert(summary.IsActive());
    std::cout << "[OK] SummaryOverlay::ShowPrevious works.\n";

    summary.ShowNext();
    assert(summary.IsActive());
    std::cout << "[OK] SummaryOverlay::ShowNext works.\n";

    // Test timer update
    summary.Update(20.0f);
    assert(!summary.IsActive());
    std::cout << "[OK] SummaryOverlay auto-dismissed on timer expiration.\n";

    summary.Shutdown();
    std::cout << "[OK] SummaryOverlay shutdown successfully.\n";

    // 3. Test Ally Toast Window
    auto& toast = sc2::AllyToastWindow::Instance();
    toast.Init(hInstance);
    std::cout << "[OK] AllyToastWindow initialized.\n";

    toast.ShowAllyInfo("AllyPro", 5.0f);
    assert(toast.IsActive());
    std::cout << "[OK] AllyToastWindow::ShowAllyInfo active.\n";

    toast.Update(6.0f);
    assert(!toast.IsActive());
    std::cout << "[OK] AllyToastWindow auto-dismissed on timer expiration.\n";

    toast.Shutdown();
    std::cout << "[OK] AllyToastWindow shutdown successfully.\n";

    // 4. Test Hotkey Manager
    HWND hDummy = CreateWindowExW(0, L"STATIC", L"test", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, nullptr);
    auto& hk = sc2::HotkeyManager::Instance();
    hk.Init(hDummy);

    bool toggle_called = false;
    hk.SetCallback(sc2::HotkeyAction::ToggleSummary, [&]() {
        toggle_called = true;
    });

    hk.OnHotkey(2001); // HOTKEY_SUMMARY_ID
    assert(toggle_called);
    std::cout << "[OK] Hotkey callback successfully triggered.\n";
    hk.Shutdown();
    DestroyWindow(hDummy);

    // 5. Test Replay Watcher lifecycle
    auto& watcher = sc2::ReplayWatcher::Instance();
    bool watcher_cb_bound = false;
    watcher.SetOnNewMatchCallback([&](const sc2::ReplayRecord&, bool) {
        watcher_cb_bound = true;
    });
    watcher.Start();
    assert(watcher.IsRunning());
    std::cout << "[OK] ReplayWatcher started running.\n";
    watcher.Stop();
    assert(!watcher.IsRunning());
    std::cout << "[OK] ReplayWatcher stopped cleanly.\n";

    sc2::Database::Instance().Close();
    std::remove("test_hud.db");

    std::cout << "\n>>> ALL HUD, OVERLAY & HOTKEY TESTS PASSED 100%! <<<\n";
    return 0;
}
