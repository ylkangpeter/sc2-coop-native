#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
#include "../src/app_state.h"
#include "../src/db/database.h"
#include "../src/replay/replay_watcher.h"
#include "../src/replay/replay_discovery.h"
#include "../src/utils/win32_utils.h"

using namespace std::chrono_literals;

int main() {
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
    auto& db = sc2::Database::Instance();
    assert(db.Open(sc2::Win32Utils::WideToUtf8((temp / L"test.db").wstring())));
    sc2::AppState::Instance().SetAccountFolder(sc2::Win32Utils::WideToUtf8(library.wstring()));
    auto& watcher = sc2::ReplayWatcher::Instance();
    const auto start = std::chrono::steady_clock::now();
    bool saw_progress = false;
    int last_completed = 0;
    watcher.Start();
    while (db.GetTotalReplayCount() < count && std::chrono::steady_clock::now() - start < 90s) {
        const auto progress = watcher.GetImportProgress();
        if (progress.total > 0) {
            assert(progress.total == count);
            assert(progress.completed >= last_completed && progress.completed <= count);
            last_completed = progress.completed;
            saw_progress |= progress.completed > 0 && progress.completed < count;
        }
        std::this_thread::sleep_for(10ms);
    }
    const auto key = sc2::ReplayScanRoot(library.wstring());
    while (watcher.GetImportProgress().total > 0 && std::chrono::steady_clock::now() - start < 90s) {
        std::this_thread::sleep_for(10ms);
    }
    assert(saw_progress && watcher.GetImportProgress().total == 0);
    assert(db.GetPendingReplayChanges(key).empty());
    watcher.Stop();
    assert(db.GetTotalReplayCount() == count);
    std::cout << "Imported " << count << " historical replays in "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << " s\n";
    db.Close();
    std::filesystem::remove_all(temp);
}
