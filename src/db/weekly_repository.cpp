#include "database.h"
#include "sqlite_raii.h"
#include <sqlite3.h>
#include <vector>
#include <string>
#include <unordered_map>
#include "../replay/mpq_reader.h"
#include "../replay/s2_decoder.h"
#include "../data/weekly_mutations.h"
#include <nlohmann/json.hpp>

namespace sc2 {

namespace {

static int DiffRank(const std::string& d) {
    if (d == "Brutal+6") return 10;
    if (d == "Brutal+5") return 9;
    if (d == "Brutal+4") return 8;
    if (d == "Brutal+3") return 7;
    if (d == "Brutal+2") return 6;
    if (d == "Brutal+1") return 5;
    if (d == "Brutal") return 4;
    if (d.find("Hard") != std::string::npos) return 3;
    if (d.find("Normal") != std::string::npos) return 2;
    if (d.find("Casual") != std::string::npos) return 1;
    return 0;
}

} // namespace

Database::WeeklyRecord Database::QueryWeeklyRecord(const std::string& map_name, const std::vector<std::string>& mutators) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    WeeklyRecord wr;
    if (!m_db) return wr;

    std::string sql = "SELECT result, difficulty, file_path, duration_seconds FROM replays WHERE map_name = ? AND is_weekly = 1";
    for (size_t i = 0; i < mutators.size(); ++i) {
        sql += " AND mutators_json LIKE ?";
    }
    sql += ";";

    SqliteStmt stmt(m_db, sql);
    if (!stmt.IsValid()) {
        return wr;
    }

    int pidx = 1;
    stmt.BindText(pidx++, map_name);
    for (const auto& m : mutators) {
        std::string pat = "%" + m + "%";
        stmt.BindText(pidx++, pat);
    }

    while (stmt.Step() == SQLITE_ROW) {
        std::string res = stmt.ColumnText(0);
        std::string diff = stmt.ColumnText(1);
        std::string fp = stmt.ColumnText(2);
        int dur = stmt.ColumnInt(3);

        if (res == "Victory") {
            wr.wins++;
            if (DiffRank(diff) >= DiffRank(wr.highest_diff)) {
                wr.highest_diff = diff;
            }
            if (wr.fastest_win_seconds == 0 || (dur > 0 && dur < wr.fastest_win_seconds)) {
                wr.fastest_win_seconds = dur;
            }
        } else if (res == "Defeat") {
            wr.losses++;
        }
        wr.replay_files.push_back(fp);
    }

    int tot = wr.wins + wr.losses;
    if (tot > 0) {
        wr.winrate = (100.0 * wr.wins) / tot;
    }
    return wr;
}

std::unordered_map<int, Database::WeeklyRecord> Database::QueryAllWeeklyRecords() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::unordered_map<int, WeeklyRecord> result;
    if (!m_db) return result;

    const char* sql = "SELECT result, difficulty, file_path, map_name, mutators_json, duration_seconds FROM replays WHERE is_weekly = 1 OR (mutators_json IS NOT NULL AND mutators_json != '[]' AND mutators_json != '');";
    SqliteStmt stmt(m_db, sql);
    if (!stmt.IsValid()) {
        return result;
    }

    struct ReplayWeeklyItem {
        std::string result;
        std::string difficulty;
        std::string file_path;
        std::string map_name;
        std::string mutators_json;
        int duration_seconds = 0;
    };
    std::vector<ReplayWeeklyItem> items;
    while (stmt.Step() == SQLITE_ROW) {
        ReplayWeeklyItem it;
        it.result = stmt.ColumnText(0);
        it.difficulty = stmt.ColumnText(1);
        it.file_path = stmt.ColumnText(2);
        it.map_name = stmt.ColumnText(3);
        it.mutators_json = stmt.ColumnText(4);
        it.duration_seconds = stmt.ColumnInt(5);
        items.push_back(std::move(it));
    }
    stmt.Finalize();

    if (items.empty()) return result;

    for (const auto& wm : kWeeklyMutations) {
        WeeklyRecord wr;
        for (const auto& item : items) {
            if (item.map_name != wm.map_name) continue;
            bool all_matched = true;
            for (const char* mut : wm.mutators) {
                if (item.mutators_json.find(mut) == std::string::npos) {
                    all_matched = false;
                    break;
                }
            }
            if (all_matched) {
                if (item.result == "Victory") {
                    wr.wins++;
                    if (DiffRank(item.difficulty) >= DiffRank(wr.highest_diff)) {
                        wr.highest_diff = item.difficulty;
                    }
                    if (wr.fastest_win_seconds == 0 || (item.duration_seconds > 0 && item.duration_seconds < wr.fastest_win_seconds)) {
                        wr.fastest_win_seconds = item.duration_seconds;
                    }
                } else if (item.result == "Defeat") {
                    wr.losses++;
                }
                wr.replay_files.push_back(item.file_path);
            }
        }
        if (wr.wins > 0 || wr.losses > 0) {
            int tot = wr.wins + wr.losses;
            wr.winrate = tot > 0 ? (100.0 * wr.wins / tot) : 0.0;
            if (wr.highest_diff.empty()) wr.highest_diff = "Brutal";
            result[wm.week_number] = std::move(wr);
        }
    }

    return result;
}

void Database::RepairWeeklyMutationsIfNeeded() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db) return;

    // Fix for Bug 5: Check migration version rather than checking if 1 row exists
    SqliteStmt vstmt(m_db, "SELECT value FROM settings WHERE key = 'weekly_repair_version' LIMIT 1;");
    int version = 0;
    if (vstmt.IsValid() && vstmt.Step() == SQLITE_ROW) {
        version = vstmt.ColumnInt(0);
    }
    if (version >= 1) return; // Already migrated

    // Select replays that may be un-repaired weekly mutations
    const char* select_sql = "SELECT file_path FROM replays WHERE is_weekly = 0 OR mutators_json IS NULL OR mutators_json = '' OR mutators_json = '[]';";
    SqliteStmt fetch_stmt(m_db, select_sql);
    if (!fetch_stmt.IsValid()) {
        return;
    }
    std::vector<std::string> paths;
    while (fetch_stmt.Step() == SQLITE_ROW) {
        std::string p = fetch_stmt.ColumnText(0);
        if (!p.empty()) paths.emplace_back(std::move(p));
    }
    fetch_stmt.Finalize();

    if (!paths.empty()) {
        std::vector<std::pair<std::string, std::string>> updates;
        for (const auto& path : paths) {
            std::vector<uint8_t> initdata;
            if (MpqReader::ReadFile(path, "replay.initData", initdata)) {
                std::vector<std::string> handles;
                std::vector<std::string> mutators;
                bool is_weekly = false;
                S2Decoder::DecodeInitDataSlots(initdata, handles, mutators, is_weekly);
                if (is_weekly && !mutators.empty()) {
                    nlohmann::json j = mutators;
                    updates.emplace_back(j.dump(), path);
                }
            }
        }

        if (!updates.empty()) {
            SqliteStmt update_stmt(m_db, "UPDATE replays SET is_weekly = 1, mutators_json = ? WHERE file_path = ?;");
            if (update_stmt.IsValid()) {
                SqliteTransaction txn(m_db);
                for (const auto& [mj, path] : updates) {
                    update_stmt.BindText(1, mj);
                    update_stmt.BindText(2, path);
                    update_stmt.Step();
                    update_stmt.Reset();
                }
                if (txn.Commit()) {
                    BumpRevision();
                }
            }
        }
    }

    SetSetting("weekly_repair_version", "1");
}

} // namespace sc2
