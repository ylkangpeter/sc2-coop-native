#include "replay_parser.h"
#include "mpq_reader.h"
#include "s2_decoder.h"
#include "../data/sc2_constants.h"
#include "match_mutators.h"
#include "../utils/win32_utils.h"
#include "../app_state.h"

#include <windows.h>
#include <sys/stat.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>

namespace sc2 {

std::string ReplayParser::FormatDuration(int seconds) {
    if (seconds < 0) seconds = 0;
    int m = seconds / 60;
    int s = seconds % 60;
    int h = m / 60;
    m %= 60;

    std::ostringstream oss;
    if (h > 0) {
        oss << std::setfill('0') << std::setw(2) << h << ":"
            << std::setfill('0') << std::setw(2) << m << ":"
            << std::setfill('0') << std::setw(2) << s;
    } else {
        oss << std::setfill('0') << std::setw(2) << m << ":"
            << std::setfill('0') << std::setw(2) << s;
    }
    return oss.str();
}

std::string ReplayParser::FormatTimestamp(int64_t mtime) {
    std::time_t t = static_cast<std::time_t>(mtime);
    std::tm tm_buf;
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M");
    return oss.str();
}

static void ApplyCommanderLevels(ReplayRecord& record, const std::vector<DecodedSlot>& slots) {
    auto apply = [&](PlayerInfo& player, size_t index) {
        if (index < slots.size() && slots[index].commander_level > 0) {
            player.commander_level = slots[index].commander_level;
            player.commander_mastery_level = slots[index].commander_mastery_level;
        }
    };
    apply(record.p1, 0);
    apply(record.p2, 1);
    record.has_level_data = true; // This initData was checked, even if levels are unavailable.
}

bool ReplayParser::RefreshCommanderLevels(ReplayRecord& record) {
    std::vector<uint8_t> bytes;
    auto path = Win32Utils::Utf8ToWide(record.file_path);
    struct _stat64 st;
    if (_wstat64(path.c_str(), &st) != 0 || st.st_mtime != record.file_mtime ||
        !MpqReader::ReadFile(path, "replay.initData", bytes) || bytes.empty()) return false;
    std::vector<std::string> handles;
    ApplyCommanderLevels(record, S2Decoder::DecodeInitDataSlots(bytes, handles));
    return true;
}

static void DecodeMatchEvents(const std::wstring& path, const nlohmann::json& metadata,
                              const std::vector<DecodedSlot>& slots, bool extension,
                              bool weekly, const std::vector<std::string>& cache_mutators, ReplayRecord& record) {
    const bool victory = record.result == "Victory";
    std::array<int, 2> swann_users{-1, -1};
    for (size_t i = 0; i < std::min(slots.size(), swann_users.size()); ++i) {
        if (slots[i].commander == "Swann") swann_users[i] = slots[i].user_id;
    }
    GameSignals signals;
    const bool read_lobby = extension && !weekly;
    if (read_lobby || swann_users[0] >= 0 || swann_users[1] >= 0 ||
        CanEstimateReplayDuration(record.raw_map_name, victory, record.duration_seconds)) {
        std::vector<uint8_t> bytes;
        const auto build = metadata.find("BaseBuild");
        if (build != metadata.end() && build->is_string() && MpqReader::ReadFile(path, "replay.game.events", bytes)) {
            signals = DecodeGameSignals(record.duration_seconds, build->get_ref<const std::string&>(), bytes,
                                        swann_users, read_lobby);
        }
    }

    std::vector<uint8_t> tracker;
    TrackerSummary summary;
    if (MpqReader::ReadFile(path, "replay.tracker.events", tracker)) {
        const int end_loop = MatchEndLoop(record.duration_seconds, record.raw_map_name, victory, signals, {});
        summary = S2Decoder::DecodeTrackerEvents(tracker, record.p1, record.p2, record.map_name, signals.drones, end_loop);
    }
    record.has_summary_data = summary.complete;
    if (summary.complete) {
        record.p1_series = std::move(summary.p1_series);
        record.p2_series = std::move(summary.p2_series);
        record.p1.units = std::move(summary.p1_units);
        record.p2.units = std::move(summary.p2_units);
        record.p1.contributions = std::move(summary.p1_contributions);
        record.p2.contributions = std::move(summary.p2_contributions);
        record.amon_units = std::move(summary.amon_units);
        record.enemy_comp = std::move(summary.enemy_comp);
        record.p1.kills = summary.p1_kills;
        record.p2.kills = summary.p2_kills;
        if (summary.bonus_known && record.bonus_total > 0) {
            record.bonus_earned = std::min(record.bonus_total, static_cast<int>(summary.bonus_loops.size()));
        }
    }
    record.duration_seconds = MatchDuration(record.duration_seconds, record.raw_map_name, victory, signals, summary.objectives);
    ApplyMatchMutators(record, slots, extension, weekly, cache_mutators, signals.lobby_actions,
                       summary.start_loop, summary.tracker_mutators);
}

std::optional<ReplayRecord> ReplayParser::ParseFile(const std::wstring& replay_path, ReplayParseDepth depth) {
    try {
        ReplayRecord record;

        // Convert wide path to UTF-8
        record.file_path = Win32Utils::WideToUtf8(replay_path);

        // Get file modification time
        struct _stat64 st;
        if (_wstat64(replay_path.c_str(), &st) != 0) {
            // A replay without a reliable mtime cannot be correlated with the
            // watcher event and must not be stored as epoch 0.
            return std::nullopt;
        }
        record.file_mtime = st.st_mtime;
        record.game_date = FormatTimestamp(st.st_mtime);

        // 1. Read replay.gamemetadata.json
        nlohmann::json meta_json;
        if (!MpqReader::ReadJson(replay_path, "replay.gamemetadata.json", meta_json)) {
            return std::nullopt; // Not a valid SC2 replay with metadata
        }

        if (meta_json.is_object()) {
            if (meta_json.contains("Title") && meta_json["Title"].is_string()) {
                record.raw_map_name = meta_json["Title"].get<std::string>();
            } else {
                record.raw_map_name = "Unknown Map";
            }
            record.map_name = NormalizeMapName(record.raw_map_name);
            if (meta_json.contains("Duration") && meta_json["Duration"].is_number()) {
                record.duration_seconds = meta_json["Duration"].get<int>();
            }

            // Parse players from metadata
            if (meta_json.contains("Players") && meta_json["Players"].is_array() && !meta_json["Players"].empty()) {
                const auto& players_arr = meta_json["Players"];
                if (players_arr.size() >= 1 && players_arr[0].is_object()) {
                    record.p1.player_id = 1;
                    record.p1.apm = players_arr[0].value("APM", 0);
                    record.p1.result = players_arr[0].value("Result", "Loss");
                }
                if (players_arr.size() >= 2 && players_arr[1].is_object()) {
                    record.p2.player_id = 2;
                    record.p2.apm = players_arr[1].value("APM", 0);
                    record.p2.result = players_arr[1].value("Result", "Loss");
                }
                if ((record.p1.result == "Win") || (record.p2.result == "Win")) {
                    record.result = "Victory";
                } else {
                    record.result = "Defeat";
                }
            } else {
                record.result = "Unknown";
            }
        } else {
            return std::nullopt;
        }

    // 2. Read replay.details
    std::vector<DecodedPlayer> players;
    std::vector<uint8_t> details_bytes;
    if (MpqReader::ReadFile(replay_path, "replay.details", details_bytes)) {
        players = S2Decoder::DecodeDetails(details_bytes);
        if (players.size() >= 1) {
            record.p1.name = Win32Utils::CleanPlayerName(players[0].name);
            record.p1.race = players[0].race;
            record.p1.handle = players[0].handle;
            record.region = players[0].region;
        }
        if (players.size() >= 2) {
            record.p2.name = Win32Utils::CleanPlayerName(players[1].name);
            record.p2.race = players[1].race;
            record.p2.handle = players[1].handle;
        }
        // If there is an enemy slot (player 3 or player 0)
        if (players.size() >= 3 && !players[2].race.empty()) {
            record.enemy_race = players[2].race;
        }
    }

    // 3. Read replay.initData
    std::vector<uint8_t> initdata_bytes;
    std::vector<std::string> cache_handles;
    std::vector<std::string> init_mutators;
    bool extension = false;
    bool is_weekly = false;
    std::vector<DecodedSlot> slots;
    if (MpqReader::ReadFile(replay_path, "replay.initData", initdata_bytes)) {
        slots = S2Decoder::DecodeInitDataSlots(initdata_bytes, cache_handles, init_mutators, is_weekly, &extension);
        ApplyCommanderLevels(record, slots);
        int bp0 = 0, bp1 = 0;
        if (slots.size() >= 1) {
            record.p1.commander = slots[0].commander.empty() ? "Unknown" : slots[0].commander;
            record.p1.prestige = (slots[0].prestige >= 0 && slots[0].prestige <= 3) ? slots[0].prestige : 0;
            record.p1.prestige_name = slots[0].prestige_name;
            record.p1.masteries = slots[0].masteries;
            if (slots[0].brutal_plus >= 1 && slots[0].brutal_plus <= 6) {
                bp0 = slots[0].brutal_plus;
            }
        }
        if (slots.size() >= 2) {
            record.p2.commander = slots[1].commander.empty() ? "Unknown" : slots[1].commander;
            record.p2.prestige = (slots[1].prestige >= 0 && slots[1].prestige <= 3) ? slots[1].prestige : 0;
            record.p2.prestige_name = slots[1].prestige_name;
            record.p2.masteries = slots[1].masteries;
            if (slots[1].brutal_plus >= 1 && slots[1].brutal_plus <= 6) {
                bp1 = slots[1].brutal_plus;
            }
        }
        record.brutal_plus = std::max(bp0, bp1);
    }

    // Determine difficulty string
    auto DifficultyFromInt = [](int d) -> std::string {
        switch (d) {
            case 1: return "Casual";
            case 2: return "Normal";
            case 3: return "Hard";
            case 4: return "Brutal";
            default: return "";
        }
    };

    if (record.brutal_plus >= 1 && record.brutal_plus <= 6) {
        record.difficulty = "Brutal+" + std::to_string(record.brutal_plus);
    } else {
        // In SC2 Co-op, human players occupy slots 0 and 1, whose lobby difficulty
        // field is set to a dummy default 1 (VyEy / Casual). The actual mission difficulty
        // is set on the computer enemy AI slots (control == 3, typically slot 2+).
        int chosen_diff = 0;
        for (size_t i = 2; i < slots.size(); ++i) {
            if (slots[i].control == 3 && slots[i].difficulty > 0) {
                chosen_diff = slots[i].difficulty;
                break;
            }
        }
        // Fallback: slot 2 if present
        if (chosen_diff == 0 && slots.size() > 2 && slots[2].difficulty > 0) {
            chosen_diff = slots[2].difficulty;
        }
        // Fallback: if human slots have explicit non-default difficulty > 1 (e.g. custom maps)
        if (chosen_diff == 0) {
            if (slots.size() > 0 && slots[0].difficulty > 1) {
                chosen_diff = slots[0].difficulty;
            } else if (slots.size() > 1 && slots[1].difficulty > 1) {
                chosen_diff = slots[1].difficulty;
            }
        }
        // Fallback: slot 0 difficulty
        if (chosen_diff == 0 && slots.size() > 0 && slots[0].difficulty > 0) {
            chosen_diff = slots[0].difficulty;
        }

        std::string diff_str = (chosen_diff > 0) ? DifficultyFromInt(chosen_diff) : "";
        if (!diff_str.empty()) {
            record.difficulty = diff_str;
        } else {
            record.difficulty = "Unknown";
        }
    }

    if (record.brutal_plus == 0 && slots.size() >= 4 && slots[2].control == 3 && slots[3].control == 3) {
        const auto first = DifficultyFromInt(slots[2].difficulty);
        const auto second = DifficultyFromInt(slots[3].difficulty);
        if (!first.empty() && !second.empty() && first != second) record.ally_difficulty = second;
    }

    // Map bonus objectives count
    auto b_it = kMapBonusCounts.find(record.map_name);
    if (b_it != kMapBonusCounts.end()) {
        record.bonus_total = b_it->second;
    }

    if (depth == ReplayParseDepth::Full) {
        DecodeMatchEvents(replay_path, meta_json, slots, extension, is_weekly, init_mutators, record);
    } else {
        ApplyMatchMutators(record, slots, extension, is_weekly, init_mutators, {}, -1, {});
    }
    record.form_length = FormatDuration(record.duration_seconds);

    // 4. Read replay.message.events (chat)
    std::vector<uint8_t> msg_bytes;
    if (MpqReader::ReadFile(replay_path, "replay.message.events", msg_bytes)) {
        auto chat_msgs = S2Decoder::DecodeMessageEvents(msg_bytes);
        for (const auto& m : chat_msgs) {
            ChatRecord cr;
            cr.time_seconds = m.time_seconds;
            cr.player_id = m.player_id;
            if (m.player_id >= 1 && m.player_id <= static_cast<int>(players.size())) {
                cr.player_name = Win32Utils::CleanPlayerName(players[m.player_id - 1].name);
            } else if (m.player_id == 1 && !record.p1.name.empty()) {
                cr.player_name = record.p1.name;
            } else if (m.player_id == 2 && !record.p2.name.empty()) {
                cr.player_name = record.p2.name;
            } else {
                cr.player_name = "Player " + std::to_string(m.player_id);
            }
            cr.text = m.text;
            record.chat.push_back(cr);
        }
    }

    return record;
    } catch (const std::exception&) {
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace sc2
