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

    while (m_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        if (!m_running.load()) break;

        const auto& state = AppState::Instance();
        if (state.account_folder.empty()) continue;

        std::wstring root_w = Win32Utils::Utf8ToWide(state.account_folder);
        if (!std::filesystem::exists(root_w)) continue;

        try {
            // Find newest .SC2Replay file
            std::filesystem::path newest_path;
            std::filesystem::file_time_type newest_time;
            bool found_any = false;

            for (const auto& entry : std::filesystem::recursive_directory_iterator(root_w, std::filesystem::directory_options::skip_permission_denied)) {
                if (!m_running.load()) break;
                if (entry.is_regular_file()) {
                    auto ext = entry.path().extension().string();
                    for (auto& c : ext) c = tolower(c);
                    if (ext == ".sc2replay") {
                        auto cur_time = entry.last_write_time();
                        if (!found_any || cur_time > newest_time) {
                            newest_time = cur_time;
                            newest_path = entry.path();
                            found_any = true;
                        }
                    }
                }
            }

            if (found_any && !newest_path.empty()) {
                std::string u8path = Win32Utils::WideToUtf8(newest_path.wstring());

                // Check if already in database
                if (!Database::Instance().HasReplay(u8path)) {
                    // Small sleep to ensure SC2 finished writing file
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));

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

                        // Trigger callback for post-game summary overlay
                        if (m_on_new_match) {
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
