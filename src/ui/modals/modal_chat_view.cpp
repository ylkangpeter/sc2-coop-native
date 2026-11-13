#include "modal_chat_view.h"
#include "../../app_state.h"

#include <imgui.h>

namespace sc2 {

void ModalChatView::Render(AppState& state) {
    if (state.show_chat_modal) {
        ImGui::OpenPopup("In-Game Chat Log");
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620, 460), ImGuiCond_Appearing);

    // Prevent outer modal scrollbar with ImGuiWindowFlags_NoScrollbar
    const ImGuiWindowFlags modal_flags = ImGuiWindowFlags_NoResize |
                                         ImGuiWindowFlags_NoScrollbar |
                                         ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::BeginPopupModal("In-Game Chat Log", &state.show_chat_modal, modal_flags)) {
        const auto& r = state.chat_replay;

        // Title and Game Info Header
        ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.25f, 1.0f), "%s", r.map_name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(%s, %s)", r.game_date.c_str(), r.form_length.c_str());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Calculate exact available height for child box so it perfectly reaches the Close button
        const float btn_w = 95.0f;
        const float btn_h = 28.0f;
        const float item_spacing_y = ImGui::GetStyle().ItemSpacing.y;
        float avail_h = ImGui::GetContentRegionAvail().y;
        float child_h = avail_h - btn_h - item_spacing_y * 2.0f;
        if (child_h < 120.0f) child_h = 120.0f;

        ImGui::BeginChild("FullChatPane", ImVec2(0, child_h), true);
        if (r.chat.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("  No in-game text messages recorded for this match.");
        } else {
            for (const auto& msg : r.chat) {
                int mm = static_cast<int>(msg.time_seconds) / 60;
                int ss = static_cast<int>(msg.time_seconds) % 60;

                ImVec4 name_col = (msg.player_id == 1) ? ImVec4(0.4f, 0.75f, 1.0f, 1.0f) : ImVec4(0.35f, 1.0f, 0.5f, 1.0f);
                ImGui::TextDisabled("[%02d:%02d]", mm, ss);
                ImGui::SameLine();
                ImGui::TextColored(name_col, "%s:", msg.player_name.c_str());
                ImGui::SameLine();
                ImGui::TextUnformatted(msg.text.c_str());
            }
        }
        ImGui::EndChild();

        ImGui::Spacing();

        // Right-align the Close button
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - btn_w - ImGui::GetStyle().WindowPadding.x);
        if (ImGui::Button("Close", ImVec2(btn_w, btn_h))) {
            state.show_chat_modal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

} // namespace sc2
