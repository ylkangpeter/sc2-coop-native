#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include "replay/replay_types.h"
#include "db/database.h"

namespace sc2 {

class AppState {
public:
    static AppState& Instance();

    // Settings
    std::string account_folder;
    bool start_minimized = false;
    bool minimize_to_tray = true;
    bool start_with_windows = false;
    bool enable_logging = true;
    bool show_ally_hud = true;
    bool auto_show_summary = true;
    bool hotkey_summary_enabled = true;
    bool hotkey_nav_enabled = true;
    bool hotkey_ally_enabled = true;
    int hud_duration_seconds = 15;
    float font_scale = 1.0f;
    int scan_interval_seconds = 3;

    // UI state
    int active_tab = 1; // Default to Games tab
    int request_tab_switch = -1;
    bool quit_requested = false;

    // Modals
    bool show_detail_modal = false;
    ReplayRecord detail_replay;
    bool show_chat_modal = false;
    ReplayRecord chat_replay;

    // Search filters
    char games_search_buf[256] = {0};
    char players_search_buf[256] = {0};

    // Stats filters
    StatsFilter stats_filter;

    // Background scan status
    std::atomic<bool> is_scanning{false};
    std::atomic<int> scan_progress{0};
    std::atomic<int> scan_total{0};
    std::string scan_status_message;
    std::mutex scan_status_mutex;

    // Core methods
    void Init();
    void LoadSettings();
    void SaveSettings();
    void StartScanAsync(bool force_reparse);
    void StartScanAsync() { StartScanAsync(false); }
    void AutoRepairCorruptedReplaysAsync();
    void StopScan();

private:
    AppState() = default;
    ~AppState();

    std::thread m_scan_thread;
    std::atomic<bool> m_stop_scan_flag{false};
};

} // namespace sc2
