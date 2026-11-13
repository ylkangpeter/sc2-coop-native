#include "modal_game_detail.h"
#include "../../app_state.h"
#include "../../i18n/i18n.h"
#include "../../utils/win32_utils.h"
#include "../../data/sc2_constants.h"
#include "../texture_loader.h"
#include "../hud/summary_overlay.h"

#include <imgui.h>

namespace sc2 {

void ModalGameDetail::Render(AppState& state) {
    if (state.show_detail_modal) {
        ImGui::OpenPopup(TR("Match Details"));
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(680, 520), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal(TR("Match Details"), &state.show_detail_modal, ImGuiWindowFlags_NoResize)) {
        const auto& r = state.detail_replay;

        // Top info header
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", r.map_name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("| %s | %s | %s", r.game_date.c_str(), r.form_length.c_str(), r.difficulty.c_str());
        ImGui::SameLine();
        if (r.result == "Victory") {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "[ %s ]", TR("Victory"));
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "[ %s ]", TR("Defeat"));
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Left Player 1 Card
        ImGui::BeginChild("P1Pane", ImVec2(310, 200), true);
        ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "%s", TR("Player 1"));
        std::string p1_clean = Win32Utils::CleanPlayerName(r.p1.name);
        ImGui::Text("%s", p1_clean.c_str());
        ImGui::Spacing();

        ImTextureID p1_icon = TextureLoader::Instance().GetCommanderIcon(r.p1.commander);
        if (p1_icon) {
            ImGui::Image(p1_icon, ImVec2(48, 48));
            ImGui::SameLine();
        }
        ImGui::BeginGroup();
        ImGui::Text("%s: %s", TR("Commander"), r.p1.commander.c_str());

        std::string p1_pres_name = "Default";
        auto it1 = kPrestiges.find(r.p1.commander);
        if (it1 != kPrestiges.end()) {
            if (r.p1.prestige == 1) p1_pres_name = it1->second.p1;
            else if (r.p1.prestige == 2) p1_pres_name = it1->second.p2;
            else if (r.p1.prestige == 3) p1_pres_name = it1->second.p3;
            else p1_pres_name = it1->second.p0;
        }
        if (r.p1.prestige >= 0 && r.p1.prestige <= 3) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s %d: %s", TR("Prestige"), r.p1.prestige, p1_pres_name.c_str());
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s: %s", TR("Prestige"), p1_pres_name.c_str());
        }
        ImGui::Text("%s: %d", TR("APM"), r.p1.apm);
        ImGui::EndGroup();
        ImGui::EndChild();

        // Right Player 2 Card
        ImGui::SameLine();
        ImGui::BeginChild("P2Pane", ImVec2(310, 200), true);
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%s", TR("Player 2 (Ally)"));
        std::string p2_clean = Win32Utils::CleanPlayerName(r.p2.name);
        ImGui::Text("%s", p2_clean.c_str());
        ImGui::Spacing();

        ImTextureID p2_icon = TextureLoader::Instance().GetCommanderIcon(r.p2.commander);
        if (p2_icon) {
            ImGui::Image(p2_icon, ImVec2(48, 48));
            ImGui::SameLine();
        }
        ImGui::BeginGroup();
        ImGui::Text("%s: %s", TR("Commander"), r.p2.commander.c_str());

        std::string p2_pres_name = "Default";
        auto it2 = kPrestiges.find(r.p2.commander);
        if (it2 != kPrestiges.end()) {
            if (r.p2.prestige == 1) p2_pres_name = it2->second.p1;
            else if (r.p2.prestige == 2) p2_pres_name = it2->second.p2;
            else if (r.p2.prestige == 3) p2_pres_name = it2->second.p3;
            else p2_pres_name = it2->second.p0;
        }
        if (r.p2.prestige >= 0 && r.p2.prestige <= 3) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s %d: %s", TR("Prestige"), r.p2.prestige, p2_pres_name.c_str());
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s: %s", TR("Prestige"), p2_pres_name.c_str());
        }
        ImGui::Text("%s: %d", TR("APM"), r.p2.apm);
        ImGui::EndGroup();
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s (%zu)", TR("Replay Chat Log"), r.chat.size());
        ImGui::BeginChild("ChatLogSubPane", ImVec2(0, 160), true);
        if (r.chat.empty()) {
            ImGui::TextDisabled("%s", TR("No in-game text messages recorded for this match."));
        } else {
            for (const auto& msg : r.chat) {
                int mm = static_cast<int>(msg.time_seconds) / 60;
                int ss = static_cast<int>(msg.time_seconds) % 60;

                ImVec4 name_col = (msg.player_id == 1) ? ImVec4(0.4f, 0.7f, 1.0f, 1.0f) : ImVec4(0.3f, 1.0f, 0.4f, 1.0f);
                ImGui::TextDisabled("[%02d:%02d]", mm, ss);
                ImGui::SameLine();
                ImGui::TextColored(name_col, "%s:", msg.player_name.c_str());
                ImGui::SameLine();
                ImGui::TextUnformatted(msg.text.c_str());
            }
        }
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button(TR("Locate Replay in Explorer"), ImVec2(180, 26))) {
            Win32Utils::RevealFileInExplorer(r.file_path);
        }
        ImGui::SameLine();
        if (ImGui::Button(TR("Summary Overlay"), ImVec2(170, 26))) {
            SummaryOverlay::Instance().ShowMatch(r);
        }
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 90);
        if (ImGui::Button(TR("Close"), ImVec2(90, 26))) {
            state.show_detail_modal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

} // namespace sc2
