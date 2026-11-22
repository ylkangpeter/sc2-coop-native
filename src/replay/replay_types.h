#pragma once

#include <string>
#include <vector>
#include <array>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <zlib/zlib.h>

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
    int commander_level = 0;
    int commander_mastery_level = 0;
    int apm = 0;
    int kills = 0;
    std::string result; // "Win" or "Loss"
    std::vector<int> masteries = {0, 0, 0, 0, 0, 0}; // 6 mastery values
    std::vector<UnitStat> units;

    std::string FormatLevel() const {
        if (commander.empty() || commander == "Unknown") return "";
        if (commander_level > 0 && commander_level < 15) {
            return "{" + std::to_string(commander_level) + "}";
        } else if (commander_level >= 15) {
            if (commander_mastery_level > 0) {
                return "{" + std::to_string(commander_mastery_level) + "}";
            } else {
                return "{15}";
            }
        }
        return "";
    }

    bool IsLevelMaxed() const {
        if (commander_level > 0 && commander_level < 15) {
            return false;
        }
        return (commander_mastery_level > 0 || commander_level >= 15);
    }
};

struct ChatRecord {
    double time_seconds = 0.0;
    int player_id = 1;
    std::string player_name;
    std::string text;
};

struct ReplayRecord {
    static constexpr size_t kMaxStoredSeriesPoints = 256;

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
    bool has_level_data = false;
    std::string enemy_comp;
    std::vector<UnitStat> amon_units;
    int bonus_earned = 0;
    int bonus_total = 0;
    PlayerTimeSeries p1_series;
    PlayerTimeSeries p2_series;

    static void DownsampleVector(std::vector<float>& values) {
        if (values.size() <= kMaxStoredSeriesPoints) return;
        std::vector<float> sampled;
        sampled.reserve(kMaxStoredSeriesPoints);
        const size_t last = values.size() - 1;
        for (size_t i = 0; i < kMaxStoredSeriesPoints; ++i) {
            const size_t source = (i == kMaxStoredSeriesPoints - 1)
                ? last
                : (i * last) / (kMaxStoredSeriesPoints - 1);
            sampled.push_back(values[source]);
        }
        values.swap(sampled);
    }

    static void DownsampleSeries(PlayerTimeSeries& series) {
        DownsampleVector(series.time_stamps);
        DownsampleVector(series.army_value);
        DownsampleVector(series.supply_used);
        DownsampleVector(series.kill_count);
        DownsampleVector(series.collection_rate);
    }

    static std::vector<uint8_t> CompressJson(const std::string& json) {
        if (json.empty()) return {};
        uLongf compressed_size = compressBound(static_cast<uLong>(json.size()));
        std::vector<uint8_t> compressed(compressed_size);
        if (compress2(compressed.data(), &compressed_size,
                      reinterpret_cast<const Bytef*>(json.data()),
                      static_cast<uLong>(json.size()), Z_BEST_COMPRESSION) != Z_OK) {
            return {};
        }
        compressed.resize(compressed_size);
        return compressed;
    }

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

        j["level_cache_version"] = 2;
        j["has_level_data"] = has_level_data;
        j["p1_commander_level"] = p1.commander_level;
        j["p1_commander_mastery_level"] = p1.commander_mastery_level;
        j["p1_prestige_name"] = p1.prestige_name;
        j["p1_masteries"] = p1.masteries;
        j["p1_units"] = SerializeUnits(p1.units);

        j["p2_commander_level"] = p2.commander_level;
        j["p2_commander_mastery_level"] = p2.commander_mastery_level;
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

        PlayerTimeSeries compact_p1 = p1_series;
        PlayerTimeSeries compact_p2 = p2_series;
        DownsampleSeries(compact_p1);
        DownsampleSeries(compact_p2);
        j["p1_series"] = SerializeSeries(compact_p1);
        j["p2_series"] = SerializeSeries(compact_p2);

        return j.dump();
    }

    std::vector<uint8_t> DetailsToCompressedBlob() const {
        return CompressJson(DetailsToJson());
    }

    bool DetailsFromCompressedBlob(const void* data, size_t size) {
        if (!data || size == 0 || size > (64u * 1024u * 1024u)) return false;
        constexpr size_t kMaxOutputSize = 64u * 1024u * 1024u;
        const size_t initial_size = std::min(kMaxOutputSize, std::max<size_t>(size * 8, 4096));
        std::vector<uint8_t> output(initial_size);
        z_stream stream{};
        stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data));
        stream.avail_in = static_cast<uInt>(size);
        if (inflateInit(&stream) != Z_OK) return false;
        int result = Z_OK;
        while (result == Z_OK) {
            if (stream.total_out == output.size()) {
                if (output.size() >= kMaxOutputSize) {
                    inflateEnd(&stream);
                    return false;
                }
                output.resize(std::min<size_t>(output.size() * 2, 64u * 1024u * 1024u));
            }
            stream.next_out = output.data() + stream.total_out;
            stream.avail_out = static_cast<uInt>(output.size() - stream.total_out);
            result = inflate(&stream, Z_NO_FLUSH);
            if (result == Z_BUF_ERROR && stream.avail_out != 0) break;
        }
        const size_t output_size = stream.total_out;
        inflateEnd(&stream);
        if (result != Z_STREAM_END) return false;
        return DetailsFromJson(std::string(reinterpret_cast<const char*>(output.data()), output_size));
    }

    bool DetailsFromJson(const std::string& json_str) {
        if (json_str.empty()) return false;
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

            const bool legacy_defaults = !j.contains("has_level_data");
            auto read_levels = [&](PlayerInfo& player, const char* level_key, const char* mastery_key) {
                const auto level = j.find(level_key);
                const auto mastery = j.find(mastery_key);
                const bool valid = level != j.end() && level->is_number_integer() &&
                                   mastery != j.end() && mastery->is_number_integer();
                player.commander_level = valid ? level->get<int>() : 0;
                player.commander_mastery_level = valid ? mastery->get<int>() : 0;
                // Before 1.0.21 the heuristic decoder supplied 15 for missing levels.
                if (legacy_defaults && player.commander_level == 15 && player.commander_mastery_level == 0) {
                    player.commander_level = 0;
                    return false;
                }
                return valid;
            };
            const bool p1_valid = read_levels(p1, "p1_commander_level", "p1_commander_mastery_level");
            const bool p2_valid = read_levels(p2, "p2_commander_level", "p2_commander_mastery_level");
            has_level_data = p1_valid && p2_valid && j.value("has_level_data", true);
            // 1.0.21 also marked old JSON converted to blobs as checked, with four zeroes.
            if (!j.contains("level_cache_version") && p1.commander_level == 0 && p2.commander_level == 0 &&
                p1.commander_mastery_level == 0 && p2.commander_mastery_level == 0) {
                has_level_data = false;
            }
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
            return true;
        } catch (...) {
            return false;
        }
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
