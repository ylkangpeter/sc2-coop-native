#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>

namespace sc2 {

class ReplayScanner {
public:
    static ReplayScanner& Instance();

    void StartScan(bool force_reparse = false, bool auto_repair_after = true);
    void StartRepair();
    void Stop();
    bool IsScanning() const { return m_is_scanning.load(); }

private:
    ReplayScanner() = default;
    ~ReplayScanner();
    ReplayScanner(const ReplayScanner&) = delete;
    ReplayScanner& operator=(const ReplayScanner&) = delete;

    void RunScan(bool force_reparse, bool auto_repair_after);
    void RunRepairInternal();

    std::thread m_worker_thread;
    std::atomic<bool> m_stop_flag{false};
    std::atomic<bool> m_is_scanning{false};
    mutable std::mutex m_start_stop_mutex;
};

} // namespace sc2
