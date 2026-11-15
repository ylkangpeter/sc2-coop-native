#include "tab_weeklies.h"
#include "../../app_state.h"
#include "../../db/database.h"
#include "../../data/weekly_mutations.h"
#include "../../data/cached_mutators.h"
#include "../../data/sc2_constants.h"
#include "../../i18n/i18n.h"
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

static void RenderMutatorBadge(const char* mut, bool show_points = true) {
    ImTextureID icon = TextureLoader::Instance().GetMutatorIcon(mut);
    if (icon) {
        ImGui::Image(icon, ImVec2(18, 18));
        ImGui::SameLine();
    }
    int pts = GetMutatorPoint(mut);
    if (show_points && pts >= 0) {
        ImGui::Text("%s (%d %s)", TR(mut), pts, TR("pts"));
    } else {
        ImGui::Text("%s", TR(mut));
    }
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        if (pts >= 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s  [%d %s]", TR(mut), pts, TR("pts"));
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", TR(mut));
        }
        ImGui::Separator();
        std::string desc_key = std::string("mut_desc_") + mut;
        const char* localized_desc = TR(desc_key.c_str());
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
        if (std::strcmp(localized_desc, desc_key.c_str()) != 0) {
            ImGui::TextUnformatted(localized_desc);
        } else {
            auto desc_it = kMutators.find(mut);
            if (desc_it != kMutators.end()) {
                ImGui::TextUnformatted(desc_it->second.c_str());
            } else {
                ImGui::TextDisabled("%s", TR("Special event mutator"));
            }
        }
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 6.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.15f, 0.20f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.25f, 0.50f, 0.85f, 0.7f));
    float banner_h = ImGui::GetFrameHeight() * 2.0f + ImGui::GetStyle().ItemSpacing.y + 16.0f;
    if (ImGui::BeginChild("CurrentWeeklyBanner", ImVec2(0, banner_h), true)) {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "%s", TR("Active Weekly Mutation"));
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), TR("Week_Format"), curr_wm.week_number, TR(curr_wm.title));
        ImGui::SameLine();
        ImGui::TextDisabled(TR("Map_Format"), TR(curr_wm.map_name));

        const char* locate_label = TR("Locate Active Week");
        float locate_w = std::max(140.0f, ImGui::CalcTextSize(locate_label).x + ImGui::GetStyle().FramePadding.x * 2.0f + 16.0f);
        float locate_x = ImGui::GetWindowWidth() - locate_w - ImGui::GetStyle().WindowPadding.x - 8.0f;
        if (locate_x > ImGui::GetCursorPosX()) {
            ImGui::SameLine(locate_x);
        } else {
            ImGui::SameLine();
        }
        if (ImGui::Button(locate_label, ImVec2(locate_w, ImGui::GetFrameHeight()))) {
            s_locate_target_week = curr_wm.week_number;
        }

        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", TR("Mutators:"));
        ImGui::SameLine();

        for (size_t m_idx = 0; m_idx < curr_wm.mutators.size(); ++m_idx) {
            RenderMutatorBadge(curr_wm.mutators[m_idx], true);
            if (m_idx + 1 < curr_wm.mutators.size()) {
                ImGui::SameLine();
                ImGui::TextDisabled("|");
                ImGui::SameLine();
            }
        }

        float score_tier_w = 240.0f;
        float st_x = ImGui::GetWindowWidth() - score_tier_w - ImGui::GetStyle().WindowPadding.x - 8.0f;
        if (st_x > ImGui::GetCursorPosX()) {
            ImGui::SameLine(st_x);
        } else {
            ImGui::SameLine();
        }
        if (curr_wm.score >= 0) {
            ImGui::Text("%s", TR("Score: "));
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%d", curr_wm.score);
        } else {
            ImGui::TextDisabled("%s", TR("Score: -"));
        }
        ImGui::SameLine();
        ImGui::Text("%s", TR("Tier: "));
        ImGui::SameLine();
        ImGui::TextColored(GetTierColor(curr_wm.tier), "%s", TR(curr_wm.tier));
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);

    ImGui::Spacing();

    // 2. Search & Controls Bar
    ImGui::SetNextItemWidth(350);
    ImGui::InputTextWithHint("##SearchWeeklies", TR("Search_Weeklies_Hint"), s_search_buf, sizeof(s_search_buf));
    ImGui::SameLine();
    if (ImGui::Button(TR("Clear"), ImVec2(0, 0))) {
        s_search_buf[0] = '\0';
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Refresh Records"), ImVec2(0, 0))) {
        s_cache_valid = false;
    }
    ImGui::SameLine();
    ImGui::TextDisabled(TR("Catalog_Count_Format"), kWeeklyMutations.size());

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
    if (ImGui::BeginTable("WeekliesTable", 9, table_flags, ImVec2(0, 0))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(TR("Week"), ImGuiTableColumnFlags_WidthFixed, 45.0f);
        ImGui::TableSetupColumn(TR("Rotation"), ImGuiTableColumnFlags_WidthFixed, 95.0f);
        ImGui::TableSetupColumn(TR("Title"), ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn(TR("Map"), ImGuiTableColumnFlags_WidthFixed, 135.0f);
        ImGui::TableSetupColumn(TR("Mutators"), ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn(TR("Score"), ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn(TR("Tier"), ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn(TR("Personal Record"), ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn(TR("Action"), ImGuiTableColumnFlags_WidthFixed, 75.0f);
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
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "%s", TR("Active"));
            } else {
                std::string up_date = FormatUpcomingDate(static_cast<int>(i), now_ts);
                ImGui::TextDisabled("%s", up_date.c_str());
            }

            // Col 2: Title
            ImGui::TableSetColumnIndex(2);
            if (static_cast<int>(i) == curr_idx) {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", TR(wm.title));
            } else {
                ImGui::TextColored(ImVec4(0.85f, 0.65f, 1.0f, 1.0f), "%s", TR(wm.title));
            }

            // Col 3: Map
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(TR(wm.map_name));

            // Col 4: Mutators with icons, point badges and tooltip descriptions
            ImGui::TableSetColumnIndex(4);
            for (size_t m_idx = 0; m_idx < wm.mutators.size(); ++m_idx) {
                if (m_idx > 0) ImGui::SameLine();
                RenderMutatorBadge(wm.mutators[m_idx], false);
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
                ImGui::TextColored(GetTierColor(wm.tier), "%s", TR(wm.tier));
            } else {
                ImGui::TextDisabled("-");
            }

            // Col 7: Personal Record
            ImGui::TableSetColumnIndex(7);
            auto it = s_cached_records.find(wm.week_number);
            if (it != s_cached_records.end() && (it->second.wins > 0 || it->second.losses > 0)) {
                const auto& rec = it->second;
                if (rec.wins > 0) {
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%s", TR(rec.highest_diff.c_str()));
                    if (rec.fastest_win_seconds > 0) {
                        ImGui::SameLine();
                        char time_buf[32];
                        std::snprintf(time_buf, sizeof(time_buf), "%02d:%02d", rec.fastest_win_seconds / 60, rec.fastest_win_seconds % 60);
                        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "%s", time_buf);
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled(TR("Record_WinLoss_Format"), rec.wins, rec.losses);
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), TR("Defeat_Count_Format"), rec.losses);
                }
            } else {
                ImGui::TextDisabled("%s", TR("Unplayed"));
            }

            // Col 8: Action
            ImGui::TableSetColumnIndex(8);
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::SmallButton(TR("View Games"))) {
                strncpy_s(state.games_search_buf, wm.map_name, sizeof(state.games_search_buf) - 1);
                state.request_tab_switch = 1; // Switch to Games tab
            }
            ImGui::PopID();
        }

        ImGui::EndTable();
    }
}

} // namespace sc2
