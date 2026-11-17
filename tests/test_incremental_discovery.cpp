#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include "../src/replay/replay_discovery.h"
#include "../src/db/database.h"
#include "../src/utils/win32_utils.h"

using namespace std::chrono_literals;

static void Write(const std::filesystem::path& path, int64_t stamp) {
    { std::ofstream file(path, std::ios::binary); file << "replay content"; }
    HANDLE h = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, 0, nullptr);
    assert(h != INVALID_HANDLE_VALUE);
    FILETIME ft{static_cast<DWORD>(stamp), static_cast<DWORD>(stamp >> 32)};
    assert(SetFileTime(h, &ft, nullptr, &ft));
    CloseHandle(h);
}

int main() {
    auto root = std::filesystem::temp_directory_path() /
        (L"sc2-incremental-" + std::to_wstring(GetCurrentProcessId()));
    auto watched = root / L"录像";
    std::filesystem::create_directories(watched);
    std::atomic<bool> stop{false};
    auto& db = sc2::Database::Instance();
    const auto database = sc2::Win32Utils::WideToUtf8((root / L"test.db").wstring());
    const auto key = sc2::ReplayScanRoot(watched.wstring());
    assert(db.Open(database));
    assert(db.GetReplayScanWatermark(key) == 0);
    const auto old = sc2::ReplayFileTimeNow() - 86400LL * 10000000;
    Write(watched / L"old.SC2Replay", old);
    Write(watched / L"tie-a.SC2Replay", old + 10);
    Write(watched / L"tie-b.SC2REPLAY", old + 10);
    auto initial = sc2::DiscoverReplayChanges(watched.wstring(), db.GetReplayScanWatermark(key), stop);
    assert(initial.complete && initial.entries.size() == 3);
    assert(db.QueueReplayChanges(key, initial.through, initial.entries));
    db.Close();
    assert(db.Open(database));
    assert(db.GetReplayScanWatermark(key) == initial.through);
    auto pending = db.GetPendingReplayChanges(key);
    assert(pending.size() == 3); // Queued before watermark commit, survives restart.
    auto next = sc2::DiscoverReplayChanges(watched.wstring(), db.GetReplayScanWatermark(key), stop);
    assert(next.complete && next.entries.empty());
    auto ties = sc2::DiscoverReplayChanges(watched.wstring(), old + 10, stop);
    assert(ties.complete && ties.entries.size() == 2); // Inclusive, full-precision boundary.
    auto newer = pending[0];
    ++newer.state.file_size;
    assert(db.QueueReplayChanges(key, initial.through, {newer}));
    assert(db.CompleteReplayChange(key, pending[0]));
    assert(db.GetPendingReplayChanges(key).size() == 3); // Stale parse cannot delete newer work.
    assert(db.GetPendingReplayChanges(key + "-other").empty());
    assert(db.GetReplayScanWatermark(key + "-other") == 0);
    stop = true;
    assert(!sc2::DiscoverReplayChanges(watched.wstring(), 0, stop).complete);
    stop = false;
    assert(!sc2::DiscoverReplayChanges((watched / L"missing").wstring(), 0, stop).complete);
    assert(db.ResetReplayScan(key));
    assert(db.GetReplayScanWatermark(key) == 0 && db.GetPendingReplayChanges(key).empty());
    std::cout << "[OK] Zero and nonzero cursors use one rule; pending work and exact timestamp boundaries survive restart.\n";

    sc2::ReplayRecord legacy;
    legacy.file_path = sc2::Win32Utils::WideToUtf8((root / L"legacy" / L"last.SC2Replay").wstring());
    legacy.file_mtime = 1700000000;
    legacy.result = "Victory";
    assert(db.SaveReplay(legacy));
    assert(db.GetReplayScanWatermark(sc2::ReplayScanRoot((root / L"legacy").wstring())) ==
        (1700000000LL + 11644473600LL) * 10000000LL);
    std::cout << "[OK] Existing databases bootstrap from their latest indexed replay.\n";

    sc2::ReplayDirectoryNotifications notifications;
    assert(notifications.Open(watched.wstring()));
    Write(watched / L"new.SC2Replay", sc2::ReplayFileTimeNow());
    bool notified = false;
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!notified && std::chrono::steady_clock::now() < deadline) {
        auto changes = notifications.Poll();
        for (const auto& path : changes.paths) if (path == L"new.SC2Replay") notified = true;
        std::this_thread::sleep_for(10ms);
    }
    assert(notified);
    std::filesystem::rename(watched / L"new.SC2Replay", watched / L"renamed.SC2Replay");
    notified = false;
    const auto rename_deadline = std::chrono::steady_clock::now() + 3s;
    while (!notified && std::chrono::steady_clock::now() < rename_deadline) {
        auto changes = notifications.Poll();
        for (const auto& path : changes.paths) if (path == L"renamed.SC2Replay") notified = true;
        std::this_thread::sleep_for(10ms);
    }
    assert(notified);
    notifications.Close();
    assert(notifications.Open(watched.wstring()));
    // Do not poll while overflowing the kernel notification buffer.
    for (int i = 0; i < 3000; ++i) {
        std::ofstream file(watched / (L"overflow-" + std::to_wstring(i) + L".SC2Replay"));
    }
    bool overflow = false;
    const auto overflow_deadline = std::chrono::steady_clock::now() + 3s;
    while (!overflow && std::chrono::steady_clock::now() < overflow_deadline) {
        overflow = notifications.Poll().rescan;
        std::this_thread::sleep_for(10ms);
    }
    assert(overflow);
    const auto caught_up = sc2::DiscoverReplayChanges(watched.wstring(), 0, stop);
    assert(caught_up.complete && caught_up.entries.size() == 3004);
    notifications.Close();
    std::cout << "[OK] Real Windows create/rename notifications and buffer-overflow catch-up work.\n";
    db.Close();
    std::filesystem::remove_all(root);
}
