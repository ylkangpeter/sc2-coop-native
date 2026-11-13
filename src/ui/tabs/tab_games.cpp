#include "tab_games.h"
#include "../../app_state.h"
#include "../../db/database.h"
#include "../../utils/win32_utils.h"
#include "../../replay/mpq_reader.h"
#include "../../replay/s2_decoder.h"
#include "../texture_loader.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace sc2 {

int TabGames::s_current_page = 0;

void TabGames::Render(AppState& state) {
    // 1. Search Bar & Controls Header
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 280);
    bool search_changed = ImGui::InputTextWithHint("##SearchGames",
                             "Search by map, player, commander, difficulty, enemy, mutator (separate terms by space)...",
                             state.games_search_buf, sizeof(state.games_search_buf));
    if (search_changed) {
        s_current_page = 0; // reset to first page on search
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear", ImVec2(60, 0))) {
        state.games_search_buf[0] = '\0';
        s_current_page = 0;
    }

    ImGui::SameLine();
    if (ImGui::Button("Refresh", ImVec2(70, 0))) {
        // Just trigger re-render
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 2. Fetch Data from Database
    int total_records = 0;
    int offset = s_current_page * kPageSize;
    auto replays = Database::Instance().QueryReplays(state.games_search_buf, kPageSize, offset, total_records);

    int total_pages = (total_records > 0) ? ((total_records + kPageSize - 1) / kPageSize) : 1;
    if (s_current_page >= total_pages) s_current_page = total_pages - 1;
    if (s_current_page < 0) s_current_page = 0;

    // 3. Render Table
    const ImGuiTableFlags table_flags = ImGuiTableFlags_ScrollY |
                                        ImGuiTableFlags_RowBg |
                                        ImGuiTableFlags_BordersOuter |
                                        ImGuiTableFlags_BordersInnerH |
                                        ImGuiTableFlags_Resizable;

    float table_height = ImGui::GetContentRegionAvail().y - 36.0f; // Leave space for pagination footer
    if (ImGui::BeginTable("GamesTable", 9, table_flags, ImVec2(0, table_height))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Map", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableSetupColumn("Player 1", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Player 2 (Ally)", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Enemy", ImGuiTableColumnFlags_WidthFixed, 65.0f);
        ImGui::TableSetupColumn("Length", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Difficulty", ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 160.0f);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < replays.size(); ++i) {
            auto& r = replays[i];
            ImGui::TableNextRow();

            // Col 0: Map
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(r.map_name.c_str());

            // Col 1: Result
            ImGui::TableSetColumnIndex(1);
            if (r.result == "Victory") {
                if (r.is_weekly) {
                    ImGui::TextColored(ImVec4(0.8f, 0.4f, 1.0f, 1.0f), "Weekly");
                } else {
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "Victory");
                }
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Defeat");
            }

            // Col 2: Player 1
            ImGui::TableSetColumnIndex(2);
            std::string p1_clean = Win32Utils::CleanPlayerName(r.p1.name);
            ImGui::Text("%s", p1_clean.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("(%s P%d)", r.p1.commander.c_str(), r.p1.prestige);

            // Col 3: Player 2 (Ally)
            ImGui::TableSetColumnIndex(3);
            if (!r.p2.name.empty()) {
                std::string p2_clean = Win32Utils::CleanPlayerName(r.p2.name);
                ImGui::Text("%s", p2_clean.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("(%s P%d)", r.p2.commander.c_str(), r.p2.prestige);
            } else {
                ImGui::TextDisabled("-");
            }

            // Col 4: Enemy Race
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(r.enemy_race.c_str());

            // Col 5: Length
            ImGui::TableSetColumnIndex(5);
            ImGui::TextUnformatted(r.form_length.c_str());

            // Col 6: Difficulty
            ImGui::TableSetColumnIndex(6);
            if (r.brutal_plus >= 1 && r.brutal_plus <= 6) {
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "%s", r.difficulty.c_str());
            } else if (r.brutal_plus > 6 || r.brutal_plus < 0) {
                ImGui::TextUnformatted("Brutal");
            } else {
                ImGui::TextUnformatted(r.difficulty.c_str());
            }

            // Col 7: Date
            ImGui::TableSetColumnIndex(7);
            ImGui::TextUnformatted(r.game_date.c_str());

            // Col 8: Actions
            ImGui::TableSetColumnIndex(8);
            ImGui::PushID(static_cast<int>(i));

            if (ImGui::SmallButton("Detail")) {
                state.detail_replay = r;
                if (state.detail_replay.chat.empty()) {
                    std::wstring wpath = Win32Utils::Utf8ToWide(state.detail_replay.file_path);
                    std::vector<uint8_t> msg_bytes;
                    if (MpqReader::ReadFile(wpath, "replay.message.events", msg_bytes)) {
                        auto msgs = S2Decoder::DecodeMessageEvents(msg_bytes);
                        if (!msgs.empty()) {
                            std::vector<DecodedPlayer> players;
                            std::vector<uint8_t> details_bytes;
                            if (MpqReader::ReadFile(wpath, "replay.details", details_bytes)) {
                                players = S2Decoder::DecodeDetails(details_bytes);
                            }
                            for (const auto& m : msgs) {
                                ChatRecord cr;
                                cr.time_seconds = m.time_seconds;
                                cr.player_id = m.player_id;
                                if (m.player_id >= 1 && m.player_id <= static_cast<int>(players.size())) {
                                    cr.player_name = Win32Utils::CleanPlayerName(players[m.player_id - 1].name);
                                } else if (m.player_id == 1 && !state.detail_replay.p1.name.empty()) {
                                    cr.player_name = state.detail_replay.p1.name;
                                } else if (m.player_id == 2 && !state.detail_replay.p2.name.empty()) {
                                    cr.player_name = state.detail_replay.p2.name;
                                } else {
                                    cr.player_name = "Player " + std::to_string(m.player_id);
                                }
                                cr.text = m.text;
                                state.detail_replay.chat.push_back(cr);
                            }
                            Database::Instance().UpdateReplayChat(state.detail_replay.file_path, state.detail_replay.ChatToJson());
                            r.chat = state.detail_replay.chat;
                        }
                    }
                }
                state.show_detail_modal = true;
            }
            ImGui::SameLine();
            bool has_chat = !r.chat.empty();
            if (has_chat) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.4f, 1.0f));
            }
            bool chat_clicked = ImGui::SmallButton("Chat");
            if (has_chat) {
                ImGui::PopStyleColor();
            }

            if (chat_clicked) {
                state.chat_replay = r;
                if (state.chat_replay.chat.empty()) {
                    std::wstring wpath = Win32Utils::Utf8ToWide(state.chat_replay.file_path);
                    std::vector<uint8_t> msg_bytes;
                    if (MpqReader::ReadFile(wpath, "replay.message.events", msg_bytes)) {
                        auto msgs = S2Decoder::DecodeMessageEvents(msg_bytes);
                        if (!msgs.empty()) {
                            std::vector<DecodedPlayer> players;
                            std::vector<uint8_t> details_bytes;
                            if (MpqReader::ReadFile(wpath, "replay.details", details_bytes)) {
                                players = S2Decoder::DecodeDetails(details_bytes);
                            }
                            for (const auto& m : msgs) {
                                ChatRecord cr;
                                cr.time_seconds = m.time_seconds;
                                cr.player_id = m.player_id;
                                if (m.player_id >= 1 && m.player_id <= static_cast<int>(players.size())) {
                                    cr.player_name = Win32Utils::CleanPlayerName(players[m.player_id - 1].name);
                                } else if (m.player_id == 1 && !state.chat_replay.p1.name.empty()) {
                                    cr.player_name = state.chat_replay.p1.name;
                                } else if (m.player_id == 2 && !state.chat_replay.p2.name.empty()) {
                                    cr.player_name = state.chat_replay.p2.name;
                                } else {
                                    cr.player_name = "Player " + std::to_string(m.player_id);
                                }
                                cr.text = m.text;
                                state.chat_replay.chat.push_back(cr);
                            }
                            Database::Instance().UpdateReplayChat(state.chat_replay.file_path, state.chat_replay.ChatToJson());
                            r.chat = state.chat_replay.chat;
                        }
                    }
                }
                state.show_chat_modal = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("File")) {
                Win32Utils::RevealFileInExplorer(r.file_path);
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    // 4. Pagination Footer
    ImGui::Separator();
    ImGui::Text("Page %d of %d (Total: %d games)", s_current_page + 1, total_pages, total_records);
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
