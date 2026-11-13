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
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", TR("界面语言 / Language", "Interface Language / 界面语言"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180);
    const char* lang_options[] = { "简体中文 (Chinese)", "English" };
    int current_lang = state.language;
    if (ImGui::Combo("##LanguageSwitcher", &current_lang, lang_options, IM_ARRAYSIZE(lang_options))) {
        state.language = current_lang;
        I18n::SetLanguage(static_cast<Language>(state.language));
        state.SaveSettings();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("%s", TR("配置路径、后台扫描器、全局快捷键及系统偏好设置。",
                                 "Configure paths, background scanner, global hotkeys, and system preferences."));
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 1. Account Folder Section
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", TR("星际争霸II战报与账号目录", "StarCraft II Replays & Account Directory"));
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
    if (ImGui::Button(TR("浏览...", "Browse..."), ImVec2(90, 0))) {
        std::string chosen = Win32Utils::SelectFolderDialog(nullptr, TR("选择星际争霸II账号目录", "Select StarCraft II Accounts Folder"));
        if (!chosen.empty()) {
            state.account_folder = chosen;
            strncpy_s(folder_buf, chosen.c_str(), sizeof(folder_buf) - 1);
            state.SaveSettings();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("自动检测", "Auto Detect"), ImVec2(100, 0))) {
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
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", TR("战报库与数据库索引", "Replay Library & Database"));
    ImGui::Spacing();

    int total_db_replays = Database::Instance().GetTotalReplayCount();
    ImGui::Text(TR("已收录战报总数: %d 场", "Total Replays Indexed in Database: %d"), total_db_replays);

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
        if (ImGui::Button(TR("停止扫描", "Stop Scan"), ImVec2(90, 22))) {
            state.StopScan();
        }
    } else {
        if (ImGui::Button(TR("增量扫描新战报", "Rescan Library Now"), ImVec2(160, 26))) {
            state.StartScanAsync(false);
        }
        ImGui::SameLine();
        if (ImGui::Button(TR("重新解析全部战报", "Re-parse All Replays"), ImVec2(170, 26))) {
            state.StartScanAsync(true);
        }
        ImGui::SameLine();
        if (ImGui::Button(TR("导出数据库至JSON", "Export Database to JSON"), ImVec2(180, 26))) {
            Database::Instance().DumpAllToJson("replay_data_dump.json");
            ShellExecuteW(nullptr, L"open", L"explorer.exe", L"/select,replay_data_dump.json", nullptr, SW_SHOWNORMAL);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 3. Post-Game Summary & In-Game Overlay
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", TR("赛后战报与游戏内浮窗", "Post-Game Summary & In-Game Overlay"));
    ImGui::Spacing();

    if (ImGui::Checkbox(TR("每场比赛结束后自动弹出战报浮窗", "Auto-show Match Summary Overlay after each game"), &state.auto_show_summary)) {
        state.SaveSettings();
    }
    ImGui::SameLine(460);
    if (ImGui::Button(TR("呼出/隐藏战报预览", "Toggle Summary Preview"), ImVec2(180, 24))) {
        SummaryOverlay::Instance().Toggle();
    }

    if (ImGui::Checkbox(TR("进游戏或开始时显示盟友胜率与备注信息", "Show in-game Ally Winrate & Notes Toast on match start"), &state.show_ally_hud)) {
        state.SaveSettings();
    }
    ImGui::SameLine(460);
    if (ImGui::Button(TR("呼出/隐藏盟友浮窗", "Toggle Ally Toast Preview"), ImVec2(180, 24))) {
        AllyToastWindow::Instance().Toggle();
    }

    ImGui::SetNextItemWidth(200);
    if (ImGui::SliderInt(TR("浮窗自动驻留时长 (秒)", "Overlay Display Duration (seconds)"), &state.hud_duration_seconds, 5, 60)) {
        state.SaveSettings();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 4. Global Hotkeys Section (Customizable, NO F10/F11 defaults)
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", TR("全局系统快捷键 (游戏内与桌面均有效)", "Global System Hotkeys (In-Game & Desktop)"));
    ImGui::Spacing();
    ImGui::TextDisabled("%s", TR("这些快捷键在星际争霸II运行中或桌面上全局生效。点击右侧按键可直接按下新键进行自定义修改：",
                                 "These hotkeys work globally while in StarCraft II or on desktop. Click to customize:"));
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

    auto RenderHotkeyRow = [&](const char* label_zh, const char* label_en, bool& enabled, UINT mod, UINT vk, int action_id) {
        if (ImGui::Checkbox(TR(label_zh, label_en), &enabled)) {
            state.SaveSettings();
            HotkeyManager::Instance().RegisterAll();
        }
        ImGui::SameLine(360);

        std::string btn_label;
        if (s_recording_action == action_id) {
            btn_label = TR("[ 请按下新按键... (Esc取消) ]", "[ Press keys... (Esc to cancel) ]");
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

    RenderHotkeyRow("呼出/隐藏战报浮窗", "Toggle Match Summary Overlay", state.hotkey_summary_enabled, state.hotkey_summary_mod, state.hotkey_summary_vk, 1);
    RenderHotkeyRow("呼出/隐藏盟友胜率及备注", "Toggle Ally Winrate & Notes Toast", state.hotkey_ally_enabled, state.hotkey_ally_mod, state.hotkey_ally_vk, 2);
    RenderHotkeyRow("翻看上一场比赛战报", "Browse Older Match Summary", state.hotkey_nav_enabled, state.hotkey_prev_mod, state.hotkey_prev_vk, 3);
    RenderHotkeyRow("翻看下一场比赛战报", "Browse Newer Match Summary", state.hotkey_nav_enabled, state.hotkey_next_mod, state.hotkey_next_vk, 4);

    ImGui::Spacing();
    if (ImGui::Button(TR("恢复默认快捷键", "Reset Default Hotkeys"), ImVec2(180, 25))) {
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
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", TR("首选项与系统集成", "Preferences & System Integration"));
    ImGui::Spacing();

    if (ImGui::Checkbox(TR("开机自动启动", "Start with Windows"), &state.start_with_windows)) {
        state.SaveSettings();
    }
    ImGui::SameLine(250);
    if (ImGui::Checkbox(TR("启动时最小化", "Start Minimized"), &state.start_minimized)) {
        state.SaveSettings();
    }

    if (ImGui::Checkbox(TR("关闭主窗口时最小化到系统托盘", "Minimize to System Tray on Close"), &state.minimize_to_tray)) {
        state.SaveSettings();
    }
    ImGui::SameLine(250);
    if (ImGui::Checkbox(TR("启用应用运行日志", "Enable App Logging"), &state.enable_logging)) {
        state.SaveSettings();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 6. About & Version Info
    ImGui::TextDisabled("StarCraft II Co-op Native v1.0.0 (DirectX 11 + Dear ImGui + SQLite3)");
    ImGui::TextDisabled("%s", TR("极速、完全离线、零隐私追踪的星际争霸II合作任务赛后分析与盟友记录工具。",
                                 "Fast, offline, zero-telemetry co-op match analyzer and player tracker."));
}

} // namespace sc2
