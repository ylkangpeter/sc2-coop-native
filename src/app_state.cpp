#include "app_state.h"
#include "i18n/i18n.h"
#include "db/database.h"
#include "replay/replay_parser.h"
#include "utils/win32_utils.h"

#include <windows.h>
#include <filesystem>
#include <iostream>

namespace sc2 {

AppState& AppState::Instance() {
    static AppState state;
    return state;
}

AppState::~AppState() {
    StopScan();
}

void AppState::Init() {
    Database::Instance().Open("coop_data.db");
    LoadSettings();

    // If account_folder is empty, auto-detect
    if (account_folder.empty() || !std::filesystem::exists(Win32Utils::Utf8ToWide(account_folder))) {
        account_folder = Win32Utils::FindDefaultAccountsFolder();
        SaveSettings();
    } else {
        RefreshUserHandles();
    }

    // Auto-repair any corrupted replays in background
    AutoRepairCorruptedReplaysAsync();

    // Auto-start initial background scan if replays directory is configured
    if (!account_folder.empty() && std::filesystem::exists(Win32Utils::Utf8ToWide(account_folder))) {
        StartScanAsync(false);
    }
}

void AppState::LoadSettings() {
    auto& db = Database::Instance();
    account_folder = db.GetSetting("account_folder", "");

    language_code = db.GetSetting("language_code", "");
    if (language_code.empty()) {
        std::string lang_str = db.GetSetting("language", "0");
        try { language = std::stoi(lang_str); } catch (...) { language = 0; }
        language_code = (language == 1) ? "en_US" : "zh_CN";
    }
    I18n::Init(language_code);
    language = I18n::GetCurrentLanguageIndex();

    start_minimized = (db.GetSetting("start_minimized", "0") == "1");
    minimize_to_tray = (db.GetSetting("minimize_to_tray", "1") == "1");
    start_with_windows = (db.GetSetting("start_with_windows", "0") == "1");
    enable_logging = (db.GetSetting("enable_logging", "1") == "1");
    show_ally_hud = (db.GetSetting("show_ally_hud", "1") == "1");
    auto_show_summary = (db.GetSetting("auto_show_summary", "1") == "1");

    hotkey_summary_enabled = (db.GetSetting("hotkey_summary_enabled", "1") == "1");
    try {
        hotkey_summary_mod = static_cast<unsigned int>(std::stoul(db.GetSetting("hotkey_summary_mod", std::to_string(0x0002 | 0x0004))));
        hotkey_summary_vk = static_cast<unsigned int>(std::stoul(db.GetSetting("hotkey_summary_vk", std::to_string('H'))));
    } catch (...) {
        hotkey_summary_mod = 0x0002 | 0x0004;
        hotkey_summary_vk = 'H';
    }

    hotkey_nav_enabled = (db.GetSetting("hotkey_nav_enabled", "1") == "1");
    try {
        hotkey_prev_mod = static_cast<unsigned int>(std::stoul(db.GetSetting("hotkey_prev_mod", std::to_string(0x0002 | 0x0001))));
        hotkey_prev_vk = static_cast<unsigned int>(std::stoul(db.GetSetting("hotkey_prev_vk", std::to_string(0x25))));
        hotkey_next_mod = static_cast<unsigned int>(std::stoul(db.GetSetting("hotkey_next_mod", std::to_string(0x0002 | 0x0001))));
        hotkey_next_vk = static_cast<unsigned int>(std::stoul(db.GetSetting("hotkey_next_vk", std::to_string(0x27))));
    } catch (...) {
        hotkey_prev_mod = 0x0002 | 0x0001;
        hotkey_prev_vk = 0x25;
        hotkey_next_mod = 0x0002 | 0x0001;
        hotkey_next_vk = 0x27;
    }

    hotkey_ally_enabled = (db.GetSetting("hotkey_ally_enabled", "1") == "1");
    try {
        hotkey_ally_mod = static_cast<unsigned int>(std::stoul(db.GetSetting("hotkey_ally_mod", std::to_string(0x0002 | 0x0001))));
        hotkey_ally_vk = static_cast<unsigned int>(std::stoul(db.GetSetting("hotkey_ally_vk", std::to_string('W'))));
    } catch (...) {
        hotkey_ally_mod = 0x0002 | 0x0001;
        hotkey_ally_vk = 'W';
    }

    std::string fs = db.GetSetting("font_scale", "1.0");
    try { font_scale = std::stof(fs); } catch (...) { font_scale = 1.0f; }

    std::string hd = db.GetSetting("hud_duration", "15");
    try { hud_duration_seconds = std::stoi(hd); } catch (...) { hud_duration_seconds = 15; }
}

void AppState::SaveSettings() {
    auto& db = Database::Instance();
    db.SetSetting("account_folder", account_folder);
    RefreshUserHandles();
    db.SetSetting("language_code", language_code);
    db.SetSetting("language", std::to_string(language));
    I18n::SetLanguage(language_code);

    db.SetSetting("start_minimized", start_minimized ? "1" : "0");
    db.SetSetting("minimize_to_tray", minimize_to_tray ? "1" : "0");
    db.SetSetting("start_with_windows", start_with_windows ? "1" : "0");
    db.SetSetting("enable_logging", enable_logging ? "1" : "0");
    db.SetSetting("show_ally_hud", show_ally_hud ? "1" : "0");
    db.SetSetting("auto_show_summary", auto_show_summary ? "1" : "0");

    db.SetSetting("hotkey_summary_enabled", hotkey_summary_enabled ? "1" : "0");
    db.SetSetting("hotkey_summary_mod", std::to_string(hotkey_summary_mod));
    db.SetSetting("hotkey_summary_vk", std::to_string(hotkey_summary_vk));

    db.SetSetting("hotkey_nav_enabled", hotkey_nav_enabled ? "1" : "0");
    db.SetSetting("hotkey_prev_mod", std::to_string(hotkey_prev_mod));
    db.SetSetting("hotkey_prev_vk", std::to_string(hotkey_prev_vk));
    db.SetSetting("hotkey_next_mod", std::to_string(hotkey_next_mod));
    db.SetSetting("hotkey_next_vk", std::to_string(hotkey_next_vk));

    db.SetSetting("hotkey_ally_enabled", hotkey_ally_enabled ? "1" : "0");
    db.SetSetting("hotkey_ally_mod", std::to_string(hotkey_ally_mod));
    db.SetSetting("hotkey_ally_vk", std::to_string(hotkey_ally_vk));

    db.SetSetting("font_scale", std::to_string(font_scale));
    db.SetSetting("hud_duration", std::to_string(hud_duration_seconds));

    // Update Windows run key
    wchar_t exe_buf[MAX_PATH];
    GetModuleFileNameW(nullptr, exe_buf, MAX_PATH);
    Win32Utils::SetAutoStart(start_with_windows, "SC2CoopNative", Win32Utils::WideToUtf8(exe_buf));
}

void AppState::RefreshUserHandles() {
    std::lock_guard<std::mutex> lock(user_handles_mutex);
    user_handles.clear();
    if (account_folder.empty()) return;

    std::filesystem::path folder = Win32Utils::Utf8ToWide(account_folder);
    std::error_code ec;

    while (folder.has_parent_path()) {
        auto parent = folder.parent_path();
        if (parent.wstring().find(L"StarCraft") != std::wstring::npos) {
            folder = parent;
        } else {
            break;
        }
    }

    std::filesystem::path scan_root = std::filesystem::exists(folder / L"Accounts", ec) ? (folder / L"Accounts") : std::filesystem::path(Win32Utils::Utf8ToWide(account_folder));
    if (!std::filesystem::exists(scan_root, ec)) {
        scan_root = std::filesystem::path(Win32Utils::Utf8ToWide(account_folder));
        if (!std::filesystem::exists(scan_root, ec)) return;
    }

    for (auto it = std::filesystem::recursive_directory_iterator(scan_root, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator();
         it.increment(ec)) {
        if (ec) continue;
        const auto& entry = *it;
        if (entry.is_directory(ec)) {
            std::string dname = entry.path().filename().string();
            std::string path_str = entry.path().string();
            int hyphens = 0;
            for (char c : dname) if (c == '-') hyphens++;
            if (hyphens >= 3 &&
                path_str.find("Banks") == std::string::npos &&
                path_str.find("Crash") == std::string::npos &&
                path_str.find("Desync") == std::string::npos &&
                path_str.find("Error") == std::string::npos) {
                user_handles.insert(dname);
            }
        }
    }
}

bool AppState::HasUserHandle(const std::string& handle) {
    std::lock_guard<std::mutex> lock(user_handles_mutex);
    return user_handles.find(handle) != user_handles.end();
}

void AppState::StopScan() {
    m_stop_scan_flag = true;
    m_stop_repair_flag = true;
    if (m_scan_thread.joinable()) {
        m_scan_thread.join();
    }
    if (m_repair_thread.joinable()) {
        m_repair_thread.join();
    }
    is_scanning = false;
}

void AppState::StartScanAsync(bool force_reparse) {
    if (is_scanning) return;
    StopScan();

    m_stop_scan_flag = false;
    is_scanning = true;
    scan_progress = 0;
    scan_total = 0;

    m_scan_thread = std::thread([this, force_reparse]() {
        std::wstring root_w = Win32Utils::Utf8ToWide(account_folder);
        if (!std::filesystem::exists(root_w)) {
            is_scanning = false;
            return;
        }

        {
            std::lock_guard<std::mutex> lock(scan_status_mutex);
            scan_status_message = "Finding replay files...";
        }

        std::vector<std::filesystem::path> replay_paths;
        try {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(root_w, std::filesystem::directory_options::skip_permission_denied)) {
                if (m_stop_scan_flag) break;
                if (entry.is_regular_file()) {
                    auto ext = entry.path().extension().string();
                    for (auto& c : ext) c = tolower(c);
                    if (ext == ".sc2replay") {
                        replay_paths.push_back(entry.path());
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Directory scan exception: " << e.what() << std::endl;
        }

        scan_total = static_cast<int>(replay_paths.size());
        int processed = 0;
        std::vector<ReplayRecord> batch;

        for (const auto& p : replay_paths) {
            if (m_stop_scan_flag) break;

            std::string u8path = Win32Utils::WideToUtf8(p.wstring());

            // Check if parsing is needed
            if (force_reparse || !Database::Instance().HasValidReplay(u8path)) {
                auto parsed = ReplayParser::ParseFile(p.wstring());
                if (parsed.has_value()) {
                    batch.push_back(parsed.value());
                    if (batch.size() >= 30) {
                        Database::Instance().BatchSaveReplays(batch);
                        batch.clear();
                    }
                }
            }

            processed++;
            scan_progress = processed;

            if (processed % 20 == 0) {
                std::lock_guard<std::mutex> lock(scan_status_mutex);
                scan_status_message = "Parsed " + std::to_string(processed) + " / " + std::to_string(scan_total.load()) + " replays";
            }
        }

        if (!batch.empty()) {
            Database::Instance().BatchSaveReplays(batch);
            batch.clear();
        }

        {
            std::lock_guard<std::mutex> lock(scan_status_mutex);
            scan_status_message = "Scan complete (" + std::to_string(scan_total.load()) + " total replays)";
        }
        is_scanning = false;
    });
}

void AppState::AutoRepairCorruptedReplaysAsync() {
    if (m_repair_thread.joinable()) {
        m_stop_repair_flag = true;
        m_repair_thread.join();
    }
    m_stop_repair_flag = false;
    m_repair_thread = std::thread([this]() {
        auto corrupted = Database::Instance().GetCorruptedReplayPaths();
        if (corrupted.empty() || m_stop_repair_flag) return;

        std::vector<ReplayRecord> batch;
        for (const auto& u8path : corrupted) {
            if (m_stop_repair_flag) break;
            std::wstring wpath = Win32Utils::Utf8ToWide(u8path);
            if (std::filesystem::exists(wpath)) {
                auto parsed = ReplayParser::ParseFile(wpath);
                if (parsed.has_value()) {
                    batch.push_back(parsed.value());
                    if (batch.size() >= 30) {
                        Database::Instance().BatchSaveReplays(batch);
                        batch.clear();
                    }
                }
            }
        }
        if (!batch.empty() && !m_stop_repair_flag) {
            Database::Instance().BatchSaveReplays(batch);
            batch.clear();
        }

        if (!m_stop_repair_flag) {
            // Clamp any remaining invalid values in DB (e.g. deleted files)
            Database::Instance().SanitizeInvalidPrestiges();
        }
    });
}

void AppState::OpenPlayerNoteModal(const std::string& player_name) {
    if (player_name.empty()) return;
    note_target_player = Win32Utils::CleanPlayerName(player_name);
    std::string existing = Database::Instance().GetPlayerNote(note_target_player);
    memset(note_edit_buf, 0, sizeof(note_edit_buf));
    strncpy_s(note_edit_buf, existing.c_str(), sizeof(note_edit_buf) - 1);

    // Populate modal data ONCE on click to ensure silky smooth 144+ FPS modal rendering
    note_modal_data.valid = true;
    note_modal_data.player_name = note_target_player;
    int dummy_total = 0;
    auto players = Database::Instance().QueryPlayers(note_target_player, "total_games", false, 1, 0, dummy_total);
    if (!players.empty() && players[0].name == note_target_player) {
        note_modal_data.total_games = players[0].total_games;
        note_modal_data.wins = players[0].wins;
        note_modal_data.losses = players[0].losses;
        note_modal_data.winrate = players[0].winrate;
        note_modal_data.median_apm = players[0].median_apm;
    } else {
        note_modal_data.total_games = 0;
        note_modal_data.wins = 0;
        note_modal_data.losses = 0;
        note_modal_data.winrate = 0.0;
        note_modal_data.median_apm = 0;
    }

    note_modal_data.recent_matches.clear();
    int rec_dummy = 0;
    auto recent_replays = Database::Instance().QueryReplays(note_target_player, 4, 0, rec_dummy);
    for (const auto& r : recent_replays) {
        NoteModalRecentMatch rm;
        rm.game_date = r.game_date;
        rm.map_name = r.map_name;
        rm.commander = (r.p2.name == note_target_player) ? r.p2.commander : r.p1.commander;
        rm.difficulty = r.difficulty;
        rm.result = r.result;
        note_modal_data.recent_matches.push_back(std::move(rm));
    }

    show_note_modal = true;
}

} // namespace sc2
