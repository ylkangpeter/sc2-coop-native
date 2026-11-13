#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <unordered_set>
#include <functional>
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

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::function<void(const ReplayRecord&, bool is_best_time)> m_on_new_match;
};

} // namespace sc2
