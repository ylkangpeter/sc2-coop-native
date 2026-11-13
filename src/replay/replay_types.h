#pragma once

#include <string>
#include <vector>
#include <array>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace sc2 {

struct UnitStat {
    std::string name;
    int created = 0;
    int lost = 0;
    int kills = 0;
    float kill_fraction = 0.0f;
};

struct PlayerTimeSeries {
    std::vector<float> time_stamps;
    std::vector<float> army_value;
    std::vector<float> supply_used;
    std::vector<float> kill_count;
    std::vector<float> collection_rate;
};

struct PlayerInfo {
    int player_id = 0;
    std::string name;
    std::string handle;
    std::string race;
    std::string commander = "Unknown";
    int prestige = 0;
    std::string prestige_name;
    int commander_level = 15;
    int apm = 0;
    int kills = 0;
    std::string result; // "Win" or "Loss"
    std::vector<int> masteries = {0, 0, 0, 0, 0, 0}; // 6 mastery values
    std::vector<UnitStat> units;
};

struct ChatRecord {
    double time_seconds = 0.0;
    int player_id = 1;
    std::string player_name;
    std::string text;
};

struct ReplayRecord {
    std::string file_path;
    int64_t file_mtime = 0;
    std::string game_date;       // "2024-03-29 18:42"
    std::string map_name;        // Normalized canonical map name, e.g. "Dead of Night"
    std::string raw_map_name;    // Original title, e.g. "亡者之夜"
    int duration_seconds = 0;    // Duration in seconds
    std::string form_length;     // "18:45"
    std::string result;          // "Victory" or "Defeat"
    std::string difficulty = "Brutal"; // "Casual", "Normal", "Hard", "Brutal", "Brutal+X"
    int brutal_plus = 0;         // 0 to 6
    std::string enemy_race;      // "Terran", "Zerg", "Protoss"
    std::string region = "CN";   // "NA", "EU", "KR", "CN"
    bool is_weekly = false;      // True if weekly mutation
    std::vector<std::string> mutators;
    PlayerInfo p1;
    PlayerInfo p2;
    std::vector<ChatRecord> chat;

    // Detailed Post-Game Summary Fields
    std::string enemy_comp;
    std::vector<UnitStat> amon_units;
    int bonus_earned = 0;
    int bonus_total = 0;
    PlayerTimeSeries p1_series;
    PlayerTimeSeries p2_series;

    std::string DetailsToJson() const {
        nlohmann::json j;
        j["enemy_comp"] = enemy_comp;
        j["bonus_earned"] = bonus_earned;
        j["bonus_total"] = bonus_total;

        auto SerializeUnits = [](const std::vector<UnitStat>& units) {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& u : units) {
                arr.push_back({
                    {"name", u.name},
                    {"created", u.created},
                    {"lost", u.lost},
                    {"kills", u.kills},
                    {"fraction", u.kill_fraction}
                });
            }
            return arr;
        };

        j["p1_prestige_name"] = p1.prestige_name;
        j["p1_masteries"] = p1.masteries;
        j["p1_units"] = SerializeUnits(p1.units);

        j["p2_prestige_name"] = p2.prestige_name;
        j["p2_masteries"] = p2.masteries;
        j["p2_units"] = SerializeUnits(p2.units);

        j["amon_units"] = SerializeUnits(amon_units);

        auto SerializeSeries = [](const PlayerTimeSeries& s) {
            nlohmann::json sj;
            sj["times"] = s.time_stamps;
            sj["army"] = s.army_value;
            sj["supply"] = s.supply_used;
            sj["killed"] = s.kill_count;
            sj["mining"] = s.collection_rate;
            return sj;
        };

        j["p1_series"] = SerializeSeries(p1_series);
        j["p2_series"] = SerializeSeries(p2_series);

        return j.dump();
    }

    void DetailsFromJson(const std::string& json_str) {
        if (json_str.empty()) return;
        try {
            auto j = nlohmann::json::parse(json_str);
            enemy_comp = j.value("enemy_comp", "");
            bonus_earned = j.value("bonus_earned", 0);
            bonus_total = j.value("bonus_total", 0);

            auto DeserializeUnits = [](const nlohmann::json& arr) {
                std::vector<UnitStat> out;
                if (!arr.is_array()) return out;
                for (const auto& item : arr) {
                    UnitStat u;
                    u.name = item.value("name", "");
                    u.created = item.value("created", 0);
                    u.lost = item.value("lost", 0);
                    u.kills = item.value("kills", 0);
                    u.kill_fraction = item.value("fraction", 0.0f);
                    out.push_back(u);
                }
                return out;
            };

            if (j.contains("p1_prestige_name")) p1.prestige_name = j["p1_prestige_name"];
            if (j.contains("p1_masteries") && j["p1_masteries"].is_array()) {
                p1.masteries = j["p1_masteries"].get<std::vector<int>>();
            }
            if (j.contains("p1_units")) p1.units = DeserializeUnits(j["p1_units"]);

            if (j.contains("p2_prestige_name")) p2.prestige_name = j["p2_prestige_name"];
            if (j.contains("p2_masteries") && j["p2_masteries"].is_array()) {
                p2.masteries = j["p2_masteries"].get<std::vector<int>>();
            }
            if (j.contains("p2_units")) p2.units = DeserializeUnits(j["p2_units"]);

            if (j.contains("amon_units")) amon_units = DeserializeUnits(j["amon_units"]);

            auto DeserializeSeries = [](const nlohmann::json& sj, PlayerTimeSeries& s) {
                if (sj.contains("times") && sj["times"].is_array()) s.time_stamps = sj["times"].get<std::vector<float>>();
                if (sj.contains("army") && sj["army"].is_array()) s.army_value = sj["army"].get<std::vector<float>>();
                if (sj.contains("supply") && sj["supply"].is_array()) s.supply_used = sj["supply"].get<std::vector<float>>();
                if (sj.contains("killed") && sj["killed"].is_array()) s.kill_count = sj["killed"].get<std::vector<float>>();
                if (sj.contains("mining") && sj["mining"].is_array()) s.collection_rate = sj["mining"].get<std::vector<float>>();
            };

            if (j.contains("p1_series")) DeserializeSeries(j["p1_series"], p1_series);
            if (j.contains("p2_series")) DeserializeSeries(j["p2_series"], p2_series);
        } catch (...) {}
    }

    std::string MutatorsToJson() const {
        nlohmann::json j = mutators;
        return j.dump();
    }

    std::string ChatToJson() const {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& c : chat) {
            j.push_back({
                {"time", c.time_seconds},
                {"pid", c.player_id},
                {"name", c.player_name},
                {"text", c.text}
            });
        }
        return j.dump();
    }

    void MutatorsFromJson(const std::string& json_str) {
        mutators.clear();
        if (json_str.empty()) return;
        try {
            auto j = nlohmann::json::parse(json_str);
            if (j.is_array()) {
                mutators = j.get<std::vector<std::string>>();
            }
        } catch (...) {}
    }

    void ChatFromJson(const std::string& json_str) {
        chat.clear();
        if (json_str.empty()) return;
        try {
            auto j = nlohmann::json::parse(json_str);
            if (j.is_array()) {
                for (const auto& item : j) {
                    ChatRecord cr;
                    cr.time_seconds = item.value("time", 0.0);
                    cr.player_id = item.value("pid", 1);
                    cr.player_name = item.value("name", "");
                    cr.text = item.value("text", "");
                    chat.push_back(cr);
                }
            }
        } catch (...) {}
    }
};

} // namespace sc2
