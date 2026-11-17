#pragma once

#include <windows.h>
#include <array>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <vector>
#include "replay_file_state.h"

namespace sc2 {

std::mutex& ReplayScanMutex();
std::string ReplayScanRoot(const std::wstring& path);
int64_t ReplayFileTimeNow();
int64_t ReplayEpochSeconds(int64_t file_time);

struct ReplayDiscovery {
    std::vector<ReplayScanEntry> entries;
    int64_t through = 0;
    bool complete = true;
};

// One discovery rule: timestamps >= since. since=0 includes every replay.
// FindFirst/NextFile supply timestamps and sizes without stat'ing each file.
ReplayDiscovery DiscoverReplayChanges(const std::wstring& path, int64_t since,
                                      const std::atomic<bool>& stop, bool stop_value = true);

class ReplayDirectoryNotifications {
public:
    ~ReplayDirectoryNotifications() { Close(); }
    bool Open(const std::wstring& root);
    void Close();
    bool IsOpen() const { return m_directory != INVALID_HANDLE_VALUE; }
    struct Changes {
        std::vector<std::wstring> paths;
        bool rescan = false;
    };
    Changes Poll();

private:
    bool Arm();
    HANDLE m_directory = INVALID_HANDLE_VALUE;
    HANDLE m_event = nullptr;
    OVERLAPPED m_overlapped{};
    std::array<DWORD, 16384> m_buffer{};
};

} // namespace sc2
