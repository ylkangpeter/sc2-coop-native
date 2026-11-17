#include "tab_settings.h"
#include "../../app_state.h"
#include "../../i18n/i18n.h"
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
    // 0. Language Selector (at the very top)
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", TR("Interface Language"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220);

    const auto& available_langs = I18n::GetAvailableLanguages();
    std::vector<const char*> lang_options;
    for (const auto& l : available_langs) {
        lang_options.push_back(l.name.c_str());
    }

    int current_lang = I18n::GetCurrentLanguageIndex();
    if (!lang_options.empty() && ImGui::Combo("##LanguageSwitcher", &current_lang, lang_options.data(), static_cast<int>(lang_options.size()))) {
        I18n::SetLanguageByIndex(current_lang);
        state.language_code = I18n::GetCurrentLanguageCode();
        state.language = current_lang;
        state.SaveSettings();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("%s", TR("Settings_Subtitle"));
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 1. Account Folder Section
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", TR("StarCraft II Replays & Account Directory"));
    ImGui::Spacing();

    static char folder_buf[512] = {0};
    std::string current_folder = state.GetAccountFolder();
    if (folder_buf[0] == '\0' && !current_folder.empty()) {
        strncpy_s(folder_buf, current_folder.c_str(), sizeof(folder_buf) - 1);
    }

    float browse_btn_w = ImGui::CalcTextSize(TR("Browse...")).x + ImGui::GetStyle().FramePadding.x * 2.0f + 16.0f;
    float auto_btn_w = ImGui::CalcTextSize(TR("Auto Detect")).x + ImGui::GetStyle().FramePadding.x * 2.0f + 16.0f;
    ImGui::SetNextItemWidth(std::max(200.0f, ImGui::GetContentRegionAvail().x - browse_btn_w - auto_btn_w - ImGui::GetStyle().ItemSpacing.x * 2.0f));
    ImGui::InputText("##AccountFolder", folder_buf, sizeof(folder_buf));
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        state.SetAccountFolder(folder_buf);
        state.SaveSettings();
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Browse..."), ImVec2(browse_btn_w, 0))) {
        std::string chosen = Win32Utils::SelectFolderDialog(nullptr, TR("StarCraft II Replays & Account Directory"));
        if (!chosen.empty()) {
            state.SetAccountFolder(chosen);
            strncpy_s(folder_buf, chosen.c_str(), sizeof(folder_buf) - 1);
            state.SaveSettings();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Auto Detect"), ImVec2(auto_btn_w, 0))) {
        std::string detected = Win32Utils::FindDefaultAccountsFolder();
        if (!detected.empty()) {
            state.SetAccountFolder(detected);
            strncpy_s(folder_buf, detected.c_str(), sizeof(folder_buf) - 1);
            state.SaveSettings();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 2. Replay Scanner Status & Controls
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", TR("Replay Library & Database"));
    ImGui::Spacing();

    static int s_cached_total_db_replays = 0;
    static double s_last_count_check = 0.0;
    double current_time = ImGui::GetTime();
    if (current_time - s_last_count_check > 1.5 || s_last_count_check == 0.0) {
        s_cached_total_db_replays = Database::Instance().GetTotalReplayCount();
        s_last_count_check = current_time;
    }
    int total_db_replays = s_cached_total_db_replays;
    ImGui::Text(I18n::GetFormat("Total Replays Indexed in Database: %d", "Total Replays Indexed in Database: %d"), total_db_replays);

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
        if (ImGui::Button(TR("Stop Scan"), ImVec2(90, 22))) {
            state.StopScan();
        }
    } else {
        if (ImGui::Button(TR("Rescan Library Now"), ImVec2(0, 28))) {
            state.StartScanAsync(false);
        }
        ImGui::SameLine();
        if (ImGui::Button(TR("Re-parse All Replays"), ImVec2(0, 28))) {
            state.StartScanAsync(true);
        }
        ImGui::SameLine();
        if (ImGui::Button(TR("Export Database to JSON"), ImVec2(0, 28))) {
            Database::Instance().DumpAllToJson("replay_data_dump.json");
            ShellExecuteW(nullptr, L"open", L"explorer.exe", L"/select,replay_data_dump.json", nullptr, SW_SHOWNORMAL);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 3. Post-Game Summary & In-Game Overlay
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", TR("Post-Game Summary & In-Game Overlay"));
    ImGui::Spacing();

    if (ImGui::Checkbox(TR("Auto-show Match Summary Overlay after each game"), &state.auto_show_summary)) {
        state.SaveSettings();
    }
    ImGui::Indent(24.0f);
    if (ImGui::Button(TR("Toggle Summary Preview"), ImVec2(0, 28))) {
        SummaryOverlay::Instance().Toggle();
    }
    ImGui::Unindent(24.0f);
    ImGui::Spacing();

    if (ImGui::Checkbox(TR("Show in-game Ally Winrate & Notes Toast on match start"), &state.show_ally_hud)) {
        state.SaveSettings();
    }
    ImGui::Indent(24.0f);
    if (ImGui::Button(TR("Toggle Ally Toast Preview"), ImVec2(0, 28))) {
        AllyToastWindow::Instance().Toggle();
    }
    ImGui::Unindent(24.0f);
    ImGui::Spacing();

    if (ImGui::Checkbox(TR("Show Toast Border"), &state.toast_show_border)) {
        state.SaveSettings();
        AllyToastWindow::Instance().Refresh();
    }

    ImGui::SetNextItemWidth(220);
    if (ImGui::SliderInt(TR("Toast Opacity (%)"), &state.toast_opacity_percent, 0, 100)) {
        state.SaveSettings();
        AllyToastWindow::Instance().Refresh();
    }
    ImGui::Spacing();

    ImGui::SetNextItemWidth(200);
    if (ImGui::SliderInt(TR("Overlay Display Duration (seconds)"), &state.hud_duration_seconds, 5, 60)) {
        state.SaveSettings();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 4. Global Hotkeys Section (Customizable, NO F10/F11 defaults)
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", TR("Global System Hotkeys (In-Game & Desktop)"));
    ImGui::Spacing();
    ImGui::TextDisabled("%s", TR("Hotkey_Description"));
    ImGui::Spacing();

    if (ImGui::BeginTable("HotkeysTable", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(TR("Action"), ImGuiTableColumnFlags_WidthStretch, 0.45f);
        ImGui::TableSetupColumn(TR("Hotkey"), ImGuiTableColumnFlags_WidthStretch, 0.55f);

        auto RenderHotkeyRow = [&](const char* label_key, bool& enabled, UINT mod, UINT vk, int action_id) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (ImGui::Checkbox(TR(label_key), &enabled)) {
                state.SaveSettings();
                HotkeyManager::Instance().RegisterAll();
            }

            ImGui::TableSetColumnIndex(1);
            bool is_pushed = (HotkeyManager::Instance().GetRecordingAction() == action_id);
            std::string btn_label;
            if (is_pushed) {
                btn_label = std::string("[ ") + TR("Press key combo... (Esc to cancel)") + " ]";
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            } else {
                btn_label = "[ " + HotkeyManager::FormatHotkey(mod, vk) + " ]";
            }

            std::string btn_id = btn_label + "##HKBtn" + std::to_string(action_id);
            float btn_w = std::max(200.0f, ImGui::CalcTextSize(btn_label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f + 16.0f);
            if (ImGui::Button(btn_id.c_str(), ImVec2(btn_w, 28))) {
                if (is_pushed) {
                    HotkeyManager::Instance().StopRecording();
                } else {
                    HotkeyManager::Instance().StartRecording(action_id);
                }
            }

            if (is_pushed) {
                ImGui::PopStyleColor(2);
            }
        };

        RenderHotkeyRow("Toggle Match Summary Overlay", state.hotkey_summary_enabled, state.hotkey_summary_mod, state.hotkey_summary_vk, 1);
        RenderHotkeyRow("Toggle Ally Winrate & Notes Toast", state.hotkey_ally_enabled, state.hotkey_ally_mod, state.hotkey_ally_vk, 2);
        RenderHotkeyRow("Browse Older Match Summary", state.hotkey_nav_enabled, state.hotkey_prev_mod, state.hotkey_prev_vk, 3);
        RenderHotkeyRow("Browse Newer Match Summary", state.hotkey_nav_enabled, state.hotkey_next_mod, state.hotkey_next_vk, 4);

        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (ImGui::Button(TR("Reset Default Hotkeys"), ImVec2(0, 28))) {
        state.hotkey_summary_mod = MOD_CONTROL | MOD_SHIFT;
        state.hotkey_summary_vk = 'H';
        state.hotkey_ally_mod = MOD_CONTROL | MOD_ALT;
        state.hotkey_ally_vk = 'W';
        state.hotkey_prev_mod = MOD_CONTROL | MOD_ALT;
        state.hotkey_prev_vk = VK_LEFT;
        state.hotkey_next_mod = MOD_CONTROL | MOD_ALT;
        state.hotkey_next_vk = VK_RIGHT;
        state.SaveSettings();
        HotkeyManager::Instance().RegisterAll();
        HotkeyManager::Instance().StopRecording();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 5. System Preferences
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", TR("Preferences & System Integration"));
    ImGui::Spacing();

    if (ImGui::Checkbox(TR("Start with Windows"), &state.start_with_windows)) {
        state.SaveSettings();
    }
    if (ImGui::Checkbox(TR("Start Minimized"), &state.start_minimized)) {
        state.SaveSettings();
    }
    if (ImGui::Checkbox(TR("Minimize to System Tray on Close"), &state.minimize_to_tray)) {
        state.SaveSettings();
    }
    if (ImGui::Checkbox(TR("Enable App Logging"), &state.enable_logging)) {
        state.SaveSettings();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 6. About & Version Info
    ImGui::TextDisabled("StarCraft II Co-op Native v1.0.14 (DirectX 11 + Dear ImGui + SQLite3)");
    ImGui::TextDisabled("%s", TR("About_Description"));
}

} // namespace sc2
