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
    if (folder_buf[0] == '\0' && !state.account_folder.empty()) {
        strncpy_s(folder_buf, state.account_folder.c_str(), sizeof(folder_buf) - 1);
    }

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 220);
    if (ImGui::InputText("##AccountFolder", folder_buf, sizeof(folder_buf))) {
        state.account_folder = folder_buf;
        state.SaveSettings();
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Browse..."), ImVec2(90, 0))) {
        std::string chosen = Win32Utils::SelectFolderDialog(nullptr, TR("StarCraft II Replays & Account Directory"));
        if (!chosen.empty()) {
            state.account_folder = chosen;
            strncpy_s(folder_buf, chosen.c_str(), sizeof(folder_buf) - 1);
            state.SaveSettings();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Auto Detect"), ImVec2(100, 0))) {
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
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", TR("Replay Library & Database"));
    ImGui::Spacing();

    int total_db_replays = Database::Instance().GetTotalReplayCount();
    ImGui::Text(TR("Total Replays Indexed in Database: %d"), total_db_replays);

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
        if (ImGui::Button(TR("Rescan Library Now"), ImVec2(160, 26))) {
            state.StartScanAsync(false);
        }
        ImGui::SameLine();
        if (ImGui::Button(TR("Re-parse All Replays"), ImVec2(170, 26))) {
            state.StartScanAsync(true);
        }
        ImGui::SameLine();
        if (ImGui::Button(TR("Export Database to JSON"), ImVec2(180, 26))) {
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
    ImGui::SameLine(460);
    if (ImGui::Button(TR("Toggle Summary Preview"), ImVec2(180, 24))) {
        SummaryOverlay::Instance().Toggle();
    }

    if (ImGui::Checkbox(TR("Show in-game Ally Winrate & Notes Toast on match start"), &state.show_ally_hud)) {
        state.SaveSettings();
    }
    ImGui::SameLine(460);
    if (ImGui::Button(TR("Toggle Ally Toast Preview"), ImVec2(180, 24))) {
        AllyToastWindow::Instance().Toggle();
    }

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

    static int s_recording_action = -1; // 1 = Summary, 2 = Ally, 3 = Prev, 4 = Next
    static bool s_skip_first_frame = false;

    // Check key presses when in recording mode
    if (s_recording_action > 0) {
        if (s_skip_first_frame) {
            s_skip_first_frame = false;
        } else {
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
                s_recording_action = -1; // Cancel
            } else {
                bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
                bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                bool win = ((GetAsyncKeyState(VK_LWIN) & 0x8000) != 0) || ((GetAsyncKeyState(VK_RWIN) & 0x8000) != 0);

                UINT pressed_vk = 0;
                // Loop through standard keys (skip mouse buttons 1-7 and modifiers)
                for (int k = 8; k <= 255; ++k) {
                    if (k == VK_CONTROL || k == VK_LCONTROL || k == VK_RCONTROL ||
                        k == VK_MENU || k == VK_LMENU || k == VK_RMENU ||
                        k == VK_SHIFT || k == VK_LSHIFT || k == VK_RSHIFT ||
                        k == VK_LWIN || k == VK_RWIN || k == VK_ESCAPE) {
                        continue;
                    }
                    if ((GetAsyncKeyState(k) & 0x8000) != 0) {
                        pressed_vk = static_cast<UINT>(k);
                        break;
                    }
                }

                if (pressed_vk != 0) {
                    UINT mod = (ctrl ? MOD_CONTROL : 0) | (alt ? MOD_ALT : 0) | (shift ? MOD_SHIFT : 0) | (win ? MOD_WIN : 0);
                    if (s_recording_action == 1) {
                        state.hotkey_summary_mod = mod;
                        state.hotkey_summary_vk = pressed_vk;
                    } else if (s_recording_action == 2) {
                        state.hotkey_ally_mod = mod;
                        state.hotkey_ally_vk = pressed_vk;
                    } else if (s_recording_action == 3) {
                        state.hotkey_prev_mod = mod;
                        state.hotkey_prev_vk = pressed_vk;
                    } else if (s_recording_action == 4) {
                        state.hotkey_next_mod = mod;
                        state.hotkey_next_vk = pressed_vk;
                    }
                    state.SaveSettings();
                    HotkeyManager::Instance().RegisterAll();
                    s_recording_action = -1;
                }
            }
        }
    }

    auto RenderHotkeyRow = [&](const char* label_key, bool& enabled, UINT mod, UINT vk, int action_id) {
        if (ImGui::Checkbox(TR(label_key), &enabled)) {
            state.SaveSettings();
            HotkeyManager::Instance().RegisterAll();
        }
        ImGui::SameLine(360);

        std::string btn_label;
        if (s_recording_action == action_id) {
            btn_label = std::string("[ ") + TR("Press key combo... (Esc to cancel)") + " ]";
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        } else {
            btn_label = "[ " + HotkeyManager::FormatHotkey(mod, vk) + " ]";
        }

        std::string btn_id = btn_label + "##HKBtn" + std::to_string(action_id);
        if (ImGui::Button(btn_id.c_str(), ImVec2(240, 24))) {
            if (s_recording_action == action_id) {
                s_recording_action = -1;
            } else {
                s_recording_action = action_id;
                s_skip_first_frame = true;
            }
        }

        if (s_recording_action == action_id) {
            ImGui::PopStyleColor(2);
        }
    };

    RenderHotkeyRow("Toggle Match Summary Overlay", state.hotkey_summary_enabled, state.hotkey_summary_mod, state.hotkey_summary_vk, 1);
    RenderHotkeyRow("Toggle Ally Winrate & Notes Toast", state.hotkey_ally_enabled, state.hotkey_ally_mod, state.hotkey_ally_vk, 2);
    RenderHotkeyRow("Browse Older Match Summary", state.hotkey_nav_enabled, state.hotkey_prev_mod, state.hotkey_prev_vk, 3);
    RenderHotkeyRow("Browse Newer Match Summary", state.hotkey_nav_enabled, state.hotkey_next_mod, state.hotkey_next_vk, 4);

    ImGui::Spacing();
    if (ImGui::Button(TR("Reset Default Hotkeys"), ImVec2(180, 25))) {
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
        s_recording_action = -1;
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
    ImGui::SameLine(250);
    if (ImGui::Checkbox(TR("Start Minimized"), &state.start_minimized)) {
        state.SaveSettings();
    }

    if (ImGui::Checkbox(TR("Minimize to System Tray on Close"), &state.minimize_to_tray)) {
        state.SaveSettings();
    }
    ImGui::SameLine(250);
    if (ImGui::Checkbox(TR("Enable App Logging"), &state.enable_logging)) {
        state.SaveSettings();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 6. About & Version Info
    ImGui::TextDisabled("StarCraft II Co-op Native v1.0.0 (DirectX 11 + Dear ImGui + SQLite3)");
    ImGui::TextDisabled("%s", TR("About_Description"));
}

} // namespace sc2
