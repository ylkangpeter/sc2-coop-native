#include "tab_settings.h"
#include "../../app_state.h"
#include "../../db/database.h"
#include "../../utils/win32_utils.h"
#include "../../utils/hotkey_manager.h"
#include "../hud/summary_overlay.h"
#include "../hud/ally_toast.h"

#include <imgui.h>
#include <windows.h>
#include <shellapi.h>

namespace sc2 {

void TabSettings::Render(AppState& state) {
    ImGui::TextDisabled("Configure paths, background scanner, and system preferences.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 1. Account Folder Section
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "StarCraft II Replays & Account Directory");
    ImGui::Spacing();

    static char folder_buf[512] = {0};
    if (folder_buf[0] == '\0' && !state.account_folder.empty()) {
        strncpy_s(folder_buf, state.account_folder.c_str(), sizeof(folder_buf) - 1);
    }

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 220);
    if (ImGui::InputText("##AccountFolder", folder_buf, sizeof(folder_buf))) {
        state.account_folder = folder_buf;
        state.SaveSettings();
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse...", ImVec2(90, 0))) {
        std::string chosen = Win32Utils::SelectFolderDialog(nullptr, "Select StarCraft II Accounts Folder");
        if (!chosen.empty()) {
            state.account_folder = chosen;
            strncpy_s(folder_buf, chosen.c_str(), sizeof(folder_buf) - 1);
            state.SaveSettings();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Auto Detect", ImVec2(100, 0))) {
        std::string detected = Win32Utils::FindDefaultAccountsFolder();
        if (!detected.empty()) {
            state.account_folder = detected;
            strncpy_s(folder_buf, detected.c_str(), sizeof(folder_buf) - 1);
            state.SaveSettings();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 2. Replay Scanner Status & Controls
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Replay Library & Database");
    ImGui::Spacing();

    int total_db_replays = Database::Instance().GetTotalReplayCount();
    ImGui::Text("Total Replays Indexed in Database: %d", total_db_replays);

    if (state.is_scanning) {
        int prog = state.scan_progress.load();
        int tot = state.scan_total.load();
        float fraction = (tot > 0) ? (static_cast<float>(prog) / tot) : 0.0f;

        std::string status;
        {
            std::lock_guard<std::mutex> lock(state.scan_status_mutex);
            status = state.scan_status_message;
        }

        ImGui::ProgressBar(fraction, ImVec2(350, 22), status.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Stop Scan", ImVec2(90, 22))) {
            state.StopScan();
        }
    } else {
        if (ImGui::Button("Rescan Library Now", ImVec2(160, 26))) {
            state.StartScanAsync(false);
        }
        ImGui::SameLine();
        if (ImGui::Button("Re-parse All Replays", ImVec2(170, 26))) {
            state.StartScanAsync(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Export Database to JSON", ImVec2(180, 26))) {
            Database::Instance().DumpAllToJson("replay_data_dump.json");
            ShellExecuteW(nullptr, L"open", L"explorer.exe", L"/select,replay_data_dump.json", nullptr, SW_SHOWNORMAL);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 3. Post-Game Summary & In-Game Overlay
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Post-Game Summary & In-Game Overlay");
    ImGui::Spacing();

    if (ImGui::Checkbox("Auto-show Match Summary Overlay after each game", &state.auto_show_summary)) {
        state.SaveSettings();
    }
    ImGui::SameLine(420);
    if (ImGui::Button("Preview Summary Overlay", ImVec2(180, 24))) {
        SummaryOverlay::Instance().ShowLatest();
    }

    if (ImGui::Checkbox("Show in-game Ally Winrate & Notes Toast on match start", &state.show_ally_hud)) {
        state.SaveSettings();
    }
    ImGui::SameLine(420);
    if (ImGui::Button("Preview Ally Info Toast", ImVec2(180, 24))) {
        AllyToastWindow::Instance().ShowTestToast();
    }

    ImGui::SetNextItemWidth(200);
    if (ImGui::SliderInt("Overlay Display Duration (seconds)", &state.hud_duration_seconds, 5, 60)) {
        state.SaveSettings();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 4. Global Hotkeys (In-Game & Desktop)
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Global System Hotkeys (In-Game & Desktop)");
    ImGui::Spacing();
    ImGui::TextDisabled("These hotkeys work globally while in StarCraft II or on desktop:");

    if (ImGui::Checkbox("Toggle Match Summary Overlay  [ Ctrl + Shift + H  or  F11 ]", &state.hotkey_summary_enabled)) {
        state.SaveSettings();
        HotkeyManager::Instance().RegisterAll();
    }

    if (ImGui::Checkbox("Browse Match Summaries  [ Ctrl + Alt + Left/Right  or  PgDn/PgUp ]", &state.hotkey_nav_enabled)) {
        state.SaveSettings();
        HotkeyManager::Instance().RegisterAll();
    }

    if (ImGui::Checkbox("Show Ally Winrate & Notes Toast  [ Ctrl + Alt + W  or  F10 ]", &state.hotkey_ally_enabled)) {
        state.SaveSettings();
        HotkeyManager::Instance().RegisterAll();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 5. System Preferences
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Preferences & System Integration");
    ImGui::Spacing();

    if (ImGui::Checkbox("Start with Windows", &state.start_with_windows)) {
        state.SaveSettings();
    }
    ImGui::SameLine(250);
    if (ImGui::Checkbox("Start Minimized", &state.start_minimized)) {
        state.SaveSettings();
    }

    if (ImGui::Checkbox("Minimize to System Tray on Close", &state.minimize_to_tray)) {
        state.SaveSettings();
    }
    ImGui::SameLine(250);
    if (ImGui::Checkbox("Enable App Logging", &state.enable_logging)) {
        state.SaveSettings();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 6. About & Version Info
    ImGui::TextDisabled("StarCraft II Co-op Native v1.0.0 (DirectX 11 + Dear ImGui + SQLite3)");
    ImGui::TextDisabled("Fast, offline, zero-telemetry co-op match analyzer and player tracker.");
}

} // namespace sc2
