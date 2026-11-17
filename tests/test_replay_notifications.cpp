#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <windows.h>
#include <StormLib.h>

#include "../src/app_state.h"
#include "../src/db/database.h"
#include "../src/replay/replay_parser.h"
#include "../src/replay/replay_watcher.h"
#include "../src/ui/ui_manager.h"
#include "../src/ui/hud/summary_overlay.h"
#include "../src/utils/win32_utils.h"

using namespace std::chrono_literals;

void CreateArcadeReplay(const std::filesystem::path& path) {
    // A parsable MPQ without co-op commander data, like the historical arcade
    // files that caused the production watcher to re-import forever.
    const std::string metadata = R"({"Title":"Arcade Example","Duration":10,"Players":[{"Result":"Win"}]})";
    HANDLE archive = nullptr;
    HANDLE file = nullptr;
    assert(SFileCreateArchive(path.c_str(), MPQ_CREATE_ARCHIVE_V1, 16, &archive));
    assert(SFileCreateFile(archive, "replay.gamemetadata.json", 0,
        static_cast<DWORD>(metadata.size()), 0, MPQ_FILE_COMPRESS, &file));
    assert(SFileWriteFile(file, metadata.data(), static_cast<DWORD>(metadata.size()), MPQ_COMPRESSION_ZLIB));
    assert(SFileFinishFile(file));
    assert(SFileCloseArchive(archive));
}

// Pump the real UI queue, including WM_USER_NEW_MATCH, while the main window is
// hidden. Calling ShowMatch directly would miss the scanner/watcher race.
template<class Predicate>
bool WaitUntil(Predicate predicate, std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        sc2::SummaryOverlay::Instance().Update(0.02f);
        if (predicate()) return true;
        std::this_thread::sleep_for(20ms);
    }
    return predicate();
}

int main() {
    auto root = std::filesystem::temp_directory_path() /
        (L"sc2-notifications-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root / L"first" / L"Replays");
    std::filesystem::create_directories(root / L"second" / L"Replays");
    auto& db = sc2::Database::Instance();
    assert(db.Open(sc2::Win32Utils::WideToUtf8((root / L"test.db").wstring())));
    auto& state = sc2::AppState::Instance();
    state.SetAccountFolder(sc2::Win32Utils::WideToUtf8((root / L"first").wstring()));
    state.start_minimized = true;
    state.auto_show_summary = true;
    state.show_ally_hud = false;
    state.hud_duration_seconds = 60;
    state.enable_logging = false;

    auto fixture = std::filesystem::path(L"tests/fixtures/sample.SC2Replay");
    auto indexed = root / L"first" / L"Replays" / L"indexed.SC2Replay";
    std::filesystem::copy_file(fixture, indexed);
    std::filesystem::last_write_time(indexed, std::filesystem::file_time_type::clock::now());
    auto parsed = sc2::ReplayParser::ParseFile(indexed.wstring());
    assert(parsed && parsed->result != "Unknown");
    assert(db.SaveReplay(*parsed)); // Simulate the scanner winning the race.

    auto& ui = sc2::UIManager::Instance();
    assert(ui.Init(GetModuleHandleW(nullptr), SW_HIDE));
    auto& overlay = sc2::SummaryOverlay::Instance();
    HWND hwnd = FindWindowW(L"SC2SummaryOverlayClass", nullptr);
    assert(WaitUntil([&] { return overlay.IsActive(); }, 10s));
    assert(!IsWindowVisible(ui.GetHwnd()));
    assert(IsWindowVisible(hwnd));
    RECT rect;
    GetWindowRect(hwnd, &rect);
    assert(WaitUntil([&] {
        GetWindowRect(hwnd, &rect);
        return rect.left < GetSystemMetrics(SM_CXVIRTUALSCREEN);
    }, 2s));
    std::cout << "[OK] Indexed replay automatically displays via the hidden main window's UI queue.\n";
    overlay.Hide();
    assert(WaitUntil([&] { return !overlay.IsActive(); }, 2s));
    assert(!WaitUntil([&] { return overlay.IsActive(); }, 2500ms));
    std::cout << "[OK] An unchanged replay does not trigger a duplicate popup.\n";

    auto partial = root / L"first" / L"Replays" / L"partial.SC2Replay";
    { std::ofstream file(partial, std::ios::binary); file << "still writing"; }
    WaitUntil([] { return false; }, 2500ms);
    std::filesystem::copy_file(fixture, partial, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::last_write_time(partial, std::filesystem::file_time_type::clock::now());
    assert(WaitUntil([&] { return overlay.IsActive(); }, 6s));
    std::cout << "[OK] A replay that finishes writing after detection is retried and displayed.\n";
    overlay.Hide();
    assert(WaitUntil([&] { return !overlay.IsActive(); }, 2s));

    state.SetAccountFolder(sc2::Win32Utils::WideToUtf8((root / L"second").wstring()));
    auto moved = root / L"second" / L"Replays" / L"new.SC2Replay";
    std::filesystem::copy_file(fixture, moved);
    std::filesystem::last_write_time(moved, std::filesystem::file_time_type::clock::now());
    assert(WaitUntil([&] { return overlay.IsActive(); }, 6s));
    std::cout << "[OK] Changing the configured folder immediately watches its replays.\n";

    overlay.Hide();
    assert(WaitUntil([&] { return !overlay.IsActive(); }, 2s));
    auto backlog = root / L"backlog" / L"Replays";
    std::filesystem::create_directories(backlog);
    // The production library contains hundreds of arcade replays that parse
    // successfully but never satisfy the co-op-specific HasValidReplay filter.
    // Include unreadable historical files too: new games must bypass both.
    for (int i = 0; i < 40; ++i) {
        auto old = backlog / (L"old-" + std::to_wstring(i) + L".SC2Replay");
        if (i % 2 == 0) {
            CreateArcadeReplay(old);
        } else {
            std::ofstream file(old, std::ios::binary);
            file << "unreadable historical replay";
        }
        std::filesystem::last_write_time(old,
            std::filesystem::file_time_type::clock::now() - 24h);
        if (i == 0) {
            auto legacy = sc2::ReplayParser::ParseFile(old.wstring());
            assert(legacy && db.SaveReplay(*legacy)); // Existing unknown commander, no hash yet.
        }
    }
    auto live = backlog / L"live.SC2Replay";
    std::filesystem::copy_file(fixture, live);
    std::filesystem::last_write_time(live, std::filesystem::file_time_type::clock::now());
    state.SetAccountFolder(sc2::Win32Utils::WideToUtf8((root / L"backlog").wstring()));
    const auto live_path = sc2::Win32Utils::WideToUtf8(live.wstring());
    assert(WaitUntil([&] { return db.HasReplay(live_path) && overlay.IsActive(); }, 5s));
    std::cout << "[OK] A new game is indexed and displayed within 5s despite a historical backlog.\n";

    // A new game arriving DURING background work must also get a prompt scan.
    overlay.Hide();
    assert(WaitUntil([&] { return !overlay.IsActive(); }, 2s));
    auto next_live = backlog / L"next-live.SC2Replay";
    std::filesystem::copy_file(fixture, next_live);
    std::filesystem::last_write_time(next_live, std::filesystem::file_time_type::clock::now());
    const auto next_path = sc2::Win32Utils::WideToUtf8(next_live.wstring());
    assert(WaitUntil([&] { return db.HasReplay(next_path) && overlay.IsActive(); }, 5s));
    std::cout << "[OK] A game arriving during background work is also indexed and displayed within 5s.\n";

    assert(WaitUntil([&] { return db.GetReplayFileStates().size() >= 45; }, 8s));
    const auto states = db.GetReplayFileStates();
    const auto bad_path = sc2::Win32Utils::WideToUtf8((backlog / L"old-1.SC2Replay").wstring());
    const auto arcade_path = sc2::Win32Utils::WideToUtf8((backlog / L"old-0.SC2Replay").wstring());
    assert(states.at(bad_path).outcome == "failed");
    assert(states.at(bad_path).sha256.size() == 64);
    assert(states.at(arcade_path).outcome == "parsed");
    assert(db.HasReplay(arcade_path));
    assert(!db.HasValidReplay(arcade_path));
    const auto revision = db.GetRevision();
    WaitUntil([] { return false; }, 2500ms);
    assert(db.GetRevision() == revision);
    std::cout << "[OK] Failed content has a SHA-256 marker; parsed arcade games are not repeatedly imported.\n";

    ui.Shutdown();
    db.Close();
    std::filesystem::remove_all(root);
    std::cout << "All automatic replay notification tests passed.\n";
}
