#include "modal_player_note.h"
#include "../../app_state.h"
#include "../../db/database.h"
#include "../../i18n/i18n.h"
#include "../../utils/win32_utils.h"

#include <imgui.h>
#include <string>
#include <vector>

namespace sc2 {

void ModalPlayerNote::Render(AppState& state) {
    if (state.show_note_modal) {
        ImGui::OpenPopup(TR("Player Notes & Stats"));
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(650, 520), ImGuiCond_Appearing);

    const ImGuiWindowFlags modal_flags = ImGuiWindowFlags_NoResize |
                                         ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::BeginPopupModal(TR("Player Notes & Stats"), &state.show_note_modal, modal_flags)) {
        const auto& md = state.note_modal_data;
        std::string player_name = state.note_target_player;
        std::string clean_name = Win32Utils::CleanPlayerName(player_name);

        // Header
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "%s:", TR("Player"));
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", clean_name.c_str());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 1. Career Stats with this player (instant 0ms render from cached NoteModalData)
        if (md.valid && md.total_games > 0) {
            ImGui::BeginGroup();
            ImGui::Text("%s: %d", TR("Total Games"), md.total_games);
            ImGui::SameLine(160.0f);
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%s: %d", TR("Wins"), md.wins);
            ImGui::SameLine(280.0f);
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s: %d", TR("Losses"), md.losses);
            ImGui::SameLine(390.0f);
            if (md.winrate >= 70.0) {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%s: %.1f%%", TR("Winrate"), md.winrate);
            } else if (md.winrate >= 50.0) {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s: %.1f%%", TR("Winrate"), md.winrate);
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s: %.1f%%", TR("Winrate"), md.winrate);
            }
            ImGui::SameLine(510.0f);
            ImGui::Text("%s: %d", TR("APM"), md.median_apm);
            ImGui::EndGroup();
        } else {
            ImGui::TextDisabled("%s", TR("No career games recorded with this player yet."));
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 2. Recent Matches with this player (instant 0ms render from cached NoteModalData)
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s:", TR("Recent Games with Player"));
        if (!md.recent_matches.empty()) {
            if (ImGui::BeginTable("RecentMatchesTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg, ImVec2(0, 115))) {
                ImGui::TableSetupColumn(TR("Date"), ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn(TR("Map"), ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(TR("Commander"), ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn(TR("Difficulty"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn(TR("Result"), ImGuiTableColumnFlags_WidthFixed, 65.0f);
                ImGui::TableHeadersRow();

                for (const auto& rm : md.recent_matches) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(rm.game_date.c_str());

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(TR(rm.map_name.c_str()));

                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(TR(rm.commander.c_str()));

                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(TR(rm.difficulty.c_str()));

                    ImGui::TableSetColumnIndex(4);
                    if (rm.result == "Victory") {
                        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%s", TR("Victory"));
                    } else if (rm.result == "Defeat") {
                        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", TR("Defeat"));
                    } else {
                        ImGui::TextDisabled("%s", TR(rm.result.c_str()));
                    }
                }
                ImGui::EndTable();
            }
        } else {
            ImGui::TextDisabled("%s", TR("No replay records found with this player"));
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 3. Note Editing Section
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s:", TR("Player Note"));
        ImGui::InputTextMultiline("##PlayerNoteEditor", state.note_edit_buf, sizeof(state.note_edit_buf), ImVec2(-1.0f, 110.0f));

        ImGui::Spacing();

        // 4. Action Buttons
        if (ImGui::Button(TR("Save Note"), ImVec2(100, 28))) {
            Database::Instance().SetPlayerNote(player_name, state.note_edit_buf);
            state.show_note_modal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button(TR("Clear"), ImVec2(75, 28))) {
            memset(state.note_edit_buf, 0, sizeof(state.note_edit_buf));
            Database::Instance().SetPlayerNote(player_name, "");
        }

        ImGui::SameLine();
        if (ImGui::Button(TR("Find Games"), ImVec2(100, 28))) {
            strncpy_s(state.games_search_buf, clean_name.c_str(), sizeof(state.games_search_buf) - 1);
            state.request_tab_switch = 1;
            state.show_note_modal = false;
            ImGui::CloseCurrentPopup();
        }

        float btn_w = 85.0f;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - btn_w - ImGui::GetStyle().WindowPadding.x);
        ImGui::SameLine();
        if (ImGui::Button(TR("Close"), ImVec2(btn_w, 28))) {
            state.show_note_modal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

} // namespace sc2
