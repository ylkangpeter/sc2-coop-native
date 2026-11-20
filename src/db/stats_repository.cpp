#include "database.h"
#include "sqlite_raii.h"
#include <sqlite3.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include "../replay/replay_parser.h"
#include "../app_state.h"

namespace sc2 {

namespace {
// Interpret the replay's calendar date directly, without local timezone conversion.
std::optional<int64_t> CareerDayEpoch(const std::string& date) {
    if (date.size() < 10 || date[4] != '-' || date[7] != '-') return std::nullopt;
    for (int i : {0, 1, 2, 3, 5, 6, 8, 9}) {
        if (date[i] < '0' || date[i] > '9') return std::nullopt;
    }
    const int year = (date[0] - '0') * 1000 + (date[1] - '0') * 100 +
                     (date[2] - '0') * 10 + date[3] - '0';
    const unsigned month = (date[5] - '0') * 10 + date[6] - '0';
    const unsigned day = (date[8] - '0') * 10 + date[9] - '0';
    const auto calendar_date = std::chrono::year{year} / month / day;
    if (year == 0 || !calendar_date.ok()) return std::nullopt;
    return std::chrono::sys_seconds{std::chrono::sys_days{calendar_date}}.time_since_epoch().count();
}
} // namespace

StatsSummary Database::CalculateStats(const StatsFilter& filter) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    StatsSummary sum;
    if (!m_db) return sum;

    // Build dynamic WHERE clause based on StatsFilter
    std::string where_clause = " WHERE 1=1";

    // 1. Difficulty filter (inclusion-based)
    std::vector<std::string> diff_conds;
    if (filter.casual) diff_conds.push_back("(difficulty LIKE '%Casual%' AND (brutal_plus IS NULL OR brutal_plus = 0))");
    if (filter.normal) diff_conds.push_back("(difficulty LIKE '%Normal%' AND (brutal_plus IS NULL OR brutal_plus = 0))");
    if (filter.hard) diff_conds.push_back("(difficulty LIKE '%Hard%' AND (brutal_plus IS NULL OR brutal_plus = 0))");
    if (filter.brutal) diff_conds.push_back("(difficulty LIKE '%Brutal%' AND (brutal_plus IS NULL OR brutal_plus = 0))");
    if (filter.brutal_plus) diff_conds.push_back("(brutal_plus > 0 OR difficulty LIKE '%Brutal+%')");

    if (diff_conds.empty()) {
        where_clause += " AND 1=0";
    } else if (diff_conds.size() < 5) {
        where_clause += " AND (";
        for (size_t i = 0; i < diff_conds.size(); ++i) {
            if (i > 0) where_clause += " OR ";
            where_clause += diff_conds[i];
        }
        where_clause += ")";
    }

    // 2. Region filter
    std::vector<std::string> reg_conds;
    if (filter.region_na) reg_conds.push_back("region = 'NA'");
    if (filter.region_eu) reg_conds.push_back("region = 'EU'");
    if (filter.region_kr) reg_conds.push_back("region = 'KR'");
    if (filter.region_cn) reg_conds.push_back("region = 'CN'");

    if (reg_conds.empty()) {
        where_clause += " AND 1=0";
    } else if (reg_conds.size() < 4) {
        where_clause += " AND (";
        for (size_t i = 0; i < reg_conds.size(); ++i) {
            if (i > 0) where_clause += " OR ";
            where_clause += reg_conds[i];
        }
        where_clause += ")";
    }

    // 3. Game type (normal vs weekly mutation)
    if (filter.normal_games && !filter.mutations) {
        where_clause += " AND is_weekly = 0";
    } else if (!filter.normal_games && filter.mutations) {
        where_clause += " AND is_weekly = 1";
    } else if (!filter.normal_games && !filter.mutations) {
        where_clause += " AND 1=0";
    }

    // 4. Wins only
    if (filter.wins_only) {
        where_clause += " AND result = 'Victory'";
    }

    std::string date_from_val;
    std::string date_to_val;
    std::string ally_pattern;

    if (!filter.date_from.empty() && filter.date_from != "2015-11-10") {
        where_clause += " AND game_date >= ?";
        date_from_val = filter.date_from;
    }
    if (!filter.date_to.empty() && filter.date_to != "2030-12-31") {
        std::string to = filter.date_to;
        if (to.length() == 10) to += " 23:59:59";
        where_clause += " AND game_date <= ?";
        date_to_val = to;
    }

    if (filter.min_length_minutes > 0) {
        where_clause += " AND duration_seconds >= " + std::to_string(filter.min_length_minutes * 60);
    }
    if (filter.max_length_minutes > 0 && filter.max_length_minutes < 1000) {
        where_clause += " AND duration_seconds <= " + std::to_string(filter.max_length_minutes * 60);
    }
    if (!filter.ally_name_filter.empty()) {
        where_clause += " AND (CASE WHEN is_user_handle(p2_handle) OR (p2_handle != '' AND instr(file_path, p2_handle) > 0) THEN p1_name ELSE p2_name END) LIKE ?";
        ally_pattern = "%" + filter.ally_name_filter + "%";
    }

    // Unified Single-Pass Query across the filtered dataset
    std::string sql = "SELECT map_name, result, duration_seconds, p1_commander, p1_apm, p2_commander, p2_apm, difficulty, brutal_plus, region, file_path, p2_handle, p1_handle, game_date FROM replays" + where_clause + ";";

    SqliteStmt stmt(m_db, sql);
    if (!stmt.IsValid()) {
        return sum;
    }

    int pidx = 1;
    if (!date_from_val.empty()) {
        stmt.BindText(pidx++, date_from_val);
    }
    if (!date_to_val.empty()) {
        stmt.BindText(pidx++, date_to_val);
    }
    if (!ally_pattern.empty()) {
        stmt.BindText(pidx++, ally_pattern);
    }

    struct MapAcc {
        int fastest_seconds = 0;
        int64_t total_seconds = 0;
        int wins = 0;
        int losses = 0;
    };
    std::unordered_map<std::string, MapAcc> map_map;

    struct CmdrAcc {
        int total_games = 0;
        int wins = 0;
        int losses = 0;
        int64_t total_apm = 0;
        int64_t total_duration_seconds = 0;
    };
    std::unordered_map<std::string, CmdrAcc> p1_map;
    std::unordered_map<std::string, CmdrAcc> p2_map;

    struct DiffAcc {
        int wins = 0;
        int losses = 0;
        int total = 0;
    };
    std::unordered_map<std::string, DiffAcc> diff_map;

    struct RegAcc {
        int wins = 0;
        int losses = 0;
        int total = 0;
    };
    std::unordered_map<std::string, RegAcc> reg_map;
    std::unordered_set<std::string> active_dates;
    std::map<int64_t, DailyCareerStat> daily_activity;

    static const std::unordered_set<std::string> kKnownCommanders = {
        "Raynor", "Kerrigan", "Artanis", "Swann", "Zagara", "Vorazun",
        "Karax", "Abathur", "Alarak", "Nova", "Stukov", "Fenix",
        "Dehaka", "HanHorner", "Tychus", "Zeratul", "Stetmann", "Mengsk"
    };

    while (stmt.Step() == SQLITE_ROW) {
        std::string map_name_str = stmt.ColumnText(0);
        std::string result = stmt.ColumnText(1);
        int dur = stmt.ColumnInt(2);
        std::string p1c_str = stmt.ColumnText(3);
        int p1_apm = stmt.ColumnInt(4);
        std::string p2c_str = stmt.ColumnText(5);
        int p2_apm = stmt.ColumnInt(6);
        std::string diff_str = stmt.ColumnText(7);
        int bp = stmt.ColumnInt(8);
        std::string reg_str = stmt.ColumnText(9);
        std::string path_str = stmt.ColumnText(10);
        std::string p2h_str = stmt.ColumnText(11);
        std::string p1h_str = stmt.ColumnText(12);
        std::string date_str = stmt.ColumnText(13);

        bool p2_is_me = (!p2h_str.empty() && (AppState::Instance().HasUserHandle(p2h_str) || (!path_str.empty() && path_str.find(p2h_str) != std::string::npos)));
        bool p1_is_user = (!p1h_str.empty() && AppState::Instance().HasUserHandle(p1h_str));
        bool p2_is_user = (!p2h_str.empty() && AppState::Instance().HasUserHandle(p2h_str));
        if (p1_is_user && !p2_is_user) {
            p2_is_me = false;
        }

        const std::string& my_cmdr_str = p2_is_me ? p2c_str : p1c_str;
        int my_apm = p2_is_me ? p2_apm : p1_apm;
        const std::string& ally_cmdr_str = p2_is_me ? p1c_str : p2c_str;
        int ally_apm = p2_is_me ? p1_apm : p2_apm;

        std::string map_name = !map_name_str.empty() ? map_name_str : "Unknown";
        if (result.empty()) result = "Defeat";
        if (result == "Unknown") {
            // Skip damaged or incomplete replays from win/loss statistics
            continue;
        }
        bool is_victory = (result == "Victory");

        sum.total_games++;
        if (is_victory) sum.total_wins++;
        else sum.total_losses++;

        const int valid_duration = std::max(dur, 0);
        sum.total_duration_seconds += valid_duration;
        sum.longest_duration_seconds = std::max(sum.longest_duration_seconds, valid_duration);
        if (dur > 0 && (sum.shortest_duration_seconds == 0 || dur < sum.shortest_duration_seconds)) {
            sum.shortest_duration_seconds = dur;
        }
        if (auto day = CareerDayEpoch(date_str)) {
            auto& daily = daily_activity[*day];
            daily.day_epoch_seconds = *day;
            daily.total_duration_seconds += valid_duration;
            daily.total_games++;
        }
        if (!date_str.empty()) {
            if (sum.first_game_date.empty() || date_str < sum.first_game_date) sum.first_game_date = date_str;
            if (sum.last_game_date.empty() || date_str > sum.last_game_date) sum.last_game_date = date_str;
            active_dates.insert(date_str.size() >= 10 ? date_str.substr(0, 10) : date_str);
        }

        // Maps
        auto& ma = map_map[map_name];
        if (is_victory) ma.wins++; else ma.losses++;
        ma.total_seconds += valid_duration;
        if (dur > 0 && (ma.fastest_seconds == 0 || dur < ma.fastest_seconds)) {
            ma.fastest_seconds = dur;
        }

        // My Commander
        if (!my_cmdr_str.empty() && my_cmdr_str != "Unknown") {
            std::string cmdr = (my_cmdr_str == "Horner") ? "HanHorner" : my_cmdr_str;
            if (kKnownCommanders.count(cmdr)) {
                auto& c = p1_map[cmdr];
                c.total_games++;
                if (is_victory) c.wins++; else c.losses++;
                c.total_apm += my_apm;
                c.total_duration_seconds += valid_duration;
            }
        }

        // Ally Commander
        if (!ally_cmdr_str.empty() && ally_cmdr_str != "Unknown") {
            std::string cmdr = (ally_cmdr_str == "Horner") ? "HanHorner" : ally_cmdr_str;
            if (kKnownCommanders.count(cmdr)) {
                auto& c = p2_map[cmdr];
                c.total_games++;
                if (is_victory) c.wins++; else c.losses++;
                c.total_apm += ally_apm;
                c.total_duration_seconds += valid_duration;
            }
        }

        // Difficulty
        std::string d_label;
        if (bp >= 1 && bp <= 6) {
            d_label = "Brutal+" + std::to_string(bp);
        } else if (!diff_str.empty()) {
            d_label = diff_str;
        }
        if (!d_label.empty() && d_label.find('/') == std::string::npos) {
            auto& da = diff_map[d_label];
            da.total++;
            if (is_victory) da.wins++; else da.losses++;
        }

        // Region
        if (reg_str == "NA" || reg_str == "EU" || reg_str == "KR" || reg_str == "CN") {
            auto& ra = reg_map[reg_str];
            ra.total++;
            if (is_victory) ra.wins++; else ra.losses++;
        }
    }

    sum.avg_duration_seconds = sum.total_games > 0
        ? static_cast<int>(sum.total_duration_seconds / sum.total_games)
        : 0;
    sum.active_days = static_cast<int>(active_dates.size());
    sum.daily_activity.reserve(daily_activity.size());
    for (const auto& [day, activity] : daily_activity) {
        sum.daily_activity.push_back(activity);
    }

    if (sum.total_games > 0) {
        sum.overall_winrate = (100.0 * sum.total_wins) / sum.total_games;
    }

    // Populate maps sorted by total games descending
    for (auto& [name, acc] : map_map) {
        MapStat ms;
        ms.map_name = name;
        ms.fastest_seconds = acc.fastest_seconds;
        ms.fastest_str = ReplayParser::FormatDuration(acc.fastest_seconds);
        int tot = acc.wins + acc.losses;
        ms.avg_seconds = tot > 0 ? static_cast<int>(acc.total_seconds / tot) : 0;
        ms.avg_str = ReplayParser::FormatDuration(ms.avg_seconds);
        ms.wins = acc.wins;
        ms.losses = acc.losses;
        ms.winrate = tot > 0 ? (100.0 * ms.wins / tot) : 0.0;
        ms.frequency = sum.total_games > 0 ? (100.0 * tot / sum.total_games) : 0.0;
        sum.maps.push_back(std::move(ms));
    }
    std::sort(sum.maps.begin(), sum.maps.end(), [](const MapStat& a, const MapStat& b) {
        return (a.wins + a.losses) > (b.wins + b.losses);
    });

    // Populate P1 commanders sorted by total descending
    for (auto& [cmdr, acc] : p1_map) {
        CommanderStat cs;
        cs.commander = cmdr;
        cs.total_games = acc.total_games;
        cs.wins = acc.wins;
        cs.losses = acc.losses;
        cs.winrate = acc.total_games > 0 ? (100.0 * acc.wins / acc.total_games) : 0.0;
        cs.frequency = sum.total_games > 0 ? (100.0 * acc.total_games / sum.total_games) : 0.0;
        cs.avg_apm = acc.total_games > 0 ? static_cast<int>(acc.total_apm / acc.total_games) : 0;
        cs.total_duration_seconds = acc.total_duration_seconds;
        cs.time_share = sum.total_duration_seconds > 0
            ? (100.0 * acc.total_duration_seconds / sum.total_duration_seconds) : 0.0;
        sum.my_commanders.push_back(std::move(cs));
    }
    std::sort(sum.my_commanders.begin(), sum.my_commanders.end(), [](const CommanderStat& a, const CommanderStat& b) {
        return a.total_games > b.total_games;
    });

    // Populate P2 commanders sorted by total descending
    for (auto& [cmdr, acc] : p2_map) {
        CommanderStat cs;
        cs.commander = cmdr;
        cs.total_games = acc.total_games;
        cs.wins = acc.wins;
        cs.losses = acc.losses;
        cs.winrate = acc.total_games > 0 ? (100.0 * acc.wins / acc.total_games) : 0.0;
        cs.frequency = sum.total_games > 0 ? (100.0 * acc.total_games / sum.total_games) : 0.0;
        cs.avg_apm = acc.total_games > 0 ? static_cast<int>(acc.total_apm / acc.total_games) : 0;
        cs.total_duration_seconds = acc.total_duration_seconds;
        cs.time_share = sum.total_duration_seconds > 0
            ? (100.0 * acc.total_duration_seconds / sum.total_duration_seconds) : 0.0;
        sum.ally_commanders.push_back(std::move(cs));
    }
    std::sort(sum.ally_commanders.begin(), sum.ally_commanders.end(), [](const CommanderStat& a, const CommanderStat& b) {
        return a.total_games > b.total_games;
    });

    // Difficulty breakdown
    const std::vector<std::string> kDifficulties = {
        "Casual", "Normal", "Hard", "Brutal",
        "Brutal+1", "Brutal+2", "Brutal+3", "Brutal+4", "Brutal+5", "Brutal+6"
    };
    for (const auto& d : kDifficulties) {
        auto it = diff_map.find(d);
        if (it != diff_map.end()) {
            DifficultyStat ds;
            ds.difficulty = d;
            ds.wins = it->second.wins;
            ds.losses = it->second.losses;
            ds.total = it->second.total;
            ds.winrate = ds.total > 0 ? (100.0 * ds.wins / ds.total) : 0.0;
            sum.difficulties.push_back(std::move(ds));
        }
    }

    // Region breakdown
    const std::vector<std::string> kRegions = {"NA", "EU", "KR", "CN"};
    for (const auto& r : kRegions) {
        auto it = reg_map.find(r);
        if (it != reg_map.end()) {
            RegionStat rs;
            rs.region = r;
            rs.wins = it->second.wins;
            rs.losses = it->second.losses;
            rs.total = it->second.total;
            rs.winrate = rs.total > 0 ? (100.0 * rs.wins / rs.total) : 0.0;
            sum.regions.push_back(std::move(rs));
        }
    }

    return sum;
}

int Database::GetFastestSecondsForMap(const std::string& map_name) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db || map_name.empty()) return 0;
    SqliteStmt stmt(m_db, "SELECT MIN(duration_seconds) FROM replays WHERE map_name = ? AND result = 'Victory' AND duration_seconds > 0;");
    if (!stmt.IsValid()) return 0;
    stmt.BindText(1, map_name);
    if (stmt.Step() == SQLITE_ROW) {
        return stmt.ColumnInt(0);
    }
    return 0;
}

} // namespace sc2
