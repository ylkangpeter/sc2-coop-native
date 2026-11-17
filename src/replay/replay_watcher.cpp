#include "replay_watcher.h"
#include "replay_parser.h"
#include "../db/database.h"
#include "../app_state.h"
#include "../utils/win32_utils.h"
#include "../utils/diagnostics.h"

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
    m_recent_retry_counts.clear();
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
    std::string watched_root;
    auto last_dir_refresh = std::chrono::steady_clock::time_point{};
    auto last_status_log = std::chrono::steady_clock::time_point{};

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
            if (watched_dirs.empty() || watched_root != acct_folder ||
                (now_time - last_dir_refresh > std::chrono::seconds(60))) {
                watched_dirs.clear();
                const bool root_changed = watched_root != acct_folder;
                watched_root = acct_folder;
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
                // Deduplicate parent/child directories so we don't scan both Replays and Replays/Multiplayer
                std::vector<std::filesystem::path> clean_dirs;
                for (const auto& d : watched_dirs) {
                    bool has_ancestor = false;
                    for (const auto& other : watched_dirs) {
                        if (d != other) {
                            auto d_str = d.wstring();
                            auto o_str = other.wstring();
                            if (o_str.back() != L'\\' && o_str.back() != L'/') o_str += L'\\';
                            if (d_str.rfind(o_str, 0) == 0) {
                                has_ancestor = true;
                                break;
                            }
                        }
                    }
                    if (!has_ancestor) {
                        clean_dirs.push_back(d);
                    }
                }
                watched_dirs = std::move(clean_dirs);
                if (watched_dirs.empty()) {
                    watched_dirs.push_back(root_w);
                }
                last_dir_refresh = now_time;
                if (root_changed) {
                    LogDiagnostic("watcher", "watching " + acct_folder +
                        "; directories=" + std::to_string(watched_dirs.size()));
                }
            }

            // Collect all unindexed or modified .sc2replay files across watched replay folders
            struct WatchCandidate {
                int64_t disk_mtime = 0;
                bool needs_recent_notification = false;
                std::filesystem::path path;
            };
            std::vector<WatchCandidate> unindexed_replays;
            std::unordered_set<std::string> seen_paths;
            auto valid_mtimes = Database::Instance().GetIndexedReplayMtimes();
            auto coop_mtimes = Database::Instance().GetValidReplayMtimes();
            auto file_states = Database::Instance().GetReplayFileStates();

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
                                     (disk_mtime <= now_epoch + 60 && now_epoch - disk_mtime <= 300));
                                const auto notified_it = m_notified_recent_mtimes.find(u8path);
                                const bool needs_recent_notification = is_recent &&
                                    (notified_it == m_notified_recent_mtimes.end() ||
                                     notified_it->second != disk_mtime);

                                auto observed = ReadReplayFileState(entry.path().wstring());
                                auto saved_state = file_states.find(u8path);
                                const bool same_version = observed && saved_state != file_states.end() &&
                                    observed->SameFileVersion(saved_state->second);
                                if (same_version && saved_state->second.outcome == "failed") {
                                    continue;
                                }
                                const bool changed_version = observed && saved_state != file_states.end() && !same_version;
                                const bool needs_state_backfill = it_mtime != valid_mtimes.end() &&
                                    coop_mtimes.find(u8path) == coop_mtimes.end() && saved_state == file_states.end();

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
                                    changed_version ||
                                    needs_state_backfill ||
                                    needs_recent_notification) {
                                    unindexed_replays.push_back({disk_mtime, needs_recent_notification, entry.path()});
                                }
                            }
                        }
                    }
                }
            }

            // New games needing notification take top priority, then newest by disk mtime
            std::sort(unindexed_replays.begin(), unindexed_replays.end(),
                      [](const auto& a, const auto& b) {
                          if (a.needs_recent_notification != b.needs_recent_notification) {
                              return a.needs_recent_notification > b.needs_recent_notification;
                          }
                          return a.disk_mtime > b.disk_mtime;
                      });

            const auto batch_start = std::chrono::steady_clock::now();
            if (batch_start - last_status_log >= std::chrono::seconds(30)) {
                LogDiagnostic("watcher", "scan complete; files=" + std::to_string(seen_paths.size()) +
                    "; pending=" + std::to_string(unindexed_replays.size()));
                last_status_log = batch_start;
            }
            size_t processed = 0;
            for (const auto& item : unindexed_replays) {
                if (!m_running.load()) break;
                // Return to discovery promptly so a game created while this
                // batch runs does not wait behind hundreds of old replays.
                // However, never break early if there are pending recent notifications!
                if (!item.needs_recent_notification &&
                    (processed >= 16 || std::chrono::steady_clock::now() - batch_start >=
                     std::chrono::milliseconds(250))) break;
                const auto& replay_path = item.path;
                std::string u8path = Win32Utils::WideToUtf8(replay_path.wstring());

                int64_t disk_mtime = item.disk_mtime;
                if (disk_mtime <= 0) {
                    struct _stat64 st;
                    if (_wstat64(replay_path.c_str(), &st) == 0) {
                        disk_mtime = st.st_mtime;
                    }
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
                     (disk_mtime <= now_epoch + 60 && now_epoch - disk_mtime <= 300));
                const auto notified_it = m_notified_recent_mtimes.find(u8path);
                const bool needs_recent_notification = is_recent &&
                    (notified_it == m_notified_recent_mtimes.end() ||
                     notified_it->second != disk_mtime);

                auto observed = ReadReplayFileState(replay_path.wstring());
                auto saved_state = file_states.find(u8path);
                const bool changed_version = observed && saved_state != file_states.end() &&
                    !observed->SameFileVersion(saved_state->second);
                const bool already_indexed = !changed_version &&
                    Database::Instance().GetReplayMtime(u8path) >= disk_mtime &&
                    Database::Instance().HasReplay(u8path);
                const bool needs_state_backfill = saved_state == file_states.end() &&
                    coop_mtimes.find(u8path) == coop_mtimes.end();
                if (!needs_recent_notification && already_indexed && !needs_state_backfill) {
                    continue;
                }

                // Incomplete files already have a retry path. A fixed wait for
                // every historical file adds minutes to live notifications.
                ++processed;
                auto fingerprint = ReadReplayFileState(replay_path.wstring(), true);
                if (!fingerprint) {
                    m_failed_retry_after[u8path] = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                    continue;
                }
                if (!needs_recent_notification && already_indexed) {
                    // Historical arcade records were already parsed and saved.
                    // Add their missing content marker without parsing again.
                    fingerprint->outcome = "parsed";
                    Database::Instance().SaveReplayFileState(u8path, *fingerprint);
                    continue;
                }
                if (Database::Instance().HasFailedReplayHash(fingerprint->sha256)) {
                    fingerprint->outcome = "failed";
                    Database::Instance().SaveReplayFileState(u8path, *fingerprint);
                    m_failed_retry_after.erase(u8path);
                    m_recent_retry_counts.erase(u8path);
                    LogDiagnostic("watcher", "skipped known failed SHA-256: " + u8path);
                    continue;
                }
                std::optional<ReplayRecord> parsed;
                try {
                    parsed = ReplayParser::ParseFile(replay_path.wstring());
                } catch (...) {
                    parsed = std::nullopt;
                }

                auto after_parse = ReadReplayFileState(replay_path.wstring());
                if (!after_parse || !after_parse->SameFileVersion(*fingerprint)) {
                    m_failed_retry_after[u8path] = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                    continue;
                }

                if (!parsed.has_value() || parsed->result == "Unknown") {
                    int retries = ++m_recent_retry_counts[u8path];
                    if (is_recent && retries < 5) {
                        m_failed_retry_after[u8path] = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                        LogDiagnostic("watcher", "retrying recent match parse: " + u8path + " (attempt " + std::to_string(retries) + ")");
                        continue;
                    }
                    fingerprint->outcome = "failed";
                    Database::Instance().SaveReplayFileState(u8path, *fingerprint);
                    m_failed_retry_after.erase(u8path);
                    m_recent_retry_counts.erase(u8path);
                    LogDiagnostic("watcher", "marked failed SHA-256=" + fingerprint->sha256 + ": " + u8path);
                } else {
                    m_failed_retry_after.erase(u8path);
                    m_recent_retry_counts.erase(u8path);
                    auto rec = parsed.value();

                    // Check if this was the fastest time on this map
                    int best_time = Database::Instance().GetFastestSecondsForMap(rec.map_name);
                    bool is_best = (rec.result == "Victory" && rec.duration_seconds > 0 &&
                                    (best_time <= 0 || rec.duration_seconds < best_time));

                    // Save new match to database
                    if (!Database::Instance().SaveReplay(rec)) {
                        LogDiagnostic("watcher", "database save failed: " + u8path);
                    } else {
                        fingerprint->outcome = "parsed";
                        Database::Instance().SaveReplayFileState(u8path, *fingerprint);
                        if (is_recent) LogDiagnostic("watcher", "indexed: " + u8path);
                    }

                    // Only auto-popup if this match was completed recently:
                    // Either finished since the watcher started, or within the last 300 seconds (with 60s future clock-skew tolerance).
                    // (Avoids popping up old replays on application startup)
                    int64_t callback_now_epoch = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                    bool is_recently_finished = rec.file_mtime > 0 &&
                                                (rec.file_mtime >= (watcher_start_epoch - 60) ||
                                                 (rec.file_mtime <= callback_now_epoch + 60 &&
                                                  callback_now_epoch - rec.file_mtime <= 300));

                    if (is_recently_finished && m_on_new_match) {
                        LogDiagnostic("watcher", "notifying UI: " + u8path);
                        m_notified_recent_mtimes[u8path] = rec.file_mtime;
                        m_on_new_match(rec, is_best);
                    }
                }
            }
        } catch (const std::exception& e) {
            LogDiagnostic("watcher", std::string("scan error: ") + e.what());
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
