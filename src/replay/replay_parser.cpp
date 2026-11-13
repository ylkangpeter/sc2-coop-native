#include "replay_parser.h"
#include "mpq_reader.h"
#include "s2_decoder.h"
#include "../data/sc2_constants.h"
#include "../data/weekly_mutations.h"
#include "../utils/win32_utils.h"

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

std::optional<ReplayRecord> ReplayParser::ParseFile(const std::wstring& replay_path) {
    ReplayRecord record;

    // Convert wide path to UTF-8
    int len = WideCharToMultiByte(CP_UTF8, 0, replay_path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len > 0) {
        std::string u8path(len - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, replay_path.c_str(), -1, &u8path[0], len, nullptr, nullptr);
        record.file_path = u8path;
    }

    // Get file modification time
    struct _stat64 st;
    if (_wstat64(replay_path.c_str(), &st) == 0) {
        record.file_mtime = st.st_mtime;
        record.game_date = FormatTimestamp(st.st_mtime);
    }

    // 1. Read replay.gamemetadata.json
    nlohmann::json meta_json;
    if (!MpqReader::ReadJson(replay_path, "replay.gamemetadata.json", meta_json)) {
        return std::nullopt; // Not a valid SC2 replay with metadata
    }

    record.raw_map_name = meta_json.value("Title", "Unknown Map");
    record.map_name = NormalizeMapName(record.raw_map_name);
    record.duration_seconds = meta_json.value("Duration", 0);
    record.form_length = FormatDuration(record.duration_seconds);

    // Parse players from metadata
    if (meta_json.contains("Players") && meta_json["Players"].is_array()) {
        const auto& players_arr = meta_json["Players"];
        if (players_arr.size() >= 1) {
            record.p1.player_id = 1;
            record.p1.apm = players_arr[0].value("APM", 0);
            record.p1.result = players_arr[0].value("Result", "Loss");
        }
        if (players_arr.size() >= 2) {
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
        record.result = "Victory";
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
    std::vector<DecodedSlot> slots;
    if (MpqReader::ReadFile(replay_path, "replay.initData", initdata_bytes)) {
        slots = S2Decoder::DecodeInitDataSlots(initdata_bytes, cache_handles);
        if (slots.size() >= 1) {
            record.p1.commander = slots[0].commander.empty() ? "Unknown" : slots[0].commander;
            record.p1.prestige = slots[0].prestige;
            record.p1.prestige_name = slots[0].prestige_name;
            record.p1.masteries = slots[0].masteries;
            record.p1.commander_level = slots[0].commander_level;
            record.brutal_plus = slots[0].brutal_plus;
        }
        if (slots.size() >= 2) {
            record.p2.commander = slots[1].commander.empty() ? "Unknown" : slots[1].commander;
            record.p2.prestige = slots[1].prestige;
            record.p2.prestige_name = slots[1].prestige_name;
            record.p2.masteries = slots[1].masteries;
            record.p2.commander_level = slots[1].commander_level;
            if (slots[1].brutal_plus > record.brutal_plus) {
                record.brutal_plus = slots[1].brutal_plus;
            }
        }
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

    std::string d1, d2;
    if (slots.size() > 2) d1 = DifficultyFromInt(slots[2].difficulty);
    if (slots.size() > 3) d2 = DifficultyFromInt(slots[3].difficulty);

    if (record.brutal_plus > 0) {
        record.difficulty = "Brutal+" + std::to_string(record.brutal_plus);
    } else if (!d1.empty() && d1 == d2) {
        record.difficulty = d1;
    } else if (!d1.empty() && !d2.empty()) {
        record.difficulty = d1 + "/" + d2;
    } else if (!d1.empty()) {
        record.difficulty = d1;
    } else if (!d2.empty()) {
        record.difficulty = d2;
    } else {
        record.difficulty = "Brutal"; // Standard fallback
    }

    // Map bonus objectives count
    auto b_it = kMapBonusCounts.find(record.map_name);
    if (b_it != kMapBonusCounts.end()) {
        record.bonus_total = b_it->second;
    } else {
        record.bonus_total = 2;
    }
    if (record.result == "Victory") {
        record.bonus_earned = record.bonus_total;
    }

    // 4. Read replay.tracker.events (Player Stats, Units, Enemy Comps)
    std::vector<uint8_t> tracker_bytes;
    if (MpqReader::ReadFile(replay_path, "replay.tracker.events", tracker_bytes)) {
        auto summary = S2Decoder::DecodeTrackerEvents(tracker_bytes,
                                                      record.p1.kills, record.p2.kills,
                                                      record.p1.commander, record.p2.commander);
        record.p1_series = std::move(summary.p1_series);
        record.p2_series = std::move(summary.p2_series);
        record.p1.units = std::move(summary.p1_units);
        record.p2.units = std::move(summary.p2_units);
        record.amon_units = std::move(summary.amon_units);
        record.enemy_comp = std::move(summary.enemy_comp);
        if (record.p1.kills <= 0) record.p1.kills = summary.p1_kills;
        if (record.p2.kills <= 0) record.p2.kills = summary.p2_kills;
    }

    // Fallback series if tracker events were missing or empty
    if (record.p1_series.time_stamps.empty()) {
        int dur = record.duration_seconds > 0 ? record.duration_seconds : 900;
        for (int i = 0; i <= 10; ++i) {
            float t = (dur * i) / 10.0f;
            float progress = static_cast<float>(i) / 10.0f;
            record.p1_series.time_stamps.push_back(t);
            record.p2_series.time_stamps.push_back(t);

            record.p1_series.kill_count.push_back(record.p1.kills * progress);
            record.p2_series.kill_count.push_back(record.p2.kills * progress);

            record.p1_series.supply_used.push_back(std::min(200.0f, 15.0f + 160.0f * progress));
            record.p2_series.supply_used.push_back(std::min(200.0f, 15.0f + 140.0f * progress));

            record.p1_series.army_value.push_back(2000.0f * progress);
            record.p2_series.army_value.push_back(1800.0f * progress);

            record.p1_series.collection_rate.push_back(progress > 0.1f ? 1800.0f : 400.0f);
            record.p2_series.collection_rate.push_back(progress > 0.1f ? 1600.0f : 400.0f);
        }
    }

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

    // 5. Match Weekly Mutation catalog
    for (const auto& wm : kWeeklyMutations) {
        if (wm.map_name == record.map_name) {
            // Check if replay path or raw name mentions mutation or if weekly
            if (record.file_path.find("[CM]") != std::string::npos ||
                record.file_path.find("突变") != std::string::npos ||
                record.file_path.find("Mutation") != std::string::npos) {
                record.is_weekly = true;
                if (record.mutators.empty()) {
                    for (const char* mut : wm.mutators) {
                        record.mutators.emplace_back(mut);
                    }
                }
                break;
            }
        }
    }

    return record;
}

} // namespace sc2
