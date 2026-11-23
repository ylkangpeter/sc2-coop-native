#include "app_state.h"
#include "i18n/i18n.h"
#include "db/database.h"
#include "replay/replay_parser.h"
#include "replay/replay_scanner.h"
#include "utils/win32_utils.h"

#include "data/sc2_constants.h"
#include <random>
#include <windows.h>
#include <filesystem>
#include <algorithm>
#include <iostream>

namespace sc2 {

AppState& AppState::Instance() {
    static AppState state;
    return state;
}

void AppState::RollRandomCommander() {
    if (!hud_randomizer) {
        session_random_commander.clear();
        session_random_prestige.clear();
        return;
    }
    static std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<size_t> pick(0, kCommanders.size() - 1);
    do {
        session_random_commander = kCommanders[pick(generator)];
    } while (session_random_commander == "Horner"); // Alias of HanHorner.
    const auto& names = kPrestiges.at(session_random_commander);
    const std::array prestiges{names.p0, names.p1, names.p2, names.p3};
    session_random_prestige = prestiges[std::uniform_int_distribution<int>(0, 3)(generator)];
}

AppState::~AppState() {
    StopScan();
}

void AppState::Init() {
    if (!Database::Instance().Open("coop_data.db")) {
        throw std::runtime_error("Cannot open coop_data.db. Check disk space and write access. "
                                 "If damaged, close the app and back up the database and its -wal/-shm files "
                                 "before moving them aside to rebuild from the original replays.");
    }
    LoadSettings();

    // Keep the configured root and its queue when a drive is temporarily offline.
    if (GetAccountFolder().empty()) {
        SetAccountFolder(Win32Utils::FindDefaultAccountsFolder());
        SaveSettings();
    } else {
        RefreshUserHandles();
    }

    // The live watcher owns automatic startup catch-up and notifications.
    // Avoid a second scanner enumerating/parsing the same startup increment.
    AutoRepairCorruptedReplaysAsync();
}

std::string AppState::GetAccountFolder() const {
    std::lock_guard<std::mutex> lock(m_settings_mutex);
    return account_folder;
}

void AppState::SetAccountFolder(const std::string& folder) {
    std::lock_guard<std::mutex> lock(m_settings_mutex);
    account_folder = folder;
}

void AppState::LoadSettings() {
    std::lock_guard<std::mutex> lock(m_settings_mutex);
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

    hud_show_charts = db.GetSetting("hud_show_charts", "1") == "1";
    hud_show_total_kills = db.GetSetting("hud_show_total_kills", "0") == "1";
    hud_show_session = db.GetSetting("hud_show_session", "1") == "1";
    hud_randomizer = db.GetSetting("hud_randomizer", "0") == "1";
    RollRandomCommander();
    std::string hd = db.GetSetting("hud_duration", "15");
    try { hud_duration_seconds = std::stoi(hd); } catch (...) { hud_duration_seconds = 15; }

    toast_show_border = (db.GetSetting("toast_show_border", "1") == "1");
    std::string toast_opacity = db.GetSetting("toast_opacity_percent", "88");
    try {
        toast_opacity_percent = std::clamp(std::stoi(toast_opacity), 0, 100);
    } catch (...) {
        toast_opacity_percent = 88;
    }
}

void AppState::SaveSettings() {
    std::lock_guard<std::mutex> lock(m_settings_mutex);
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
    db.SetSetting("hud_show_charts", hud_show_charts ? "1" : "0");
    db.SetSetting("hud_show_total_kills", hud_show_total_kills ? "1" : "0");
    db.SetSetting("hud_show_session", hud_show_session ? "1" : "0");
    db.SetSetting("hud_randomizer", hud_randomizer ? "1" : "0");
    db.SetSetting("hud_duration", std::to_string(hud_duration_seconds));
    db.SetSetting("toast_show_border", toast_show_border ? "1" : "0");
    db.SetSetting("toast_opacity_percent", std::to_string(std::clamp(toast_opacity_percent, 0, 100)));

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
    ReplayScanner::Instance().Stop();
}

void AppState::StartScanAsync(bool force_reparse) {
    ReplayScanner::Instance().StartScan(force_reparse, true);
}

void AppState::AutoRepairCorruptedReplaysAsync() {
    ReplayScanner::Instance().StartRepair();
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
