#include "tab_games.h"
#include "mutator_badge.h"
#include "../../data/weekly_mutations.h"
#include "../../replay/match_mutators.h"
#include "../../app_state.h"
#include "../../i18n/i18n.h"
#include "../../db/database.h"
#include "../../utils/win32_utils.h"
#include "../../replay/mpq_reader.h"
#include "../../replay/s2_decoder.h"
#include "../texture_loader.h"
#include "../hud/summary_overlay.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace sc2 {

int TabGames::s_current_page = 0;
uint64_t TabGames::s_last_revision = 0;
std::string TabGames::s_last_search = "";
int TabGames::s_last_page = -1;
int TabGames::s_cached_total_records = 0;
std::vector<ReplayRecord> TabGames::s_cached_replays;


static const WeeklyMutation* FindWeeklyMutation(const ReplayRecord& replay) {
    if (!replay.is_weekly || replay.mutators.empty()) return nullptr;
    std::vector<std::string> actual;
    for (const auto& mutator : replay.mutators) actual.push_back(NormalizeMutation(mutator));
    std::sort(actual.begin(), actual.end());
    for (const auto& weekly : kWeeklyMutations) {
        if (replay.map_name != weekly.map_name || actual.size() != weekly.mutators.size()) continue;
        std::vector<std::string> expected;
        for (const char* mutator : weekly.mutators) expected.push_back(NormalizeMutation(mutator));
        std::sort(expected.begin(), expected.end());
        if (actual == expected) return &weekly;
    }
    return nullptr;
}

void TabGames::Render(AppState& state) {
    // 1. Search Bar & Controls Header
    float clear_w = ImGui::CalcTextSize(TR("Clear")).x + ImGui::GetStyle().FramePadding.x * 2.0f + 16.0f;
    float refresh_w = ImGui::CalcTextSize(TR("Refresh")).x + ImGui::GetStyle().FramePadding.x * 2.0f + 16.0f;
    ImGui::SetNextItemWidth(std::max(200.0f, ImGui::GetContentRegionAvail().x - clear_w - refresh_w - ImGui::GetStyle().ItemSpacing.x * 2.0f));
    const char* search_hint = TR("Search_Games_Hint");
    bool search_changed = ImGui::InputTextWithHint("##SearchGames",
                             search_hint,
                             state.games_search_buf, sizeof(state.games_search_buf));
    if (search_changed) {
        s_current_page = 0; // reset to first page on search
    }

    ImGui::SameLine();
    if (ImGui::Button(TR("Clear"), ImVec2(clear_w, 0))) {
        state.games_search_buf[0] = '\0';
        s_current_page = 0;
    }

    ImGui::SameLine();
    if (ImGui::Button(TR("Refresh"), ImVec2(refresh_w, 0))) {
        Database::Instance().BumpRevision();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 2. Fetch Data from Database (cached via unified revision)
    uint64_t current_rev = Database::Instance().GetRevision();
    std::string current_search = state.games_search_buf;
    if (s_last_revision != current_rev || current_search != s_last_search || s_current_page != s_last_page) {
        int offset = s_current_page * kPageSize;
        s_cached_replays = Database::Instance().QueryReplays(current_search, kPageSize, offset, s_cached_total_records);
        s_last_search = current_search;
        s_last_page = s_current_page;
        s_last_revision = current_rev;
    }

    int total_records = s_cached_total_records;
    int total_pages = (total_records > 0) ? ((total_records + kPageSize - 1) / kPageSize) : 1;
    if (s_current_page >= total_pages) {
        s_current_page = total_pages - 1;
        if (s_current_page != s_last_page) {
            int offset = s_current_page * kPageSize;
            s_cached_replays = Database::Instance().QueryReplays(current_search, kPageSize, offset, s_cached_total_records);
            s_last_page = s_current_page;
        }
    }
    if (s_current_page < 0) s_current_page = 0;

    auto& replays = s_cached_replays;

    // 3. Render Table
    const ImGuiTableFlags table_flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX |
                                        ImGuiTableFlags_RowBg |
                                        ImGuiTableFlags_BordersOuter |
                                        ImGuiTableFlags_BordersInnerH |
                                        ImGuiTableFlags_Resizable |
                                        ImGuiTableFlags_NoSavedSettings;

    float footer_h = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y + 12.0f;
    float table_height = ImGui::GetContentRegionAvail().y - footer_h;
    const float inner_width = std::max(1500.0f, ImGui::GetContentRegionAvail().x);
    if (ImGui::BeginTable("GamesTable", 12, table_flags, ImVec2(0, table_height), inner_width)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(TR("Map"), ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn(TR("Mode"), ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn(TR("Mutation Name"), ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn(TR("Mutators"), ImGuiTableColumnFlags_WidthFixed, 220.0f);
        ImGui::TableSetupColumn(TR("Result"), ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn(TR("Player 1"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(TR("Player 2"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(TR("Enemy"), ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn(TR("Length"), ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn(TR("Difficulty"), ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableSetupColumn(TR("Date"), ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn(TR("Actions"), ImGuiTableColumnFlags_WidthFixed, 220.0f);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < replays.size(); ++i) {
            auto& r = replays[i];
            ImGui::TableNextRow();

            bool p2_is_me = (!r.p2.handle.empty() && r.file_path.find(r.p2.handle) != std::string::npos) ||
                            (!r.p2.handle.empty() && AppState::Instance().HasUserHandle(r.p2.handle));
            bool p1_is_me = (!r.p1.handle.empty() && r.file_path.find(r.p1.handle) != std::string::npos) ||
                            (!r.p1.handle.empty() && AppState::Instance().HasUserHandle(r.p1.handle));
            if (!p1_is_me && !p2_is_me) {
                p1_is_me = true; // default fallback
            }

            // Col 0: Map
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(TR(r.map_name.c_str()));

            // Col 1: Mode (Weekly, Custom Mut, Regular)
            ImGui::TableSetColumnIndex(1);
            if (r.is_weekly) {
                ImGui::TextColored(ImVec4(0.85f, 0.45f, 1.0f, 1.0f), "%s", TR("Weekly"));
            } else if (!r.mutators.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.2f, 1.0f), "%s", TR("Custom Mut"));
            } else {
                ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.65f, 1.0f), "%s", TR("Regular"));
            }

            // Weekly names are only shown when the full mutator set matches.
            ImGui::TableSetColumnIndex(2);
            const auto* weekly = FindWeeklyMutation(r);
            if (weekly) ImGui::TextUnformatted(TR(weekly->title));
            else ImGui::TextDisabled("-");

            ImGui::TableSetColumnIndex(3);
            if (r.mutators.empty()) {
                ImGui::TextDisabled("-");
            } else {
                for (size_t m = 0; m < r.mutators.size(); ++m) {
                    if (m > 0) ImGui::SameLine(0.0f, 2.0f);
                    RenderMutatorBadge(r.mutators[m].c_str(), false, true);
                }
            }

            // Col 2: Result (Victory, Defeat, Tie, Unknown)
            ImGui::TableSetColumnIndex(4);
            if (r.result == "Victory") {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%s", TR("Victory"));
            } else if (r.result == "Defeat") {
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", TR("Defeat"));
            } else if (r.result == "Tie") {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", TR("Tie"));
            } else {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", TR("Unknown"));
            }

            // Col 3: Player 1
            ImGui::TableSetColumnIndex(5);
            std::string p1_clean = Win32Utils::CleanPlayerName(r.p1.name);
            if (p1_is_me) {
                ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "%s (Me)", p1_clean.c_str());
            } else {
                ImGui::Text("%s", p1_clean.c_str());
            }
            ImGui::SameLine();
            if (r.p1.prestige >= 0 && r.p1.prestige <= 3) {
                ImGui::TextDisabled("(%s P%d)", TR(r.p1.commander.c_str()), r.p1.prestige);
            } else {
                ImGui::TextDisabled("(%s)", TR(r.p1.commander.c_str()));
            }

            // Col 4: Player 2
            ImGui::TableSetColumnIndex(6);
            if (!r.p2.name.empty()) {
                std::string p2_clean = Win32Utils::CleanPlayerName(r.p2.name);
                if (p2_is_me) {
                    ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "%s (Me)", p2_clean.c_str());
                } else {
                    ImGui::Text("%s", p2_clean.c_str());
                }
                ImGui::SameLine();
                if (r.p2.prestige >= 0 && r.p2.prestige <= 3) {
                    ImGui::TextDisabled("(%s P%d)", TR(r.p2.commander.c_str()), r.p2.prestige);
                } else {
                    ImGui::TextDisabled("(%s)", TR(r.p2.commander.c_str()));
                }
            } else {
                ImGui::TextDisabled("-");
            }

            // Col 5: Enemy Race
            ImGui::TableSetColumnIndex(7);
            ImGui::TextUnformatted(TR(r.enemy_race.c_str()));

            // Col 6: Length
            ImGui::TableSetColumnIndex(8);
            ImGui::TextUnformatted(r.form_length.c_str());

            // Col 7: Difficulty
            ImGui::TableSetColumnIndex(9);
            if (r.difficulty == "Brutal" && weekly && weekly->score >= 0 &&
                std::string(weekly->tier).rfind("Brutal+", 0) == 0) {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%s", TR(weekly->tier));
            } else if (r.brutal_plus >= 1 && r.brutal_plus <= 6) {
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "%s", TR(r.difficulty.c_str()));
            } else if (r.brutal_plus > 6 || r.brutal_plus < 0) {
                ImGui::TextUnformatted(TR("Brutal"));
            } else {
                ImGui::TextUnformatted(TR(r.difficulty.c_str()));
            }

            // Col 8: Date
            ImGui::TableSetColumnIndex(10);
            ImGui::TextUnformatted(r.game_date.c_str());

            // Col 9: Actions
            ImGui::TableSetColumnIndex(11);
            ImGui::PushID(static_cast<int>(i));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 0.0f));

            if (ImGui::SmallButton(TR("Detail"))) {
                SummaryOverlay::Instance().ToggleMatch(r);
            }
            ImGui::SameLine();
            bool has_chat = !r.chat.empty();
            if (has_chat) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.4f, 1.0f));
            }
            bool chat_clicked = ImGui::SmallButton(TR("Chat"));
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
                                    cr.player_name = std::string(TR("Player")) + " " + std::to_string(m.player_id);
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
            if (ImGui::SmallButton(TR("Notes"))) {
                std::string ally_name = p1_is_me ? r.p2.name : r.p1.name;
                state.OpenPlayerNoteModal(ally_name);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("File"))) {
                Win32Utils::RevealFileInExplorer(r.file_path);
            }

            ImGui::PopStyleVar();
            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    // 4. Pagination Footer
    ImGui::Separator();
    ImGui::Text(I18n::GetFormat("page_info_format", "Page %d of %d (Total: %d games)"), s_current_page + 1, total_pages, total_records);

    float btn_bar_w = ImGui::CalcTextSize(TR("<< First")).x + ImGui::CalcTextSize(TR("< Prev")).x +
                      ImGui::CalcTextSize(TR("Next >")).x + ImGui::CalcTextSize(TR("Last >>")).x +
                      (ImGui::GetStyle().FramePadding.x * 2.0f * 4) + (ImGui::GetStyle().ItemSpacing.x * 3) + 20.0f;
    float page_avail = ImGui::GetContentRegionAvail().x;
    if (page_avail > btn_bar_w) {
        ImGui::SameLine(ImGui::GetCursorPosX() + page_avail - btn_bar_w);
    } else {
        ImGui::SameLine();
    }

    if (ImGui::Button(TR("<< First")) && s_current_page > 0) {
        s_current_page = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("< Prev")) && s_current_page > 0) {
        s_current_page--;
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Next >")) && s_current_page < total_pages - 1) {
        s_current_page++;
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Last >>")) && s_current_page < total_pages - 1) {
        s_current_page = total_pages - 1;
    }
}

} // namespace sc2
