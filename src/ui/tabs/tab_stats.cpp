#include "tab_stats.h"
#include "../../app_state.h"
#include "../../db/database.h"
#include "../texture_loader.h"

#include <imgui.h>
#include <implot.h>
#include <vector>
#include <string>

namespace sc2 {

int TabStats::s_sub_tab = 0;
bool TabStats::s_cache_valid = false;
StatsFilter TabStats::s_last_filter;
StatsSummary TabStats::s_cached_summary;

void TabStats::Render(AppState& state) {
    // 1. Top Filters Bar
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Filters");
    ImGui::SameLine(100);

    ImGui::Checkbox("Casual", &state.stats_filter.casual);
    ImGui::SameLine();
    ImGui::Checkbox("Normal", &state.stats_filter.normal);
    ImGui::SameLine();
    ImGui::Checkbox("Hard", &state.stats_filter.hard);
    ImGui::SameLine();
    ImGui::Checkbox("Brutal", &state.stats_filter.brutal);
    ImGui::SameLine();
    ImGui::Checkbox("Brutal+", &state.stats_filter.brutal_plus);

    ImGui::SameLine(520);
    ImGui::Checkbox("NA", &state.stats_filter.region_na);
    ImGui::SameLine();
    ImGui::Checkbox("EU", &state.stats_filter.region_eu);
    ImGui::SameLine();
    ImGui::Checkbox("KR", &state.stats_filter.region_kr);
    ImGui::SameLine();
    ImGui::Checkbox("CN", &state.stats_filter.region_cn);

    ImGui::SameLine(820);
    if (ImGui::SmallButton("Refresh##Stats")) {
        s_cache_valid = false;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 2. Fetch Aggregated Stats (Cached - only recalculate when filters change or cache invalidated)
    bool filter_changed = !s_cache_valid || (s_last_filter != state.stats_filter);
    if (filter_changed) {
        s_cached_summary = Database::Instance().CalculateStats(state.stats_filter);
        s_last_filter = state.stats_filter;
        s_cache_valid = true;
    }
    const auto& summary = s_cached_summary;

    // Games found count
    ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "Games found: %d", summary.total_games);
    ImGui::SameLine(180);

    // 3. Stats Sub-Navigation
    if (ImGui::Button("Maps Overview", ImVec2(120, 24))) s_sub_tab = 0;
    ImGui::SameLine();
    if (ImGui::Button("Difficulty & Regions", ImVec2(150, 24))) s_sub_tab = 1;
    ImGui::SameLine();
    if (ImGui::Button("My Commanders", ImVec2(120, 24))) s_sub_tab = 2;
    ImGui::SameLine();
    if (ImGui::Button("Allied Commanders", ImVec2(130, 24))) s_sub_tab = 3;
    ImGui::SameLine();
    if (ImGui::Button("Career Summary", ImVec2(130, 24))) s_sub_tab = 4;

    ImGui::Spacing();

    if (s_sub_tab == 0) {
        // --- Maps Tab ---
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Map Clear Times & Winrates");
        ImGui::Spacing();

        const ImGuiTableFlags table_flags = ImGuiTableFlags_RowBg |
                                            ImGuiTableFlags_BordersOuter |
                                            ImGuiTableFlags_BordersInnerH |
                                            ImGuiTableFlags_Resizable;

        float table_width = ImGui::GetContentRegionAvail().x - 300.0f;
        if (ImGui::BeginTable("MapsTable", 7, table_flags, ImVec2(table_width, 0))) {
            ImGui::TableSetupColumn("Map Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Fastest", ImGuiTableColumnFlags_WidthFixed, 65.0f);
            ImGui::TableSetupColumn("Avg", ImGuiTableColumnFlags_WidthFixed, 65.0f);
            ImGui::TableSetupColumn("Wins", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableSetupColumn("Losses", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableSetupColumn("Winrate", ImGuiTableColumnFlags_WidthFixed, 65.0f);
            ImGui::TableSetupColumn("Freq", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableHeadersRow();

            for (const auto& m : summary.maps) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(m.map_name.c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%s", m.fastest_str.c_str());

                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(m.avg_str.c_str());

                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%d", m.wins);

                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%d", m.losses);

                ImGui::TableSetColumnIndex(5);
                ImGui::Text("%.1f%%", m.winrate);

                ImGui::TableSetColumnIndex(6);
                ImGui::Text("%.1f%%", m.frequency);
            }
            ImGui::EndTable();
        }

        // Right side: Fastest map thumbnail preview
        ImGui::SameLine();
        ImGui::BeginChild("MapPreviewPane", ImVec2(0, 320), true);
        if (!summary.maps.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Most Played Map:");
            ImGui::Text("%s (%d games)", summary.maps[0].map_name.c_str(), summary.maps[0].wins + summary.maps[0].losses);
            ImGui::Spacing();
            ImTextureID img = TextureLoader::Instance().GetMapImage(summary.maps[0].map_name);
            if (img) {
                ImGui::Image(img, ImVec2(260, 160));
            }
            ImGui::Spacing();
            ImGui::Text("Fastest Clear: %s", summary.maps[0].fastest_str.c_str());
            ImGui::Text("Average Time: %s", summary.maps[0].avg_str.c_str());
            ImGui::Text("Winrate: %.1f%%", summary.maps[0].winrate);
        }
        ImGui::EndChild();

    } else if (s_sub_tab == 1) {
        // --- Difficulty & Regions Tab ---
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Difficulty Breakdown & Regional Statistics");
        ImGui::Spacing();

        // Left Table: Difficulty
        ImGui::BeginChild("DiffChild", ImVec2(450, 400), false);
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Difficulty Breakdown");
        ImGui::Spacing();

        if (ImGui::BeginTable("DiffTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Difficulty", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Wins", ImGuiTableColumnFlags_WidthFixed, 65.0f);
            ImGui::TableSetupColumn("Losses", ImGuiTableColumnFlags_WidthFixed, 65.0f);
            ImGui::TableSetupColumn("Winrate", ImGuiTableColumnFlags_WidthFixed, 75.0f);
            ImGui::TableHeadersRow();

            int sum_wins = 0;
            int sum_losses = 0;

            for (const auto& d : summary.difficulties) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (d.difficulty.find("Brutal+") == 0) {
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "%s", d.difficulty.c_str());
                } else {
                    ImGui::TextUnformatted(d.difficulty.c_str());
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%d", d.wins);

                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%d", d.losses);

                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%.1f%%", d.winrate);

                sum_wins += d.wins;
                sum_losses += d.losses;
            }

            // Total Sigma row
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s (Total)", reinterpret_cast<const char*>(u8"Σ"));

            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%d", sum_wins);

            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%d", sum_losses);

            ImGui::TableSetColumnIndex(3);
            int tot = sum_wins + sum_losses;
            double wr = tot > 0 ? (100.0 * sum_wins / tot) : 0.0;
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 1.0f, 1.0f), "%.1f%%", wr);

            ImGui::EndTable();
        }
        ImGui::EndChild();

        // Right Table: Regions
        ImGui::SameLine(480);
        ImGui::BeginChild("RegionChild", ImVec2(400, 400), false);
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Region Breakdown");
        ImGui::Spacing();

        if (ImGui::BeginTable("RegionTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Region", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Wins", ImGuiTableColumnFlags_WidthFixed, 65.0f);
            ImGui::TableSetupColumn("Losses", ImGuiTableColumnFlags_WidthFixed, 65.0f);
            ImGui::TableSetupColumn("Winrate", ImGuiTableColumnFlags_WidthFixed, 75.0f);
            ImGui::TableHeadersRow();

            for (const auto& r : summary.regions) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(r.region.c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%d", r.wins);

                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%d", r.losses);

                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%.1f%%", r.winrate);
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

    } else if (s_sub_tab == 2) {
        // --- My Commanders Tab ---
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "My Commander Statistics & Winrate Distribution");
        ImGui::Spacing();

        if (ImGui::BeginTable("MyComTable", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable, ImVec2(480, 0))) {
            ImGui::TableSetupColumn("Commander", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Games", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Wins", ImGuiTableColumnFlags_WidthFixed, 55.0f);
            ImGui::TableSetupColumn("Losses", ImGuiTableColumnFlags_WidthFixed, 55.0f);
            ImGui::TableSetupColumn("Winrate", ImGuiTableColumnFlags_WidthFixed, 65.0f);
            ImGui::TableSetupColumn("APM", ImGuiTableColumnFlags_WidthFixed, 55.0f);
            ImGui::TableHeadersRow();

            for (const auto& c : summary.my_commanders) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);

                ImTextureID icon = TextureLoader::Instance().GetCommanderIcon(c.commander);
                if (icon) {
                    ImGui::Image(icon, ImVec2(18, 18));
                    ImGui::SameLine();
                }
                ImGui::TextUnformatted(c.commander.c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d", c.total_games);

                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%d", c.wins);

                ImGui::TableSetColumnIndex(3);
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%d", c.losses);

                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%.1f%%", c.winrate);

                ImGui::TableSetColumnIndex(5);
                ImGui::Text("%d", c.avg_apm);
            }
            ImGui::EndTable();
        }

        // Chart on right
        ImGui::SameLine();
        if (ImPlot::BeginPlot("Games per Commander", ImVec2(-1, 380))) {
            std::vector<double> x, y;
            std::vector<const char*> labels;
            for (size_t i = 0; i < summary.my_commanders.size(); ++i) {
                x.push_back(static_cast<double>(i));
                y.push_back(static_cast<double>(summary.my_commanders[i].total_games));
                labels.push_back(summary.my_commanders[i].commander.c_str());
            }

            ImPlot::SetupAxes("Commander", "Total Games", ImPlotAxisFlags_None, ImPlotAxisFlags_AutoFit);
            if (!labels.empty()) {
                ImPlot::SetupAxisTicks(ImAxis_X1, x.data(), static_cast<int>(x.size()), labels.data());
            }
            if (!x.empty()) {
                ImPlot::PlotBars("Games Played", x.data(), y.data(), static_cast<int>(x.size()), 0.6);
            }
            ImPlot::EndPlot();
        }

    } else if (s_sub_tab == 3) {
        // --- Allied Commanders Tab ---
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Allied Commander Statistics & Synergy");
        ImGui::Spacing();

        if (ImGui::BeginTable("AllyComTable", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Allied Commander", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Games", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Wins", ImGuiTableColumnFlags_WidthFixed, 55.0f);
            ImGui::TableSetupColumn("Losses", ImGuiTableColumnFlags_WidthFixed, 55.0f);
            ImGui::TableSetupColumn("Winrate", ImGuiTableColumnFlags_WidthFixed, 65.0f);
            ImGui::TableSetupColumn("APM", ImGuiTableColumnFlags_WidthFixed, 55.0f);
            ImGui::TableHeadersRow();

            for (const auto& c : summary.ally_commanders) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);

                ImTextureID icon = TextureLoader::Instance().GetCommanderIcon(c.commander);
                if (icon) {
                    ImGui::Image(icon, ImVec2(18, 18));
                    ImGui::SameLine();
                }
                ImGui::TextUnformatted(c.commander.c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d", c.total_games);

                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%d", c.wins);

                ImGui::TableSetColumnIndex(3);
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%d", c.losses);

                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%.1f%%", c.winrate);

                ImGui::TableSetColumnIndex(5);
                ImGui::Text("%d", c.avg_apm);
            }
            ImGui::EndTable();
        }

    } else if (s_sub_tab == 4) {
        // --- Career Summary Tab ---
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Career Overview & Match Progression");
        ImGui::Spacing();

        ImGui::BeginChild("CardGames", ImVec2(180, 90), true);
        ImGui::TextDisabled("TOTAL GAMES");
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%d", summary.total_games);
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("CardWins", ImVec2(180, 90), true);
        ImGui::TextDisabled("VICTORIES");
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%d", summary.total_wins);
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("CardLosses", ImVec2(180, 90), true);
        ImGui::TextDisabled("DEFEATS");
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%d", summary.total_losses);
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("CardWinrate", ImVec2(180, 90), true);
        ImGui::TextDisabled("OVERALL WINRATE");
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "%.1f%%", summary.overall_winrate);
        ImGui::EndChild();
    }
}

} // namespace sc2
