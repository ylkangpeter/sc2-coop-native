#include "tab_weeklies.h"
#include "../../app_state.h"
#include "../../db/database.h"
#include "../../data/weekly_mutations.h"
#include "../../data/cached_mutators.h"
#include "../../data/sc2_constants.h"
#include "../texture_loader.h"

#include <imgui.h>
#include <string>
#include <algorithm>
#include <ctime>
#include <cstring>

namespace sc2 {

char TabWeeklies::s_search_buf[128] = {0};
bool TabWeeklies::s_cache_valid = false;
int TabWeeklies::s_locate_target_week = -1;
std::unordered_map<int, Database::WeeklyRecord> TabWeeklies::s_cached_records;

static ImVec4 GetTierColor(const char* tier) {
    if (!tier || std::strcmp(tier, "-") == 0) return ImVec4(0.6f, 0.6f, 0.6f, 0.8f);
    if (std::strstr(tier, "Brutal+6")) return ImVec4(0.95f, 0.25f, 1.0f, 1.0f);
    if (std::strstr(tier, "Brutal+5")) return ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
    if (std::strstr(tier, "Brutal+4")) return ImVec4(1.0f, 0.6f, 0.2f, 1.0f);
    if (std::strstr(tier, "Brutal+3")) return ImVec4(1.0f, 0.85f, 0.3f, 1.0f);
    if (std::strstr(tier, "Brutal+2")) return ImVec4(0.3f, 0.8f, 1.0f, 1.0f);
    if (std::strstr(tier, "Brutal+1")) return ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
    return ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
}

void TabWeeklies::Render(AppState& state) {
    if (!s_cache_valid) {
        s_cached_records = Database::Instance().QueryAllWeeklyRecords();
        s_cache_valid = true;
    }

    int64_t now_ts = std::time(nullptr);
    int curr_idx = GetCurrentWeeklyMutationIndex(now_ts);
    const auto& curr_wm = kWeeklyMutations[curr_idx];

    // 1. Top Card: Current Weekly Mutation Banner
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.15f, 0.20f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.25f, 0.50f, 0.85f, 0.7f));
    if (ImGui::BeginChild("CurrentWeeklyBanner", ImVec2(0, 78), true)) {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "● Active Weekly Mutation");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Week #%d: %s", curr_wm.week_number, curr_wm.title);
        ImGui::SameLine();
        ImGui::TextDisabled("|  Map: %s", curr_wm.map_name);

        ImGui::SameLine(ImGui::GetWindowWidth() - 165);
        if (ImGui::Button("Locate Active Week", ImVec2(150, 24))) {
            s_locate_target_week = curr_wm.week_number;
        }

        ImGui::Spacing();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Mutators:");
        ImGui::SameLine();

        for (size_t m_idx = 0; m_idx < curr_wm.mutators.size(); ++m_idx) {
            const char* mut = curr_wm.mutators[m_idx];
            ImTextureID icon = TextureLoader::Instance().GetMutatorIcon(mut);
            if (icon) {
                ImGui::Image(icon, ImVec2(18, 18));
                ImGui::SameLine();
            }
            int pts = GetMutatorPoint(mut);
            if (pts >= 0) {
                ImGui::Text("%s (%d pts)", mut, pts);
            } else {
                ImGui::Text("%s", mut);
            }
            if (m_idx + 1 < curr_wm.mutators.size()) {
                ImGui::SameLine();
                ImGui::TextDisabled("|");
                ImGui::SameLine();
            }
        }

        ImGui::SameLine(ImGui::GetWindowWidth() - 320);
        if (curr_wm.score >= 0) {
            ImGui::Text("Score: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%d", curr_wm.score);
        } else {
            ImGui::TextDisabled("Score: -");
        }
        ImGui::SameLine();
        ImGui::Text("  Tier: ");
        ImGui::SameLine();
        ImGui::TextColored(GetTierColor(curr_wm.tier), "%s", curr_wm.tier);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();

    ImGui::Spacing();

    // 2. Search & Controls Bar
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

    // Table with 9 columns: Week, Rotation, Title, Map, Mutators, Score, Tier, Personal Record, Action
    if (ImGui::BeginTable("WeekliesTable", 9, table_flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Week", ImGuiTableColumnFlags_WidthFixed, 45.0f);
        ImGui::TableSetupColumn("Rotation", ImGuiTableColumnFlags_WidthFixed, 95.0f);
        ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthFixed, 170.0f);
        ImGui::TableSetupColumn("Map", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Mutators", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Score", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("Tier", ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableSetupColumn("Personal Record", ImGuiTableColumnFlags_WidthFixed, 170.0f);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 80.0f);
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

            // Scroll to target week if requested
            if (s_locate_target_week == wm.week_number) {
                ImGui::SetScrollHereY(0.25f);
                s_locate_target_week = -1;
            }

            // Col 0: Week #
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("#%d", wm.week_number);

            // Col 1: Rotation / Upcoming Date
            ImGui::TableSetColumnIndex(1);
            if (static_cast<int>(i) == curr_idx) {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "● Active");
            } else {
                std::string up_date = FormatUpcomingDate(static_cast<int>(i), now_ts);
                ImGui::TextDisabled("%s", up_date.c_str());
            }

            // Col 2: Title
            ImGui::TableSetColumnIndex(2);
            if (static_cast<int>(i) == curr_idx) {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", wm.title);
            } else {
                ImGui::TextColored(ImVec4(0.85f, 0.65f, 1.0f, 1.0f), "%s", wm.title);
            }

            // Col 3: Map
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(wm.map_name);

            // Col 4: Mutators with icons, point badges and tooltip descriptions
            ImGui::TableSetColumnIndex(4);
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
                    ImGui::BeginTooltip();
                    int pts = GetMutatorPoint(mut);
                    if (pts >= 0) {
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s  [%d pts]", mut, pts);
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", mut);
                    }
                    ImGui::Separator();
                    auto desc_it = kMutators.find(mut);
                    if (desc_it != kMutators.end()) {
                        ImGui::TextUnformatted(desc_it->second.c_str());
                    } else {
                        ImGui::TextDisabled("Special event mutator");
                    }
                    ImGui::EndTooltip();
                }

                if (m_idx + 1 < wm.mutators.size()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("|");
                }
            }

            // Col 5: Score
            ImGui::TableSetColumnIndex(5);
            if (wm.score >= 0) {
                ImGui::Text("%d", wm.score);
            } else {
                ImGui::TextDisabled("-");
            }

            // Col 6: Difficulty Tier (Brutal+X)
            ImGui::TableSetColumnIndex(6);
            if (std::strcmp(wm.tier, "-") != 0) {
                ImGui::TextColored(GetTierColor(wm.tier), "%s", wm.tier);
            } else {
                ImGui::TextDisabled("-");
            }

            // Col 7: Personal Record
            ImGui::TableSetColumnIndex(7);
            auto it = s_cached_records.find(wm.week_number);
            if (it != s_cached_records.end() && (it->second.wins > 0 || it->second.losses > 0)) {
                const auto& rec = it->second;
                if (rec.wins > 0) {
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%s", rec.highest_diff.c_str());
                    if (rec.fastest_win_seconds > 0) {
                        ImGui::SameLine();
                        char time_buf[32];
                        std::snprintf(time_buf, sizeof(time_buf), "%02d:%02d", rec.fastest_win_seconds / 60, rec.fastest_win_seconds % 60);
                        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "%s", time_buf);
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%dW %dL)", rec.wins, rec.losses);
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Defeat (%dL)", rec.losses);
                }
            } else {
                ImGui::TextDisabled("Unplayed");
            }

            // Col 8: Action
            ImGui::TableSetColumnIndex(8);
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
