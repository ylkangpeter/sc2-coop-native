#include "replay_watcher.h"
#include "replay_parser.h"
#include "../db/database.h"
#include "../app_state.h"
#include "../utils/win32_utils.h"

#include <filesystem>
#include <chrono>
#include <iostream>

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

    while (m_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        if (!m_running.load()) break;

        const auto& state = AppState::Instance();
        if (state.account_folder.empty()) continue;

        std::wstring root_w = Win32Utils::Utf8ToWide(state.account_folder);
        std::error_code ec;
        if (!std::filesystem::exists(root_w, ec) || ec) continue;

        try {
            // Find newest .SC2Replay file
            std::filesystem::path newest_path;
            std::filesystem::file_time_type newest_time;
            bool found_any = false;

            for (auto it = std::filesystem::recursive_directory_iterator(root_w, std::filesystem::directory_options::skip_permission_denied, ec);
                 it != std::filesystem::recursive_directory_iterator();
                 it.increment(ec)) {
                if (ec || !m_running.load()) break;

                const auto& entry = *it;
                std::error_code entry_ec;
                if (entry.is_regular_file(entry_ec)) {
                    auto ext = entry.path().extension().string();
                    for (auto& c : ext) c = tolower(c);
                    if (ext == ".sc2replay") {
                        auto cur_time = entry.last_write_time(entry_ec);
                        if (!entry_ec) {
                            if (!found_any || cur_time > newest_time) {
                                newest_time = cur_time;
                                newest_path = entry.path();
                                found_any = true;
                            }
                        }
                    }
                }
            }

            if (found_any && !newest_path.empty()) {
                std::string u8path = Win32Utils::WideToUtf8(newest_path.wstring());

                // Check if already in database
                if (!Database::Instance().HasReplay(u8path)) {
                    // Sleep to ensure SC2 finished flushing and closing the file
                    std::this_thread::sleep_for(std::chrono::milliseconds(600));

                    auto parsed = ReplayParser::ParseFile(newest_path.wstring());
                    if (parsed.has_value()) {
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
            }
        } catch (const std::exception& e) {
            // Directory watch exception swallowed safely
        }
    }
}

} // namespace sc2
