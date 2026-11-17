#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <thread>
#include <windows.h>
#include "../src/app_state.h"
#include "../src/db/database.h"
#include "../src/replay/replay_file_state.h"
#include "../src/replay/replay_scanner.h"
#include "../src/utils/win32_utils.h"

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        (L"sc2-file-state-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    const auto path = root / L"replay.SC2Replay";
    { std::ofstream file(path, std::ios::binary); file << "abc"; }
    auto fingerprint = sc2::ReadReplayFileState(path.wstring(), true);
    assert(fingerprint && fingerprint->sha256 ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    fingerprint->outcome = "failed";
    auto& db = sc2::Database::Instance();
    const auto database_path = sc2::Win32Utils::WideToUtf8((root / L"test.db").wstring());
    const auto replay_path = sc2::Win32Utils::WideToUtf8(path.wstring());
    assert(db.Open(database_path));
    assert(db.SaveReplayFileState(replay_path, *fingerprint));
    db.Close();
    assert(db.Open(database_path));
    assert(db.HasFailedReplayHash(fingerprint->sha256));
    assert(db.GetReplayFileStates().at(replay_path).SameFileVersion(*fingerprint));
    const auto copy = root / L"renamed.SC2Replay";
    std::filesystem::copy_file(path, copy);
    const auto copied = sc2::ReadReplayFileState(copy.wstring(), true);
    assert(copied && db.HasFailedReplayHash(copied->sha256));
    std::cout << "[OK] SHA-256 is correct; failed content stays marked after DB reopen and file rename.\n";

    auto& app = sc2::AppState::Instance();
    app.enable_logging = false;
    app.SetAccountFolder(sc2::Win32Utils::WideToUtf8(root.wstring()));
    auto& scanner = sc2::ReplayScanner::Instance();
    auto scan = [&](bool force) {
        scanner.StartScan(force, false);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (scanner.IsScanning() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        assert(!scanner.IsScanning());
        scanner.Stop();
    };
    scan(false);
    assert(db.GetReplayFileStates().size() == 2);
    assert(db.SaveReplayFileState("obsolete.SC2Replay", *fingerprint));
    scan(true);
    assert(db.GetReplayFileStates().size() == 2); // Full scan removed obsolete markers.
    std::cout << "[OK] Startup scan respects content markers; forced scan clears them.\n";

    // An active writer is a transient condition, even when read sharing is
    // enabled. It must not create a permanent failed-content marker.
    HANDLE writer = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(writer != INVALID_HANDLE_VALUE);
    assert(!sc2::ReadReplayFileState(path.wstring(), true));
    CloseHandle(writer);
    assert(sc2::ReadReplayFileState(path.wstring(), true));
    std::cout << "[OK] Files with an active writer remain retryable.\n";

    assert(db.ClearReplayFileStates());
    assert(db.SaveReplayFileState(replay_path, *fingerprint));
    { std::ofstream file(path, std::ios::binary); file << "abcd"; }
    auto changed = sc2::ReadReplayFileState(path.wstring(), true);
    assert(changed && !changed->SameFileVersion(*fingerprint));
    assert(changed->sha256 != fingerprint->sha256);
    assert(!db.HasFailedReplayHash(changed->sha256));
    changed->outcome = "parsed";
    assert(db.SaveReplayFileState(replay_path, *changed));
    // Repairing/overwriting a path must not erase the failed content's marker.
    assert(db.HasFailedReplayHash(fingerprint->sha256));
    assert(db.ClearReplayFileStates());
    assert(!db.HasFailedReplayHash(fingerprint->sha256));
    std::cout << "[OK] Content changes can be retried; explicit full reparse clears failed markers.\n";
    db.Close();
    std::filesystem::remove_all(root);
}
