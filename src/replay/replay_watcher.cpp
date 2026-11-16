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
#include <unordered_map>
#include <cwctype>
#include <sys/stat.h>

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
    m_failed_retry_after.clear();
    m_notified_recent_mtimes.clear();
    m_running = true;
    m_thread = std::thread(&ReplayWatcher::WatchLoop, this);
}

void ReplayWatcher::Stop() {
    if (!m_running.load()) return;
    m_running = false;
    m_cv.notify_all();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

bool ReplayWatcher::InterruptibleSleep(std::chrono::milliseconds ms) {
    std::unique_lock<std::mutex> lock(m_cv_mutex);
    return !m_cv.wait_for(lock, ms, [this]() { return !m_running.load(); });
}

void ReplayWatcher::SetOnNewMatchCallback(std::function<void(const ReplayRecord&, bool is_best_time)> callback) {
    m_on_new_match = callback;
}

void ReplayWatcher::WatchLoop() {
    // Initial cool-down
    if (!InterruptibleSleep(std::chrono::seconds(2))) return;

    auto watcher_start = std::chrono::system_clock::now();
    int64_t watcher_start_epoch = std::chrono::duration_cast<std::chrono::seconds>(watcher_start.time_since_epoch()).count();

    std::vector<std::filesystem::path> watched_dirs;
    auto last_dir_refresh = std::chrono::steady_clock::time_point{};

    while (m_running.load()) {
        int interval_sec = AppState::Instance().scan_interval_seconds;
        if (interval_sec < 1) interval_sec = 1;
        if (interval_sec > 10) interval_sec = 10;
        if (!InterruptibleSleep(std::chrono::seconds(interval_sec))) break;

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
                        const auto fname = it->path().filename().wstring();
                        if (fname == L"Multiplayer" || fname == L"Recent" || fname == L"Replays") {
                            watched_dirs.push_back(it->path());
                        }
                    }
                }
                if (watched_dirs.empty()) {
                    watched_dirs.push_back(root_w);
                }
                last_dir_refresh = now_time;
            }

            // Collect all unindexed or modified .sc2replay files across watched replay folders
            std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> unindexed_replays;
            std::unordered_set<std::string> seen_paths;
            auto valid_mtimes = Database::Instance().GetValidReplayMtimes();

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
                        auto ext = entry.path().extension().wstring();
                        for (auto& c : ext) c = static_cast<wchar_t>(std::towlower(c));
                        if (ext == L".sc2replay") {
                            std::string u8path = Win32Utils::WideToUtf8(entry.path().wstring());
                            if (seen_paths.insert(u8path).second) {
                                auto it_mtime = valid_mtimes.find(u8path);
                                struct _stat64 st;
                                int64_t disk_mtime = 0;
                                if (_wstat64(entry.path().c_str(), &st) == 0) {
                                    disk_mtime = st.st_mtime;
                                }

                                const int64_t now_epoch = std::chrono::duration_cast<std::chrono::seconds>(
                                    std::chrono::system_clock::now().time_since_epoch()).count();
                                const bool is_recent = disk_mtime > 0 &&
                                    (disk_mtime >= watcher_start_epoch - 60 ||
                                     (now_epoch >= disk_mtime && now_epoch - disk_mtime <= 180));
                                const auto notified_it = m_notified_recent_mtimes.find(u8path);
                                const bool needs_recent_notification = is_recent &&
                                    (notified_it == m_notified_recent_mtimes.end() ||
                                     notified_it->second != disk_mtime);

                                auto retry_it = m_failed_retry_after.find(u8path);
                                if (retry_it != m_failed_retry_after.end() &&
                                    std::chrono::steady_clock::now() < retry_it->second) {
                                    // Give a partially written replay a short retry backoff.
                                    continue;
                                }

                                // The startup scanner may have inserted a just-finished
                                // replay before this loop reaches it. Keep recent files in
                                // the queue until the watcher has delivered their popup.
                                if (it_mtime == valid_mtimes.end() ||
                                    disk_mtime > it_mtime->second ||
                                    needs_recent_notification) {
                                    auto cur_time = entry.last_write_time(entry_ec);
                                    // A replay can still be held open by SC2, so a
                                    // transient last_write_time error must not make
                                    // us drop the candidate. Sorting is only an
                                    // optimization; the disk mtime above remains
                                    // the source of truth for change detection.
                                    if (entry_ec) cur_time = std::filesystem::file_time_type::min();
                                    unindexed_replays.push_back({cur_time, entry.path()});
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

                struct _stat64 st;
                int64_t disk_mtime = 0;
                if (_wstat64(replay_path.c_str(), &st) == 0) {
                    disk_mtime = st.st_mtime;
                }

                auto retry_it = m_failed_retry_after.find(u8path);
                if (retry_it != m_failed_retry_after.end() &&
                    std::chrono::steady_clock::now() < retry_it->second) {
                    // Give a partially written replay a short retry backoff.
                    continue;
                }

                const int64_t now_epoch = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                const bool is_recent = disk_mtime > 0 &&
                    (disk_mtime >= watcher_start_epoch - 60 ||
                     (now_epoch >= disk_mtime && now_epoch - disk_mtime <= 180));
                const auto notified_it = m_notified_recent_mtimes.find(u8path);
                const bool needs_recent_notification = is_recent &&
                    (notified_it == m_notified_recent_mtimes.end() ||
                     notified_it->second != disk_mtime);

                if (!needs_recent_notification &&
                    Database::Instance().GetReplayMtime(u8path) >= disk_mtime &&
                    Database::Instance().HasValidReplay(u8path)) {
                    continue;
                }

                // Sleep to ensure SC2 finished flushing and closing the file
                if (!InterruptibleSleep(std::chrono::milliseconds(600))) break;

                std::optional<ReplayRecord> parsed;
                try {
                    parsed = ReplayParser::ParseFile(replay_path.wstring());
                } catch (...) {
                    parsed = std::nullopt;
                }

                if (!parsed.has_value() || parsed->result == "Unknown") {
                    m_failed_retry_after[u8path] =
                        std::chrono::steady_clock::now() + std::chrono::seconds(1);
                } else {
                    m_failed_retry_after.erase(u8path);
                    auto rec = parsed.value();

                    // Check if this was the fastest time on this map
                    int best_time = Database::Instance().GetFastestSecondsForMap(rec.map_name);
                    bool is_best = (rec.result == "Victory" && rec.duration_seconds > 0 &&
                                    (best_time <= 0 || rec.duration_seconds < best_time));

                    // Save new match to database
                    Database::Instance().SaveReplay(rec);

                    // Only auto-popup if this match was completed recently:
                    // Either finished since the watcher started, or within the last 180 seconds.
                    // (Avoids popping up old replays on application startup)
                    int64_t callback_now_epoch = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                    bool is_recently_finished = rec.file_mtime > 0 &&
                                                (rec.file_mtime >= (watcher_start_epoch - 60) ||
                                                 (callback_now_epoch >= rec.file_mtime &&
                                                  callback_now_epoch - rec.file_mtime <= 180));

                    if (is_recently_finished && m_on_new_match) {
                        m_notified_recent_mtimes[u8path] = rec.file_mtime;
                        m_on_new_match(rec, is_best);
                    }
                }
            }
        } catch (const std::exception&) {
            // If a filesystem conversion or permission error interrupts the
            // directory cache refresh, fall back to the account root. This
            // preserves the Python watcher's full-tree behavior and avoids a
            // permanently empty watch set.
            if (watched_dirs.empty()) {
                watched_dirs.push_back(root_w);
            }
        }
    }
}

} // namespace sc2
