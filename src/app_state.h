#pragma once

#include <string>
#include <vector>
#include <unordered_set>
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
    std::string language_code = "zh_CN"; // default Chinese (Simplified)
    int language = 0; // backward compatibility index
    bool start_minimized = false;
    bool minimize_to_tray = true;
    bool start_with_windows = false;
    bool enable_logging = true;
    bool show_ally_hud = true;
    bool auto_show_summary = true;

    // Customizable Hotkeys (MOD_CONTROL=0x02, MOD_ALT=0x01, MOD_SHIFT=0x04)
    bool hotkey_summary_enabled = true;
    unsigned int hotkey_summary_mod = 0x0002 | 0x0004; // Ctrl + Shift
    unsigned int hotkey_summary_vk = 'H';              // 'H'

    bool hotkey_nav_enabled = true;
    unsigned int hotkey_prev_mod = 0x0002 | 0x0001;    // Ctrl + Alt
    unsigned int hotkey_prev_vk = 0x25;                // VK_LEFT
    unsigned int hotkey_next_mod = 0x0002 | 0x0001;    // Ctrl + Alt
    unsigned int hotkey_next_vk = 0x27;                // VK_RIGHT

    bool hotkey_ally_enabled = true;
    unsigned int hotkey_ally_mod = 0x0002 | 0x0001;    // Ctrl + Alt
    unsigned int hotkey_ally_vk = 'W';                 // 'W'

    bool hud_show_charts = true;
    bool hud_show_total_kills = false;
    bool hud_show_session = true;
    bool hud_randomizer = false;
    int session_wins = 0;
    int session_games = 0;
    std::string session_random_commander;
    std::string session_random_prestige;
    void RollRandomCommander();
    int hud_duration_seconds = 15;
    bool toast_show_border = true;
    int toast_opacity_percent = 88;
    float font_scale = 1.0f;
    int scan_interval_seconds = 1;

    // UI state
    int active_tab = 1; // Default to Games tab
    int request_tab_switch = -1;
    bool quit_requested = false;

    // Modals
    bool show_detail_modal = false;
    ReplayRecord detail_replay;
    bool show_chat_modal = false;
    ReplayRecord chat_replay;
    bool show_note_modal = false;
    std::string note_target_player;
    char note_edit_buf[1024] = {0};

    struct NoteModalRecentMatch {
        std::string game_date;
        std::string map_name;
        std::string commander;
        std::string difficulty;
        std::string result;
    };

    struct NoteModalData {
        bool valid = false;
        std::string player_name;
        int total_games = 0;
        int wins = 0;
        int losses = 0;
        double winrate = 0.0;
        int median_apm = 0;
        std::vector<NoteModalRecentMatch> recent_matches;
    };
    NoteModalData note_modal_data;

    void OpenPlayerNoteModal(const std::string& player_name);

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
    std::string GetAccountFolder() const;
    void SetAccountFolder(const std::string& folder);
    mutable std::mutex m_settings_mutex;
    void RefreshUserHandles();
    bool HasUserHandle(const std::string& handle);
    std::unordered_set<std::string> user_handles;
    std::mutex user_handles_mutex;

    void StartScanAsync(bool force_reparse = false);
    void AutoRepairCorruptedReplaysAsync();
    void StopScan();

private:
    AppState() = default;
    ~AppState();
};

} // namespace sc2
