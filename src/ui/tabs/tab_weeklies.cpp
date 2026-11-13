#include "tab_weeklies.h"
#include "../../app_state.h"
#include "../../db/database.h"
#include "../../data/weekly_mutations.h"
#include "../../data/sc2_constants.h"
#include "../texture_loader.h"

#include <imgui.h>
#include <string>
#include <algorithm>

namespace sc2 {

char TabWeeklies::s_search_buf[128] = {0};
bool TabWeeklies::s_cache_valid = false;
std::unordered_map<int, Database::WeeklyRecord> TabWeeklies::s_cached_records;

void TabWeeklies::Render(AppState& state) {
    if (!s_cache_valid) {
        s_cached_records = Database::Instance().QueryAllWeeklyRecords();
        s_cache_valid = true;
    }

    ImGui::SetNextItemWidth(350);
    ImGui::InputTextWithHint("##SearchWeeklies", "Filter by title, map, or mutator...", s_search_buf, sizeof(s_search_buf));
    ImGui::SameLine();
    if (ImGui::Button("Clear", ImVec2(60, 0))) {
        s_search_buf[0] = '\0';
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh##Weeklies", ImVec2(70, 0))) {
        s_cache_valid = false;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(Catalog of %zu Weekly Mutations)", kWeeklyMutations.size());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    std::string search_term = s_search_buf;
    for (auto& c : search_term) c = tolower(c);

    const ImGuiTableFlags table_flags = ImGuiTableFlags_ScrollY |
                                        ImGuiTableFlags_RowBg |
                                        ImGuiTableFlags_BordersOuter |
                                        ImGuiTableFlags_BordersInnerH |
                                        ImGuiTableFlags_Resizable;

    if (ImGui::BeginTable("WeekliesTable", 6, table_flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Week", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableSetupColumn("Map", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Mutators", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Personal Record", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < kWeeklyMutations.size(); ++i) {
            const auto& wm = kWeeklyMutations[i];

            // Filter check
            if (!search_term.empty()) {
                std::string title_lower = wm.title;
                std::string map_lower = wm.map_name;
                for (auto& c : title_lower) c = tolower(c);
                for (auto& c : map_lower) c = tolower(c);

                bool match = (title_lower.find(search_term) != std::string::npos ||
                              map_lower.find(search_term) != std::string::npos);

                if (!match) {
                    for (const char* m : wm.mutators) {
                        std::string mut_lower = m;
                        for (auto& c : mut_lower) c = tolower(c);
                        if (mut_lower.find(search_term) != std::string::npos) {
                            match = true;
                            break;
                        }
                    }
                }
                if (!match) continue;
            }

            ImGui::TableNextRow();

            // Col 0: Week #
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("#%d", wm.week_number);

            // Col 1: Title
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(ImVec4(0.85f, 0.65f, 1.0f, 1.0f), "%s", wm.title);

            // Col 2: Map
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(wm.map_name);

            // Col 3: Mutators with icons and tooltip descriptions
            ImGui::TableSetColumnIndex(3);
            for (size_t m_idx = 0; m_idx < wm.mutators.size(); ++m_idx) {
                if (m_idx > 0) ImGui::SameLine();
                const char* mut = wm.mutators[m_idx];

                ImTextureID icon = TextureLoader::Instance().GetMutatorIcon(mut);
                if (icon) {
                    ImGui::Image(icon, ImVec2(18, 18));
                    ImGui::SameLine();
                }

                ImGui::Text("%s", mut);
                if (ImGui::IsItemHovered()) {
                    auto it = kMutators.find(mut);
                    if (it != kMutators.end()) {
                        ImGui::BeginTooltip();
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", mut);
                        ImGui::Separator();
                        ImGui::TextUnformatted(it->second.c_str());
                        ImGui::EndTooltip();
                    }
                }

                if (m_idx + 1 < wm.mutators.size()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("|");
                }
            }

            // Col 4: Personal Record
            ImGui::TableSetColumnIndex(4);
            auto it = s_cached_records.find(wm.week_number);
            if (it != s_cached_records.end() && (it->second.wins > 0 || it->second.losses > 0)) {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%dW", it->second.wins);
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%dL", it->second.losses);
                ImGui::SameLine();
                ImGui::TextDisabled("(%.0f%%)", it->second.winrate);
            } else {
                ImGui::TextDisabled("Unplayed");
            }

            // Col 5: Action
            ImGui::TableSetColumnIndex(5);
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::SmallButton("View Games")) {
                strncpy_s(state.games_search_buf, wm.map_name, sizeof(state.games_search_buf) - 1);
                state.request_tab_switch = 1; // Switch to Games tab
            }
            ImGui::PopID();
        }

        ImGui::EndTable();
    }
}

} // namespace sc2
