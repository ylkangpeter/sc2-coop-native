#include "replay_watcher.h"
#include "replay_parser.h"
#include "../db/database.h"
#include "../app_state.h"
#include "../utils/win32_utils.h"

#include <filesystem>
#include <chrono>
#include <iostream>
#include <algorithm>
#include <unordered_set>

namespace sc2 {

ReplayWatcher& ReplayWatcher::Instance() {
    static ReplayWatcher instance;
    return instance;
}

ReplayWatcher::~ReplayWatcher() {
    Stop();
}

void ReplayWatcher::Start() {
    if (m_running.load()) return;
    m_running = true;
    m_thread = std::thread(&ReplayWatcher::WatchLoop, this);
}

void ReplayWatcher::Stop() {
    if (!m_running.load()) return;
    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void ReplayWatcher::SetOnNewMatchCallback(std::function<void(const ReplayRecord&, bool is_best_time)> callback) {
    m_on_new_match = callback;
}

void ReplayWatcher::WatchLoop() {
    // Initial cool-down
    std::this_thread::sleep_for(std::chrono::seconds(2));

    auto watcher_start = std::chrono::system_clock::now();
    int64_t watcher_start_epoch = std::chrono::duration_cast<std::chrono::seconds>(watcher_start.time_since_epoch()).count();

    std::vector<std::filesystem::path> watched_dirs;
    auto last_dir_refresh = std::chrono::steady_clock::time_point{};

    while (m_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        if (!m_running.load()) break;

        const auto& state = AppState::Instance();
        std::string acct_folder = state.GetAccountFolder();
        if (acct_folder.empty()) continue;

        std::wstring root_w = Win32Utils::Utf8ToWide(acct_folder);
        std::error_code ec;
        if (!std::filesystem::exists(root_w, ec) || ec) continue;

        try {
            // Refresh replay directory cache periodically (every 60s) to avoid full recursive scans on every tick
            auto now_time = std::chrono::steady_clock::now();
            if (watched_dirs.empty() || (now_time - last_dir_refresh > std::chrono::seconds(60))) {
                watched_dirs.clear();
                std::error_code dir_ec;
                for (auto it = std::filesystem::recursive_directory_iterator(root_w, std::filesystem::directory_options::skip_permission_denied, dir_ec);
                     it != std::filesystem::recursive_directory_iterator();
                     it.increment(dir_ec)) {
                    if (dir_ec || !m_running.load()) break;
                    if (it->is_directory(dir_ec)) {
                        auto fname = it->path().filename().string();
                        if (fname == "Multiplayer" || fname == "Recent" || fname == "Replays") {
                            watched_dirs.push_back(it->path());
                        }
                    }
                }
                if (watched_dirs.empty()) {
                    watched_dirs.push_back(root_w);
                }
                last_dir_refresh = now_time;
            }

            // Collect all unindexed .sc2replay files across watched replay folders
            std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> unindexed_replays;
            std::unordered_set<std::string> seen_paths;

            for (const auto& dir : watched_dirs) {
                if (!m_running.load()) break;
                std::error_code dir_ec;
                for (auto it = std::filesystem::recursive_directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied, dir_ec);
                     it != std::filesystem::recursive_directory_iterator();
                     it.increment(dir_ec)) {
                    if (dir_ec || !m_running.load()) break;
                    const auto& entry = *it;
                    std::error_code entry_ec;
                    if (entry.is_regular_file(entry_ec)) {
                        auto ext = entry.path().extension().string();
                        for (auto& c : ext) c = tolower(c);
                        if (ext == ".sc2replay") {
                            std::string u8path = Win32Utils::WideToUtf8(entry.path().wstring());
                            if (seen_paths.insert(u8path).second) {
                                if (!Database::Instance().HasReplay(u8path)) {
                                    auto cur_time = entry.last_write_time(entry_ec);
                                    if (!entry_ec) {
                                        unindexed_replays.push_back({cur_time, entry.path()});
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Sort unindexed files by file write time in ascending order (older first, newest last)
            std::sort(unindexed_replays.begin(), unindexed_replays.end(),
                      [](const auto& a, const auto& b) {
                          return a.first < b.first;
                      });

            for (const auto& item : unindexed_replays) {
                if (!m_running.load()) break;
                const auto& replay_path = item.second;
                std::string u8path = Win32Utils::WideToUtf8(replay_path.wstring());

                if (Database::Instance().HasReplay(u8path)) continue;

                // Sleep to ensure SC2 finished flushing and closing the file
                std::this_thread::sleep_for(std::chrono::milliseconds(600));
                if (!m_running.load()) break;

                auto parsed = ReplayParser::ParseFile(replay_path.wstring());
                if (parsed.has_value() && parsed->result != "Unknown") {
                    auto rec = parsed.value();

                    // Check if this was the fastest time on this map
                    StatsFilter f;
                    auto stats = Database::Instance().CalculateStats(f);
                    bool is_best = false;
                    for (const auto& ms : stats.maps) {
                        if (ms.map_name == rec.map_name) {
                            if (rec.result == "Victory" && (ms.fastest_seconds <= 0 || rec.duration_seconds < ms.fastest_seconds)) {
                                is_best = true;
                            }
                            break;
                        }
                    }

                    // Save new match to database
                    Database::Instance().SaveReplay(rec);

                    // Only auto-popup if this match was completed recently:
                    // Either finished since the watcher started, or within the last 180 seconds.
                    // (Avoids popping up old replays on application startup)
                    int64_t now_epoch = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                    bool is_recently_finished = (rec.file_mtime >= (watcher_start_epoch - 60)) ||
                                                ((now_epoch - rec.file_mtime) <= 180);

                    if (is_recently_finished && m_on_new_match) {
                        m_on_new_match(rec, is_best);
                    }
                }
            }
        } catch (const std::exception& e) {
            // Directory watch exception swallowed safely
        }
    }
}

} // namespace sc2
