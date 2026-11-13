#include <iostream>
#include <windows.h>
#include "../src/ui/hud/summary_overlay.h"
#include "../src/db/database.h"
#include "../src/app_state.h"

int main() {
    std::cout << "Testing overlay click...\n";
    HINSTANCE hInstance = GetModuleHandleW(nullptr);

    sc2::Database::Instance().Open("test_click.db");

    sc2::ReplayRecord r;
    r.file_path = "C:\\test.SC2Replay";
    r.map_name = "Dead of Night";
    r.result = "Victory";
    r.p1.name = "P1";
    r.p2.name = "P2";
    sc2::Database::Instance().SaveReplay(r);

    auto& overlay = sc2::SummaryOverlay::Instance();
    overlay.Init(hInstance);
    overlay.ShowLatest();

    HWND hwnd = FindWindowW(L"SC2SummaryOverlayClass", nullptr);
    std::cout << "Found hwnd: " << hwnd << "\n";

    if (hwnd) {
        // Send click at center of window (300, 100)
        std::cout << "Sending WM_LBUTTONDOWN at (300, 100)...\n";
        SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(300, 100));
        SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(300, 100));

        // Send click on Prev button (50, 200)
        std::cout << "Sending WM_LBUTTONDOWN at (50, 200) [Prev button]...\n";
        SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(50, 200));
        SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(50, 200));

        // Send click on Pin button (550, 200)
        std::cout << "Sending WM_LBUTTONDOWN at (550, 200) [Pin button]...\n";
        SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(550, 200));
        SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(550, 200));

        // Send click on Close button (650, 200)
        std::cout << "Sending WM_LBUTTONDOWN at (650, 200) [Close button]...\n";
        SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(650, 200));
        SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(650, 200));
    }

    overlay.Shutdown();
    sc2::Database::Instance().Close();
    std::remove("test_click.db");
    std::cout << "Overlay click test finished successfully!\n";
    return 0;
}
