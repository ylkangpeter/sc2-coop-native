#include "app_state.h"
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
    start_minimized = (db.GetSetting("start_minimized", "0") == "1");
    minimize_to_tray = (db.GetSetting("minimize_to_tray", "1") == "1");
    start_with_windows = (db.GetSetting("start_with_windows", "0") == "1");
    enable_logging = (db.GetSetting("enable_logging", "1") == "1");
    show_ally_hud = (db.GetSetting("show_ally_hud", "1") == "1");
    auto_show_summary = (db.GetSetting("auto_show_summary", "1") == "1");
    hotkey_summary_enabled = (db.GetSetting("hotkey_summary_enabled", "1") == "1");
    hotkey_nav_enabled = (db.GetSetting("hotkey_nav_enabled", "1") == "1");
    hotkey_ally_enabled = (db.GetSetting("hotkey_ally_enabled", "1") == "1");

    std::string fs = db.GetSetting("font_scale", "1.0");
    try { font_scale = std::stof(fs); } catch (...) { font_scale = 1.0f; }

    std::string hd = db.GetSetting("hud_duration", "15");
    try { hud_duration_seconds = std::stoi(hd); } catch (...) { hud_duration_seconds = 15; }
}

void AppState::SaveSettings() {
    auto& db = Database::Instance();
    db.SetSetting("account_folder", account_folder);
    db.SetSetting("start_minimized", start_minimized ? "1" : "0");
    db.SetSetting("minimize_to_tray", minimize_to_tray ? "1" : "0");
    db.SetSetting("start_with_windows", start_with_windows ? "1" : "0");
    db.SetSetting("enable_logging", enable_logging ? "1" : "0");
    db.SetSetting("show_ally_hud", show_ally_hud ? "1" : "0");
    db.SetSetting("auto_show_summary", auto_show_summary ? "1" : "0");
    db.SetSetting("hotkey_summary_enabled", hotkey_summary_enabled ? "1" : "0");
    db.SetSetting("hotkey_nav_enabled", hotkey_nav_enabled ? "1" : "0");
    db.SetSetting("hotkey_ally_enabled", hotkey_ally_enabled ? "1" : "0");
    db.SetSetting("font_scale", std::to_string(font_scale));
    db.SetSetting("hud_duration", std::to_string(hud_duration_seconds));

    // Update Windows run key
    wchar_t exe_buf[MAX_PATH];
    GetModuleFileNameW(nullptr, exe_buf, MAX_PATH);
    Win32Utils::SetAutoStart(start_with_windows, "SC2CoopNative", Win32Utils::WideToUtf8(exe_buf));
}

void AppState::StopScan() {
    m_stop_scan_flag = true;
    if (m_scan_thread.joinable()) {
        m_scan_thread.join();
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
    std::thread([this]() {
        auto corrupted = Database::Instance().GetCorruptedReplayPaths();
        if (corrupted.empty()) return;

        std::vector<ReplayRecord> batch;
        for (const auto& u8path : corrupted) {
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
        if (!batch.empty()) {
            Database::Instance().BatchSaveReplays(batch);
            batch.clear();
        }

        // Clamp any remaining invalid values in DB (e.g. deleted files)
        Database::Instance().SanitizeInvalidPrestiges();
    }).detach();
}

} // namespace sc2
