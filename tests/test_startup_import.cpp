#include <cassert>
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
#include "../src/app_state.h"
#include "../src/db/database.h"
#include "../src/replay/replay_watcher.h"
#include "../src/replay/replay_parser.h"
#include "../src/replay/replay_discovery.h"
#include "../src/utils/win32_utils.h"

using namespace std::chrono_literals;

// Exit without stopping the watcher or closing SQLite, as with Task Manager.
static int CrashDuringImport(const std::string& phase) {
    const auto root = std::filesystem::current_path();
    auto& db = sc2::Database::Instance();
    assert(db.Open("test.db"));
    sc2::AppState::Instance().SetAccountFolder(sc2::Win32Utils::WideToUtf8((root / L"Replays").wstring()));
    auto& watcher = sc2::ReplayWatcher::Instance();
    watcher.Start();
    const auto deadline = std::chrono::steady_clock::now() + 30s;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto progress = phase == "base" ? watcher.GetImportProgress() : watcher.GetDetailProgress();
        if (progress.completed > 0 && progress.completed < progress.total) {
            TerminateProcess(GetCurrentProcess(), 37);
        }
        std::this_thread::sleep_for(1ms);
    }
    return 1;
}

static void Interrupt(const std::filesystem::path& root, const std::wstring& phase) {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" " + phase;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    assert(CreateProcessW(exe, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr,
                          root.c_str(), &si, &pi));
    assert(WaitForSingleObject(pi.hProcess, 35000) == WAIT_OBJECT_0);
    DWORD code;
    assert(GetExitCodeProcess(pi.hProcess, &code) && code == 37);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}

int main(int argc, char** argv) {
    if (argc == 2) return CrashDuringImport(argv[1]);

    const auto temp = std::filesystem::temp_directory_path() /
        (L"sc2-startup-import-" + std::to_wstring(GetCurrentProcessId()));
    const auto library = temp / L"Replays";
    std::filesystem::create_directories(library);
    constexpr int count = 128;
    for (int i = 0; i < count; ++i) {
        const auto path = library / (std::to_wstring(i) + L".SC2Replay");
        std::filesystem::copy_file(L"tests/fixtures/sample.SC2Replay", path);
        std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() - 24h);
    }
    const auto database = sc2::Win32Utils::WideToUtf8((temp / L"test.db").wstring());
    const auto key = sc2::ReplayScanRoot(library.wstring());
    auto& db = sc2::Database::Instance();
    Interrupt(temp, L"base");
    assert(db.Open(database));
    assert(db.GetTotalReplayCount() > 0 && db.GetTotalReplayCount() < count);
    assert(db.GetPendingReplayChanges(key).size() == count);
    const auto offline = temp / L"offline";
    std::filesystem::rename(library, offline);
    sc2::AppState::Instance().SetAccountFolder(sc2::Win32Utils::WideToUtf8(library.wstring()));
    auto& watcher = sc2::ReplayWatcher::Instance();
    watcher.Start();
    const auto offline_deadline = std::chrono::steady_clock::now() + 5s;
    while (!watcher.IsWaitingForRetry() && std::chrono::steady_clock::now() < offline_deadline)
        std::this_thread::sleep_for(10ms);
    assert(watcher.IsWaitingForRetry());
    watcher.Stop();
    assert(db.GetPendingReplayChanges(key).size() == count);
    std::filesystem::rename(offline, library);
    db.Close();

    Interrupt(temp, L"details");
    assert(db.Open(database));
    const auto pending = db.GetPendingReplayChanges(key);
    assert(db.GetTotalReplayCount() == count && !pending.empty() && pending.size() < count);
    assert(pending.size() >= 2);
    const auto first = pending.front();
    const auto replaced = std::filesystem::path(sc2::Win32Utils::Utf8ToWide(pending[1].path));
    const auto stamp = std::filesystem::last_write_time(replaced);
    const auto size = std::filesystem::file_size(replaced);
    std::filesystem::copy_file(L"tests/fixtures/[CM] 湮灭快车 - P.SC2Replay", replaced,
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::resize_file(replaced, size);
    std::filesystem::last_write_time(replaced, stamp);
    const auto replacement = sc2::ReplayParser::ParseFile(replaced.wstring());
    assert(replacement && replacement->result != "Unknown");
    assert(first.state.outcome == "summary");
    sc2::ReplayRecord basic;
    basic.file_path = first.path;
    assert(!db.LoadReplayDetails(basic));
    const auto locked_path = sc2::Win32Utils::Utf8ToWide(first.path);
    HANDLE writer = CreateFileW(locked_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(writer != INVALID_HANDLE_VALUE);
    watcher.Start();
    const auto deadline = std::chrono::steady_clock::now() + 30s;
    while (db.GetPendingReplayChanges(key).size() > 1 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(20ms);
    }
    assert(db.GetPendingReplayChanges(key).size() == 1);
    assert(db.GetReplayFileState(first.path)->outcome == "summary");
    while (!watcher.IsWaitingForRetry() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(10ms);
    assert(watcher.IsWaitingForRetry());
    watcher.Stop();
    CloseHandle(writer);

    // Fail the last statement of the completion transaction. The full record
    // and its success marker must roll back along with the queue deletion.
    sqlite3* fault_db = nullptr;
    assert(sqlite3_open(database.c_str(), &fault_db) == SQLITE_OK);
    assert(sqlite3_exec(fault_db, "CREATE TRIGGER fail_commit BEFORE DELETE ON replay_scan_pending "
        "BEGIN SELECT RAISE(ABORT, 'simulated write failure'); END;", nullptr, nullptr, nullptr) == SQLITE_OK);
    watcher.Start();
    while (!watcher.IsWaitingForRetry() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(10ms);
    assert(watcher.IsWaitingForRetry());
    assert(db.GetReplayFileState(first.path)->outcome == "summary");
    assert(!db.LoadReplayDetails(basic));
    assert(db.GetPendingReplayChanges(key).size() == 1);
    assert(sqlite3_exec(fault_db, "DROP TRIGGER fail_commit;", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(fault_db);
    while (!db.GetPendingReplayChanges(key).empty() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(20ms);
    watcher.Stop();
    assert(db.GetPendingReplayChanges(key).empty());
    assert(db.GetTotalReplayCount() == count);
    int total = 0;
    auto records = db.QueryReplays("", count, 0, total);
    assert(records.size() == count);
    const auto full = sc2::ReplayParser::ParseFile(locked_path);
    assert(full);
    for (auto& record : records) {
        assert(db.GetReplayFileState(record.file_path)->outcome == "parsed");
        assert(db.LoadReplayDetails(record));
        const auto& expected = record.file_path == pending[1].path ? *replacement : *full;
        assert(record.ChatToJson() == expected.ChatToJson());
        assert(record.duration_seconds == expected.duration_seconds);
        assert(record.DetailsToJson() == expected.DetailsToJson());
    }
    std::cout << "All historical summaries and final details survived a restart and a locked replay.\n";
    db.Close();
    std::filesystem::remove_all(temp);
}
