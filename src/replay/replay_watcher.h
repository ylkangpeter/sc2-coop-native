#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <unordered_set>
#include <unordered_map>
#include <functional>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include "replay_types.h"

namespace sc2 {

class ReplayWatcher {
public:
    static ReplayWatcher& Instance();

    void Start();
    void Stop();
    bool IsRunning() const { return m_running.load(); }

    void SetOnNewMatchCallback(std::function<void(const ReplayRecord&, bool is_best_time)> callback);

private:
    ReplayWatcher() = default;
    ~ReplayWatcher();

    void WatchLoop();
    bool InterruptibleSleep(std::chrono::milliseconds ms);

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::condition_variable m_cv;
    std::mutex m_cv_mutex;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_failed_retry_after;
    std::unordered_map<std::string, int> m_recent_retry_counts;
    // Tracks recent replays that have already generated a popup. The initial
    // scanner can save a replay before this watcher sees it, so a valid DB row
    // alone must not suppress the first notification.
    std::unordered_map<std::string, int64_t> m_notified_recent_mtimes;
    std::function<void(const ReplayRecord&, bool is_best_time)> m_on_new_match;
};

} // namespace sc2
