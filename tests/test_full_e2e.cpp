#include <iostream>
#include <cassert>
#include <windows.h>
#include "../src/db/database.h"
#include "../src/app_state.h"
#include "../src/ui/hud/summary_overlay.h"
#include "../src/ui/hud/ally_toast.h"
#include "../src/ui/modals/modal_chat_view.h"

int main() {
    std::cout << "========================================\n";
    std::cout << "TEST: End-to-End Database & Overlay Test\n";
    std::cout << "========================================\n";

    // 1. Open production database
    bool ok = sc2::Database::Instance().Open("dist/sc2_coop/coop_data.db");
    assert(ok);
    int total = sc2::Database::Instance().GetTotalReplayCount();
    std::cout << "[OK] Opened coop_data.db with " << total << " total replays.\n";
    assert(total > 0);

    // 2. Query games and check chat
    int q_total = 0;
    auto replays = sc2::Database::Instance().QueryReplays("", 50, 0, q_total);
    assert(!replays.empty());

    int replays_with_chat = 0;
    int total_chat_msgs = 0;
    for (const auto& r : replays) {
        if (!r.chat.empty()) {
            replays_with_chat++;
            total_chat_msgs += r.chat.size();
        }
    }
    std::cout << "[OK] Queried " << replays.size() << " recent replays: "
              << replays_with_chat << " have chat (" << total_chat_msgs << " messages total).\n";

    // Print first game with chat
    for (const auto& r : replays) {
        if (!r.chat.empty()) {
            std::cout << "Sample Chat for [" << r.map_name << "] (" << r.game_date << "):\n";
            for (const auto& msg : r.chat) {
                int mm = static_cast<int>(msg.time_seconds) / 60;
                int ss = static_cast<int>(msg.time_seconds) % 60;
                std::cout << "  [" << mm << ":" << ss << "] " << msg.player_name << ": " << msg.text << "\n";
            }
            break;
        }
    }

    // 3. Test Overlay interactions & stability
    HINSTANCE hInstance = GetModuleHandleW(nullptr);
    auto& overlay = sc2::SummaryOverlay::Instance();
    overlay.Init(hInstance);
    overlay.ShowLatest();
    assert(overlay.IsActive());

    HWND hwnd = FindWindowW(L"SC2SummaryOverlayClass", nullptr);
    assert(hwnd != nullptr);
    std::cout << "[OK] SummaryOverlay visible with hwnd: " << hwnd << "\n";

    // Test dragging (clicking at center 370, 100)
    SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(370, 100));
    SendMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(400, 120));
    SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(400, 120));
    assert(overlay.IsActive());
    std::cout << "[OK] Dragging overlay succeeded without crash or dismissal.\n";

    // Test Pinning button (570, 200)
    SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(570, 200));
    SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(570, 200));
    assert(overlay.IsActive());
    std::cout << "[OK] Pin button toggled.\n";

    // Test Previous match button (80, 200)
    SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(80, 200));
    SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(80, 200));
    assert(overlay.IsActive());
    std::cout << "[OK] Older Match button clicked.\n";

    // Test Next match button (200, 200)
    SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(200, 200));
    SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(200, 200));
    assert(overlay.IsActive());
    std::cout << "[OK] Newer Match button clicked.\n";

    // Test Close button (670, 200)
    SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(670, 200));
    SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(670, 200));
    assert(!overlay.IsActive());
    std::cout << "[OK] Close button closed overlay cleanly.\n";

    overlay.Shutdown();
    sc2::Database::Instance().Close();

    std::cout << "\n>>> ALL END-TO-END VERIFICATION TESTS PASSED SUCCESSFULLY! <<<\n";
    return 0;
}
