#include "tab_stats.h"
#include "career_chart.h"
#include "../../app_state.h"
#include "../../db/database.h"
#include "../../i18n/i18n.h"
#include "../../replay/replay_parser.h"
#include "../texture_loader.h"

#include <imgui.h>
#include <implot.h>
#include <algorithm>
#include <vector>
#include <string>
#include <cstdio>

namespace sc2 {

static std::string FormatHoursMinutes(int64_t total_seconds) {
    const int64_t total_minutes = std::max<int64_t>(0, total_seconds) / 60;
    const int hours = static_cast<int>(total_minutes / 60);
    const int minutes = static_cast<int>(total_minutes % 60);
    char buffer[96] = {};
    if (hours > 0) {
        std::snprintf(buffer, sizeof(buffer),
                      I18n::GetFormat("Duration_Hours_Minutes", "%dh %02dm"), hours, minutes);
    } else {
        std::snprintf(buffer, sizeof(buffer),
                      I18n::GetFormat("Duration_Minutes", "%dm"), minutes);
    }
    return buffer;
}

int TabStats::s_sub_tab = 0;
uint64_t TabStats::s_last_revision = 0;
StatsFilter TabStats::s_last_filter;
StatsSummary TabStats::s_cached_summary;

void TabStats::Render(AppState& state) {
    static CareerChart career_chart;
    // 1. Top Filters Bar
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s:", TR("Filters"));
    ImGui::SameLine();

    ImGui::Checkbox(TR("Casual"), &state.stats_filter.casual);
    ImGui::SameLine();
    ImGui::Checkbox(TR("Normal"), &state.stats_filter.normal);
    ImGui::SameLine();
    ImGui::Checkbox(TR("Hard"), &state.stats_filter.hard);
    ImGui::SameLine();
    ImGui::Checkbox(TR("Brutal"), &state.stats_filter.brutal);
    ImGui::SameLine();
    ImGui::Checkbox(TR("Brutal+"), &state.stats_filter.brutal_plus);

    ImGui::SameLine(0, 16.0f);
    ImGui::TextDisabled("|");
    ImGui::SameLine(0, 16.0f);

    ImGui::Checkbox("NA", &state.stats_filter.region_na);
    ImGui::SameLine();
    ImGui::Checkbox("EU", &state.stats_filter.region_eu);
    ImGui::SameLine();
    ImGui::Checkbox("KR", &state.stats_filter.region_kr);
    ImGui::SameLine();
    ImGui::Checkbox("CN", &state.stats_filter.region_cn);

    ImGui::SameLine(0, 20.0f);
    if (ImGui::SmallButton((std::string(TR("Refresh")) + "##Stats").c_str())) {
        Database::Instance().BumpRevision();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 2. Fetch Aggregated Stats (cached via unified revision)
    uint64_t current_rev = Database::Instance().GetRevision();
    bool filter_changed = (s_last_revision != current_rev) || (s_last_filter != state.stats_filter);
    if (filter_changed) {
        s_cached_summary = Database::Instance().CalculateStats(state.stats_filter);
        career_chart.SetActivity(s_cached_summary.daily_activity);
        s_last_filter = state.stats_filter;
        s_last_revision = current_rev;
    }
    const auto& summary = s_cached_summary;

    // 3. Stats Sub-Navigation
    auto SubTabBtn = [&](const char* label, int tab_idx) {
        bool is_active = (s_sub_tab == tab_idx);
        if (is_active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.45f, 0.85f, 1.0f));
        }
        if (ImGui::Button(label, ImVec2(0, 28))) {
            s_sub_tab = tab_idx;
        }
        if (is_active) {
            ImGui::PopStyleColor();
        }
    };

    SubTabBtn(TR("Maps Overview"), 0);
    ImGui::SameLine();
    SubTabBtn(TR("Difficulty & Regions"), 1);
    ImGui::SameLine();
    SubTabBtn(TR("My Commanders"), 2);
    ImGui::SameLine();
    SubTabBtn(TR("Allied Commanders"), 3);
    ImGui::SameLine();
    SubTabBtn(TR("Career Summary"), 4);

    ImGui::Spacing();
    if (s_sub_tab != 4) {
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), I18n::GetFormat("Games found: %d", "Games found: %d"), summary.total_games);
        ImGui::Spacing();
    }

    if (s_sub_tab == 0) {
        // --- Maps Tab ---
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", TR("Map Clear Times & Winrates"));
        ImGui::Spacing();

        float avail_w = ImGui::GetContentRegionAvail().x;
        float preview_w = std::clamp(avail_w * 0.32f, 280.0f, 360.0f);
        float table_w = avail_w - preview_w - ImGui::GetStyle().ItemSpacing.x;

        // Left Pane: Maps Table
        ImGui::BeginChild("MapsTablePane", ImVec2(table_w, 0), false);
        const ImGuiTableFlags table_flags = ImGuiTableFlags_RowBg |
                                            ImGuiTableFlags_BordersOuter |
                                            ImGuiTableFlags_BordersInnerH |
                                            ImGuiTableFlags_ScrollY |
                                            ImGuiTableFlags_ScrollX |
                                            ImGuiTableFlags_Resizable;

        float maps_inner_w = std::max(560.0f, table_w);
        if (ImGui::BeginTable("MapsTable", 7, table_flags, ImVec2(0, 0), maps_inner_w)) {
            ImGui::TableSetupColumn(TR("Map Name"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(TR("Fastest"), ImGuiTableColumnFlags_WidthFixed, 75.0f);
            ImGui::TableSetupColumn(TR("Avg"), ImGuiTableColumnFlags_WidthFixed, 75.0f);
            ImGui::TableSetupColumn(TR("Wins"), ImGuiTableColumnFlags_WidthFixed, 55.0f);
            ImGui::TableSetupColumn(TR("Losses"), ImGuiTableColumnFlags_WidthFixed, 55.0f);
            ImGui::TableSetupColumn(TR("Winrate"), ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn(TR("Freq"), ImGuiTableColumnFlags_WidthFixed, 65.0f);
            ImGui::TableHeadersRow();

            for (const auto& m : summary.maps) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(TR(m.map_name.c_str()));

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
        ImGui::EndChild();

        // Right side: Fastest map thumbnail preview
        ImGui::SameLine();
        ImGui::BeginChild("MapPreviewPane", ImVec2(0, 0), true);
        if (!summary.maps.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", TR("Most Played Map:"));
            ImGui::Text("%s (%d %s)", TR(summary.maps[0].map_name.c_str()),
                        summary.maps[0].wins + summary.maps[0].losses,
                        TR("games"));
            ImGui::Spacing();
            ImTextureID img = TextureLoader::Instance().GetMapImage(summary.maps[0].map_name);
            if (img) {
                ImGui::Image(img, ImVec2(260, 160));
            } else {
                ImGui::Dummy(ImVec2(260, 160));
            }
            ImGui::Spacing();
            ImGui::Text(I18n::GetFormat("Fastest Clear: %s", "Fastest Clear: %s"), summary.maps[0].fastest_str.c_str());
            ImGui::Text(I18n::GetFormat("Average Time: %s", "Average Time: %s"), summary.maps[0].avg_str.c_str());
            ImGui::Text(I18n::GetFormat("Winrate: %.1f%%", "Winrate: %.1f%%"), summary.maps[0].winrate);
        } else {
            ImGui::TextDisabled("%s", TR("No map data available."));
        }
        ImGui::EndChild();

    } else if (s_sub_tab == 1) {
        // --- Difficulty & Regions Tab ---
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", TR("Difficulty Breakdown & Regional Statistics"));
        ImGui::Spacing();

        float avail_w = ImGui::GetContentRegionAvail().x;
        float half_w = (avail_w - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

        // Left Table: Difficulty
        ImGui::BeginChild("DiffChild", ImVec2(half_w, 0), false);
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", TR("Difficulty Breakdown"));
        ImGui::Spacing();

        if (ImGui::BeginTable("DiffTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn(TR("Difficulty"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(TR("Wins"), ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn(TR("Losses"), ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn(TR("Winrate"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();

            int sum_wins = 0;
            int sum_losses = 0;

            for (const auto& d : summary.difficulties) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (d.difficulty.find("Brutal+") == 0) {
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "%s", TR(d.difficulty.c_str()));
                } else {
                    ImGui::TextUnformatted(TR(d.difficulty.c_str()));
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
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s (%s)", reinterpret_cast<const char*>(u8"Σ"), TR("Total"));

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
        ImGui::SameLine();
        ImGui::BeginChild("RegionChild", ImVec2(0, 0), false);
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", TR("Region Breakdown"));
        ImGui::Spacing();

        if (ImGui::BeginTable("RegionTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn(TR("Region"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(TR("Wins"), ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn(TR("Losses"), ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn(TR("Winrate"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
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
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", TR("My Commander Statistics & Winrate Distribution"));
        ImGui::Spacing();

        float avail_w = ImGui::GetContentRegionAvail().x;
        float left_w = (avail_w - ImGui::GetStyle().ItemSpacing.x) * 0.52f;

        ImGui::BeginChild("MyComTablePane", ImVec2(left_w, 0), false);
        const float commander_inner_width = std::max(650.0f, ImGui::GetContentRegionAvail().x);
        if (ImGui::BeginTable("MyComTable", 8, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_Resizable, ImVec2(0, 0), commander_inner_width)) {
            ImGui::TableSetupColumn(TR("Commander"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(TR("Games"), ImGuiTableColumnFlags_WidthFixed, 65.0f);
            ImGui::TableSetupColumn(TR("Wins"), ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn(TR("Losses"), ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn(TR("Winrate"), ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn(TR("APM"), ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn(TR("TIME"), ImGuiTableColumnFlags_WidthFixed, 85.0f);
            ImGui::TableSetupColumn(TR("TIME SHARE"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();

            for (const auto& c : summary.my_commanders) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);

                ImTextureID icon = TextureLoader::Instance().GetCommanderIcon(c.commander);
                if (icon) {
                    ImGui::Image(icon, ImVec2(18, 18));
                    ImGui::SameLine();
                }
                ImGui::TextUnformatted(TR(c.commander.c_str()));

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

                ImGui::TableSetColumnIndex(6);
                const std::string commander_time = FormatHoursMinutes(c.total_duration_seconds);
                ImGui::TextUnformatted(commander_time.c_str());

                ImGui::TableSetColumnIndex(7);
                ImGui::Text("%.1f%%", c.time_share);
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

        // Chart on right
        ImGui::SameLine();
        ImGui::BeginChild("MyComChartPane", ImVec2(0, 0), false);
        if (ImPlot::BeginPlot(TR("Games per Commander"), ImVec2(-1, -1))) {
            std::vector<double> x, y;
            std::vector<const char*> labels;
            for (size_t i = 0; i < summary.my_commanders.size(); ++i) {
                x.push_back(static_cast<double>(i));
                y.push_back(static_cast<double>(summary.my_commanders[i].total_games));
                labels.push_back(TR(summary.my_commanders[i].commander.c_str()));
            }

            ImPlot::SetupAxes(TR("Commander"), TR("Total Games"), ImPlotAxisFlags_None, ImPlotAxisFlags_AutoFit);
            if (!labels.empty()) {
                ImPlot::SetupAxisTicks(ImAxis_X1, x.data(), static_cast<int>(x.size()), labels.data());
            }
            if (!x.empty()) {
                ImPlot::PlotBars(TR("Games Played"), x.data(), y.data(), static_cast<int>(x.size()), 0.6);
            }
            ImPlot::EndPlot();
        }
        ImGui::EndChild();

    } else if (s_sub_tab == 3) {
        // --- Allied Commanders Tab ---
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", TR("Allied Commander Statistics & Synergy"));
        ImGui::Spacing();

        ImGui::BeginChild("AllyComPane", ImVec2(0, 0), false);
        if (ImGui::BeginTable("AllyComTable", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn(TR("Allied Commander"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(TR("Games"), ImGuiTableColumnFlags_WidthFixed, 65.0f);
            ImGui::TableSetupColumn(TR("Wins"), ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn(TR("Losses"), ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn(TR("Winrate"), ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn(TR("APM"), ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableHeadersRow();

            for (const auto& c : summary.ally_commanders) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);

                ImTextureID icon = TextureLoader::Instance().GetCommanderIcon(c.commander);
                if (icon) {
                    ImGui::Image(icon, ImVec2(18, 18));
                    ImGui::SameLine();
                }
                ImGui::TextUnformatted(TR(c.commander.c_str()));

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
        ImGui::EndChild();

    } else if (s_sub_tab == 4) {
        // --- Career Summary Tab ---
        const float available_width = ImGui::GetContentRegionAvail().x;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float card_width = std::min(180.0f, available_width);
        const int columns = std::clamp(static_cast<int>((available_width + spacing) / (card_width + spacing)), 1, 5);
        int index = 0;
        auto card = [&](const char* id, const char* label, const ImVec4& color, const char* format, auto value) {
            if (index++ % columns != 0) ImGui::SameLine();
            ImGui::BeginChild(id, ImVec2(card_width, 90), true);
            ImGui::TextDisabled("%s", label);
            ImGui::TextColored(color, format, value);
            ImGui::EndChild();
        };

        const ImVec4 time_color(0.3f, 0.9f, 1.0f, 1.0f);
        const std::string total_time = FormatHoursMinutes(summary.total_duration_seconds);
        const std::string average_time = ReplayParser::FormatDuration(summary.avg_duration_seconds);
        const std::string longest_time = ReplayParser::FormatDuration(summary.longest_duration_seconds);
        const std::string shortest_time = summary.shortest_duration_seconds > 0
            ? ReplayParser::FormatDuration(summary.shortest_duration_seconds) : "-";

        card("CardGames", TR("TOTAL GAMES"), ImVec4(1, 1, 1, 1), "%d", summary.total_games);
        card("CardWins", TR("VICTORIES"), ImVec4(0.3f, 1, 0.4f, 1), "%d", summary.total_wins);
        card("CardLosses", TR("DEFEATS"), ImVec4(1, 0.4f, 0.4f, 1), "%d", summary.total_losses);
        card("CardWinrate", TR("OVERALL WINRATE"), ImVec4(0.3f, 0.8f, 1, 1), "%.1f%%", summary.overall_winrate);
        card("CardActiveDays", TR("ACTIVE DAYS"), ImVec4(0.8f, 0.7f, 1, 1), "%d", summary.active_days);
        card("CardTotalTime", TR("TOTAL PLAY TIME"), time_color, "%s", total_time.c_str());
        card("CardAverageTime", TR("AVERAGE GAME TIME"), time_color, "%s", average_time.c_str());
        card("CardLongestTime", TR("LONGEST GAME"), ImVec4(1, 0.8f, 0.2f, 1), "%s", longest_time.c_str());
        card("CardShortestTime", TR("SHORTEST GAME"), time_color, "%s", shortest_time.c_str());

        ImGui::Spacing();
        career_chart.Render();
    }
}

} // namespace sc2
