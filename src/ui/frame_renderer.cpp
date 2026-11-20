#include "frame_renderer.h"
#include "../replay/replay_watcher.h"
#include "../app_state.h"
#include "../i18n/i18n.h"
#include "tabs/tab_settings.h"
#include "tabs/tab_games.h"
#include "tabs/tab_players.h"
#include "tabs/tab_weeklies.h"
#include "tabs/tab_stats.h"
#include "modals/modal_game_detail.h"
#include "modals/modal_chat_view.h"
#include "modals/modal_player_note.h"
#include "hud/summary_overlay.h"
#include "../utils/hotkey_manager.h"
#include <imgui.h>
#include <string>
#include <cstdio>

namespace sc2 {

void FrameRenderer::RenderFrame() {
    auto& state = AppState::Instance();

    // Handle requested tab switch
    if (state.request_tab_switch >= 0) {
        state.active_tab = state.request_tab_switch;
        state.request_tab_switch = -1;
    }

    // Full window viewport
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));

    const ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar |
                                          ImGuiWindowFlags_NoCollapse |
                                          ImGuiWindowFlags_NoResize |
                                          ImGuiWindowFlags_NoMove |
                                          ImGuiWindowFlags_NoBringToFrontOnFocus |
                                          ImGuiWindowFlags_NoNavFocus |
                                          ImGuiWindowFlags_NoScrollbar |
                                          ImGuiWindowFlags_NoScrollWithMouse |
                                          ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("MainWindow", nullptr, window_flags);
    ImGui::PopStyleVar(3);

    // Header Navigation Bar
    ImGui::TextColored(ImVec4(0.0f, 0.75f, 1.0f, 1.0f), "SC2 CO-OP NATIVE");
    ImGui::SameLine();
    ImGui::TextDisabled("v1.0.17");
    ImGui::SameLine(0, 16.0f);

    // Tab Buttons
    auto TabBtn = [&](const char* label, int tab_idx, float width = 0.0f) {
        bool is_active = (state.active_tab == tab_idx);
        if (is_active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.45f, 0.85f, 1.0f));
        }
        if (ImGui::Button(label, ImVec2(width, 28))) {
            state.active_tab = tab_idx;
        }
        if (is_active) {
            ImGui::PopStyleColor();
        }
    };

    TabBtn(TR("Games"), 1, 0.0f);
    ImGui::SameLine();
    TabBtn(TR("Players"), 2, 0.0f);
    ImGui::SameLine();
    TabBtn(TR("Weeklies"), 3, 0.0f);
    ImGui::SameLine();
    TabBtn(TR("Statistics"), 4, 0.0f);
    ImGui::SameLine();
    TabBtn(TR("Settings"), 0, 0.0f);

    // Separator between navigation tabs and action floating HUD toggle
    ImGui::SameLine(0, 14.0f);
    ImGui::TextDisabled("|");
    ImGui::SameLine(0, 14.0f);

    // Summary Overlay action button
    bool overlay_active = SummaryOverlay::Instance().IsActive();
    if (overlay_active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.45f, 0.50f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.55f, 0.60f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.65f, 0.70f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 1.0f, 0.90f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.35f, 0.90f, 0.85f, 0.8f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.20f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.32f, 0.44f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.28f, 0.42f, 0.58f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.90f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.30f, 0.45f, 0.65f, 0.6f));
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

    std::string sum_btn_label = std::string(overlay_active ? "◆ " : "◇ ") + TR("Summary Overlay") + " [" + HotkeyManager::FormatHotkey(state.hotkey_summary_mod, state.hotkey_summary_vk) + "]";
    if (ImGui::Button(sum_btn_label.c_str(), ImVec2(0.0f, 28))) {
        SummaryOverlay::Instance().Toggle();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "%s", TR("Summary Overlay"));
        ImGui::Separator();
        ImGui::TextUnformatted(TR("Toggle Match Summary Overlay"));
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(5);

    // Right side: Scanner status & Rescan indicator
    const auto imported = ReplayWatcher::Instance().GetImportProgress();
    const bool manual_scan = state.is_scanning.load();
    const bool scanning = manual_scan || imported.total > 0;
    char scan_buf[128];
    const char* status = TR("Idle");
    if (scanning) {
        std::snprintf(scan_buf, sizeof(scan_buf), "%s (%d/%d)...", TR("Scanning"),
                      manual_scan ? state.scan_progress.load() : imported.completed,
                      manual_scan ? state.scan_total.load() : imported.total);
        status = scan_buf;
    }
    const float target_x = ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(status).x;
    if (target_x >= ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + ImGui::GetStyle().ItemSpacing.x) {
        ImGui::SameLine(target_x);
    } else {
        ImGui::SetCursorPosX(target_x);
    }
    if (scanning) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", status);
    else ImGui::TextDisabled("%s", status);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Render Active Tab
    switch (state.active_tab) {
        case 0: TabSettings::Render(state); break;
        case 1: TabGames::Render(state); break;
        case 2: TabPlayers::Render(state); break;
        case 3: TabWeeklies::Render(state); break;
        case 4: TabStats::Render(state); break;
        default: break;
    }

    // Render Modals
    ModalGameDetail::Render(state);
    ModalChatView::Render(state);
    ModalPlayerNote::Render(state);

    ImGui::End();
}

} // namespace sc2
