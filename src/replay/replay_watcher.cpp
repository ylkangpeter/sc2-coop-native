#include "replay_watcher.h"
#include "replay_parser.h"
#include "replay_discovery.h"
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
    m_recent_retry_hashes.clear();
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
    const int64_t watcher_start_epoch = ReplayEpochSeconds(ReplayFileTimeNow());
    auto& db = Database::Instance();
    ReplayDirectoryNotifications notifications;
    std::string watched_root;
    int64_t watermark = 0;
    std::vector<ReplayScanEntry> pending;
    size_t next = 0;
    bool reload_pending = false;
    bool run_immediately = true;
    ImportProgress progress{};
    auto retry_watch_at = std::chrono::steady_clock::time_point{};

    while (m_running.load()) {
        const int interval = std::clamp(AppState::Instance().scan_interval_seconds, 1, 10);
        if (!run_immediately && !InterruptibleSleep(std::chrono::seconds(interval))) break;
        run_immediately = false;
        // Explicit scans and the live watcher share one durable queue. Do not
        // race a forced reset or parse the same replay on two threads.
        std::unique_lock<std::mutex> scan_lock(ReplayScanMutex());
        if (!m_running.load()) break;
        const auto folder = AppState::Instance().GetAccountFolder();
        if (folder.empty()) {
            notifications.Close();
            watched_root.clear();
            pending.clear();
            next = 0;
            m_import_progress = progress = {};
            continue;
        }
        const auto root_w = Win32Utils::Utf8ToWide(folder);
        try {
            const auto root = ReplayScanRoot(root_w);
            const bool root_changed = root != watched_root;
            bool discover = root_changed;
            if (root_changed) {
                // Register before catch-up, so files arriving during enumeration
                // remain buffered by Windows for the next iteration.
                notifications.Open(root_w);
                watched_root = root;
                m_import_progress = progress = {};
                watermark = db.GetReplayScanWatermark(root);
                reload_pending = true; // Recover work durably queued before exit.
                retry_watch_at = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                LogDiagnostic("watcher", "watching " + folder + "; last_update=" + std::to_string(watermark));
            }

            auto changes = notifications.Poll();
            discover = discover || changes.rescan;
            if (!notifications.IsOpen() && std::chrono::steady_clock::now() >= retry_watch_at) {
                notifications.Open(root_w);
                discover = true;
                retry_watch_at = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            }
            if (discover) {
                ++m_discovery_count;
                auto found = DiscoverReplayChanges(root_w, watermark, m_running, false);
                // Even a partial enumeration can queue work, but may not advance
                // the watermark past unreadable/unvisited directories.
                const auto through = found.complete ? found.through : watermark;
                if (!db.QueueReplayChanges(root, through, found.entries)) {
                    notifications.Close(); // Retry from the unchanged durable cursor.
                    retry_watch_at = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                    continue;
                }
                watermark = through;
                reload_pending = true;
                LogDiagnostic("watcher", "incremental discovery; candidates=" + std::to_string(found.entries.size()) +
                    "; complete=" + std::to_string(found.complete));
                if (!found.complete) {
                    notifications.Close();
                    retry_watch_at = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                }
            }
            if (!changes.paths.empty()) {
                std::unordered_set<std::wstring> seen;
                std::vector<ReplayScanEntry> entries;
                for (const auto& relative : changes.paths) {
                    if (!seen.insert(relative).second) continue;
                    // A notification names the changed path directly. Include
                    // preserved old timestamps when a replay/tree is copied in.
                    ++m_discovery_count;
                    auto found = DiscoverReplayChanges((std::filesystem::path(root_w) / relative).wstring(),
                        0, m_running, false);
                    entries.insert(entries.end(), found.entries.begin(), found.entries.end());
                }
                if (!entries.empty()) {
                    if (!db.QueueReplayChanges(root, watermark, entries)) {
                        notifications.Close();
                        retry_watch_at = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                        continue;
                    }
                    reload_pending = true;
                }
            }
            if (m_check_pending.exchange(false)) {
                watermark = db.GetReplayScanWatermark(root);
                reload_pending = true;
            }
            if (reload_pending) {
                pending = db.GetPendingReplayChanges(root);
                next = 0;
                reload_pending = false;
                if (pending.empty()) progress = {};
                progress.total = progress.completed + static_cast<int>(pending.size());
                m_import_progress = progress;
            }
            // Keep our place across batches. Release the scan lock and poll for
            // new games between them, without sleeping while historical work remains.
            const auto batch_start = std::chrono::steady_clock::now();
            const auto batch_first = next;
            for (; next < pending.size() && m_running.load(); ++next) {
                if (next > batch_first && (next - batch_first >= 16 ||
                    std::chrono::steady_clock::now() - batch_start >= std::chrono::milliseconds(250))) {
                    run_immediately = true;
                    break;
                }
                const auto& item = pending[next];
                const auto finish_item = [&] {
                    if (!db.CompleteReplayChange(root, item)) return false;
                    ++progress.completed;
                    m_import_progress = progress;
                    return true;
                };
                const auto& path = item.path;
                const auto wide = Win32Utils::Utf8ToWide(path);
                auto retry = m_failed_retry_after.find(path);
                if (retry != m_failed_retry_after.end() && std::chrono::steady_clock::now() < retry->second) continue;
                const auto observed = ReadReplayFileState(wide);
                if (!observed) {
                    const auto error = GetLastError();
                    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
                        finish_item();
                    } else {
                        m_failed_retry_after[path] = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                    }
                    continue;
                }
                const int64_t disk_mtime = ReplayEpochSeconds(observed->write_time);
                const int64_t now_epoch = ReplayEpochSeconds(ReplayFileTimeNow());
                const bool is_recent = disk_mtime > 0 && disk_mtime <= now_epoch + 60 &&
                    (disk_mtime >= watcher_start_epoch - 60 || now_epoch - disk_mtime <= 300);
                const auto notified = m_notified_recent_mtimes.find(path);
                const bool needs_notification = is_recent &&
                    (notified == m_notified_recent_mtimes.end() || notified->second != disk_mtime);

                const auto saved = db.GetReplayFileState(path);
                const bool same_version = saved && observed->SameFileVersion(*saved);
                const bool changed_version = saved && !same_version;
                if (same_version && (saved->outcome == "failed" || !needs_notification)) {
                    finish_item();
                    continue;
                }
                const bool already_indexed = !changed_version && db.GetReplayMtime(path) >= disk_mtime && db.HasReplay(path);
                if (!needs_notification && already_indexed && db.HasValidReplay(path)) {
                    finish_item();
                    continue;
                }
                auto fingerprint = ReadReplayFileState(wide, true);
                if (!fingerprint) {
                    m_failed_retry_after[path] = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                    continue;
                }
                if (m_recent_retry_hashes[path] != fingerprint->sha256) {
                    m_recent_retry_counts.erase(path);
                    m_recent_retry_hashes[path] = fingerprint->sha256;
                }
                auto complete = [&](const std::string& outcome) {
                    fingerprint->outcome = outcome;
                    if (db.SaveReplayFileState(path, *fingerprint) && finish_item()) {
                        m_failed_retry_after.erase(path);
                        m_recent_retry_counts.erase(path);
                        m_recent_retry_hashes.erase(path);
                        return true;
                    }
                    m_failed_retry_after[path] = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                    return false;
                };
                if (!needs_notification && already_indexed) { complete("parsed"); continue; }
                if (db.HasFailedReplayHash(fingerprint->sha256)) { complete("failed"); continue; }
                std::optional<ReplayRecord> parsed;
                try { parsed = ReplayParser::ParseFile(wide); } catch (...) { parsed = std::nullopt; }
                const auto after = ReadReplayFileState(wide);
                if (!after || !after->SameFileVersion(*fingerprint)) {
                    m_failed_retry_after[path] = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                    continue;
                }
                if (!parsed || parsed->result == "Unknown") {
                    if (is_recent && ++m_recent_retry_counts[path] < 5) {
                        m_failed_retry_after[path] = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                        continue;
                    }
                    complete("failed");
                    continue;
                }
                const int best = needs_notification ? db.GetFastestSecondsForMap(parsed->map_name) : 0;
                const bool is_best = parsed->result == "Victory" && parsed->duration_seconds > 0 &&
                    (best <= 0 || parsed->duration_seconds < best);
                if (!db.SaveReplay(*parsed) || !complete("parsed")) {
                    m_failed_retry_after[path] = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                    continue;
                }
                if (needs_notification && m_on_new_match) {
                    m_notified_recent_mtimes[path] = disk_mtime;
                    LogDiagnostic("watcher", "notifying UI: " + path);
                    m_on_new_match(*parsed, is_best);
                }
            }
            if (!pending.empty() && next == pending.size()) {
                pending.clear();
                next = 0;
                // Revisit incomplete files at the normal polling interval.
                reload_pending = progress.completed < progress.total;
                if (!reload_pending) m_import_progress = progress = {};
            }
        } catch (const std::exception& e) {
            reload_pending = true;
            LogDiagnostic("watcher", std::string("incremental scan error: ") + e.what());
            notifications.Close();
            retry_watch_at = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        }
    }
    m_import_progress = ImportProgress{};
}

} // namespace sc2
