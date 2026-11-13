#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <mutex>
#include "../replay/replay_types.h"

struct sqlite3;

namespace sc2 {

struct PlayerRecord {
    std::string name;
    int total_games = 0;
    int wins = 0;
    int losses = 0;
    double winrate = 0.0;
    int median_apm = 0;
    double median_kill_share = 50.0;
    std::string top_commander = "-";
    double top_commander_freq = 0.0;
    std::string note;
    int64_t last_played_mtime = 0;
};

struct MapStat {
    std::string map_name;
    int fastest_seconds = 0;
    std::string fastest_str = "-";
    int avg_seconds = 0;
    std::string avg_str = "-";
    int wins = 0;
    int losses = 0;
    double winrate = 0.0;
    double frequency = 0.0;
    double bonus_rate = 100.0;
};

struct CommanderStat {
    std::string commander;
    int total_games = 0;
    int wins = 0;
    int losses = 0;
    double winrate = 0.0;
    double frequency = 0.0;
    int avg_apm = 0;
    double avg_kill_share = 50.0;
};

struct StatsFilter {
    bool casual = true;
    bool normal = true;
    bool hard = true;
    bool brutal = true;
    bool brutal_plus = true;

    bool region_na = true;
    bool region_eu = true;
    bool region_kr = true;
    bool region_cn = true;

    bool normal_games = true;
    bool mutations = true;
    bool wins_only = false;

    std::string date_from = "2015-11-10";
    std::string date_to = "2030-12-31";
    int min_length_minutes = 0;
    int max_length_minutes = 1000;
    std::string ally_name_filter;
};

struct DifficultyStat {
    std::string difficulty; // "Casual", "Normal", "Hard", "Brutal", "Brutal+1", ..., "Brutal+6"
    int wins = 0;
    int losses = 0;
    int total = 0;
    double winrate = 0.0;
};

struct RegionStat {
    std::string region;     // "NA", "EU", "KR", "CN"
    int wins = 0;
    int losses = 0;
    int total = 0;
    double winrate = 0.0;
};

struct StatsSummary {
    int total_games = 0;
    int total_wins = 0;
    int total_losses = 0;
    double overall_winrate = 0.0;
    std::vector<DifficultyStat> difficulties;
    std::vector<RegionStat> regions;
    std::vector<MapStat> maps;
    std::vector<CommanderStat> my_commanders;
    std::vector<CommanderStat> ally_commanders;
};

class Database {
public:
    static Database& Instance();

    bool Open(const std::string& db_path);
    void Close();
    bool IsOpen() const { return m_db != nullptr; }

    // Replay CRUD
    bool SaveReplay(const ReplayRecord& rec);
    bool BatchSaveReplays(const std::vector<ReplayRecord>& recs);
    bool HasReplay(const std::string& file_path);
    bool HasValidReplay(const std::string& file_path);
    std::vector<std::string> GetCorruptedReplayPaths();
    void SanitizeInvalidPrestiges();
    int GetTotalReplayCount();

    // Query games for Games Tab
    std::vector<ReplayRecord> QueryReplays(const std::string& search_query,
                                          int limit, int offset,
                                          int& out_total_count);

    // Query players for Players Tab
    std::vector<PlayerRecord> QueryPlayers(const std::string& search_filter,
                                          const std::string& sort_column,
                                          bool sort_ascending,
                                          int limit, int offset,
                                          int& out_total_count);

    // Player Notes
    bool SetPlayerNote(const std::string& player_name, const std::string& note);
    std::string GetPlayerNote(const std::string& player_name);

    // Chat update
    bool UpdateReplayChat(const std::string& file_path, const std::string& chat_json);

    // Settings storage
    bool SetSetting(const std::string& key, const std::string& value);
    std::string GetSetting(const std::string& key, const std::string& default_val = "");

    // Stats calculations
    StatsSummary CalculateStats(const StatsFilter& filter);

    // Weekly mutation history matching
    struct WeeklyRecord {
        int wins = 0;
        int losses = 0;
        double winrate = 0.0;
        std::string highest_diff;
        std::vector<std::string> replay_files;
    };
    WeeklyRecord QueryWeeklyRecord(const std::string& map_name, const std::vector<std::string>& mutators);

    // Export
    bool DumpAllToJson(const std::string& output_path);

private:
    Database() = default;
    ~Database();

    mutable std::recursive_mutex m_mutex;
    sqlite3* m_db = nullptr;
};

} // namespace sc2
