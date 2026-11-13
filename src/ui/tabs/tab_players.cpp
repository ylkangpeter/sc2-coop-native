#include "tab_players.h"
#include "../../app_state.h"
#include "../../db/database.h"
#include "../../utils/win32_utils.h"

#include <imgui.h>
#include <vector>

namespace sc2 {

int TabPlayers::s_current_page = 0;
std::string TabPlayers::s_sort_column = "wins";
bool TabPlayers::s_sort_ascending = false;

void TabPlayers::Render(AppState& state) {
    // 1. Search Bar & Controls
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 420);
    bool search_changed = ImGui::InputTextWithHint("##SearchPlayers",
                             "Search by player name or note keywords...",
                             state.players_search_buf, sizeof(state.players_search_buf));
    if (search_changed) {
        s_current_page = 0;
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear", ImVec2(60, 0))) {
        state.players_search_buf[0] = '\0';
        s_current_page = 0;
    }

    ImGui::SameLine();
    ImGui::Text("Sort:");
    ImGui::SameLine();
    const char* sort_items[] = {"Wins", "Losses", "Winrate", "APM", "Name"};
    static int sort_idx = 0;
    ImGui::SetNextItemWidth(100);
    if (ImGui::Combo("##SortCombo", &sort_idx, sort_items, IM_ARRAYSIZE(sort_items))) {
        if (sort_idx == 0) s_sort_column = "wins";
        else if (sort_idx == 1) s_sort_column = "losses";
        else if (sort_idx == 2) s_sort_column = "winrate";
        else if (sort_idx == 3) s_sort_column = "apm";
        else if (sort_idx == 4) s_sort_column = "name";
        s_current_page = 0;
    }

    ImGui::SameLine();
    if (ImGui::Checkbox(s_sort_ascending ? "Asc" : "Desc", &s_sort_ascending)) {
        s_current_page = 0;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 2. Fetch Players from Database
    int total_records = 0;
    int offset = s_current_page * kPageSize;
    auto players = Database::Instance().QueryPlayers(state.players_search_buf,
                                                     s_sort_column, s_sort_ascending,
                                                     kPageSize, offset, total_records);

    int total_pages = (total_records > 0) ? ((total_records + kPageSize - 1) / kPageSize) : 1;
    if (s_current_page >= total_pages) s_current_page = total_pages - 1;
    if (s_current_page < 0) s_current_page = 0;

    // 3. Render Table
    const ImGuiTableFlags table_flags = ImGuiTableFlags_ScrollY |
                                        ImGuiTableFlags_RowBg |
                                        ImGuiTableFlags_BordersOuter |
                                        ImGuiTableFlags_BordersInnerH |
                                        ImGuiTableFlags_Resizable;

    float table_height = ImGui::GetContentRegionAvail().y - 36.0f;
    if (ImGui::BeginTable("PlayersTable", 8, table_flags, ImVec2(0, table_height))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Player Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Games", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Wins", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("Losses", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("Winrate", ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableSetupColumn("APM", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("Player Note", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < players.size(); ++i) {
            auto& p = players[i];
            ImGui::TableNextRow();

            // Col 0: Name
            ImGui::TableSetColumnIndex(0);
            std::string clean_name = Win32Utils::CleanPlayerName(p.name);
            ImGui::Text("%s", clean_name.c_str());

            // Col 1: Games
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", p.total_games);

            // Col 2: Wins
            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%d", p.wins);

            // Col 3: Losses
            ImGui::TableSetColumnIndex(3);
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%d", p.losses);

            // Col 4: Winrate
            ImGui::TableSetColumnIndex(4);
            if (p.winrate >= 70.0) {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%.1f%%", p.winrate);
            } else if (p.winrate >= 50.0) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%.1f%%", p.winrate);
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%.1f%%", p.winrate);
            }

            // Col 5: APM
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%d", p.median_apm);

            // Col 6: Player Note (Inline editable)
            ImGui::TableSetColumnIndex(6);
            ImGui::PushID(static_cast<int>(i * 10 + 1));
            char note_buf[256];
            strncpy_s(note_buf, p.note.c_str(), sizeof(note_buf) - 1);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##NoteInput", note_buf, sizeof(note_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
                p.note = note_buf;
                Database::Instance().SetPlayerNote(p.name, p.note);
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                p.note = note_buf;
                Database::Instance().SetPlayerNote(p.name, p.note);
            }
            ImGui::PopID();

            // Col 7: Actions
            ImGui::TableSetColumnIndex(7);
            ImGui::PushID(static_cast<int>(i * 10 + 2));
            if (ImGui::SmallButton("Find Games")) {
                strncpy_s(state.games_search_buf, p.name.c_str(), sizeof(state.games_search_buf) - 1);
                state.request_tab_switch = 1; // switch to Games tab
            }
            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    // 4. Pagination Footer
    ImGui::Separator();
    ImGui::Text("Page %d of %d (Total: %d players)", s_current_page + 1, total_pages, total_records);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 240);

    if (ImGui::Button("<< First") && s_current_page > 0) {
        s_current_page = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button("< Prev") && s_current_page > 0) {
        s_current_page--;
    }
    ImGui::SameLine();
    if (ImGui::Button("Next >") && s_current_page < total_pages - 1) {
        s_current_page++;
    }
    ImGui::SameLine();
    if (ImGui::Button("Last >>") && s_current_page < total_pages - 1) {
        s_current_page = total_pages - 1;
    }
}

} // namespace sc2
