#include "database.h"
#include "sqlite_raii.h"
#include <sqlite3.h>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <nlohmann/json.hpp>

namespace sc2 {

namespace {

static std::vector<std::string> SplitTerms(const std::string& query) {
    std::vector<std::string> terms;
    std::istringstream iss(query);
    std::string term;
    while (iss >> term) {
        if (!term.empty()) {
            std::replace(term.begin(), term.end(), '_', ' ');
            terms.push_back(term);
        }
    }
    return terms;
}

} // namespace

bool Database::SaveReplay(const ReplayRecord& rec) {
    return BatchSaveReplays({rec});
}

bool Database::BatchSaveReplays(const std::vector<ReplayRecord>& recs) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db || recs.empty()) return false;

    const char* sql = R"(
        INSERT OR REPLACE INTO replays (
            file_path, file_mtime, game_date, map_name, raw_map_name,
            duration_seconds, form_length, result, difficulty, brutal_plus,
            enemy_race, region, is_weekly, mutators_json,
            p1_name, p1_handle, p1_commander, p1_prestige, p1_apm, p1_kills,
            p2_name, p2_handle, p2_commander, p2_prestige, p2_apm, p2_kills,
            chat_json, details_json, details_blob, details_format
        ) VALUES (
            ?, ?, ?, ?, ?,
            ?, ?, ?, ?, ?,
            ?, ?, ?, ?,
            ?, ?, ?, ?, ?, ?,
            ?, ?, ?, ?, ?, ?,
            ?, ?, ?, ?
        );
    )";

    SqliteStmt stmt(m_db, sql);
    if (!stmt.IsValid()) {
        std::cerr << "[DB ERROR] BatchSaveReplays prepare failed: " << sqlite3_errmsg(m_db) << "\n";
        return false;
    }

    SqliteTransaction txn(m_db);
    if (!txn.IsActive()) {
        std::cerr << "[DB ERROR] BatchSaveReplays BEGIN TRANSACTION failed: " << sqlite3_errmsg(m_db) << "\n";
        return false;
    }

    try {
        for (const auto& rec : recs) {
            stmt.BindText(1, rec.file_path);
            stmt.BindInt64(2, rec.file_mtime);
            stmt.BindText(3, rec.game_date);
            stmt.BindText(4, rec.map_name);
            stmt.BindText(5, rec.raw_map_name);
            stmt.BindInt(6, rec.duration_seconds);
            stmt.BindText(7, rec.form_length);
            stmt.BindText(8, rec.result);
            stmt.BindText(9, rec.difficulty);
            stmt.BindInt(10, (rec.brutal_plus >= 1 && rec.brutal_plus <= 6) ? rec.brutal_plus : 0);
            stmt.BindText(11, rec.enemy_race);
            stmt.BindText(12, rec.region);
            stmt.BindInt(13, rec.is_weekly ? 1 : 0);

            std::string mut_json = rec.MutatorsToJson();
            stmt.BindText(14, mut_json);

            stmt.BindText(15, rec.p1.name);
            stmt.BindText(16, rec.p1.handle);
            stmt.BindText(17, rec.p1.commander);
            stmt.BindInt(18, rec.p1.prestige);
            stmt.BindInt(19, rec.p1.apm);
            stmt.BindInt(20, rec.p1.kills);

            stmt.BindText(21, rec.p2.name);
            stmt.BindText(22, rec.p2.handle);
            stmt.BindText(23, rec.p2.commander);
            stmt.BindInt(24, rec.p2.prestige);
            stmt.BindInt(25, rec.p2.apm);
            stmt.BindInt(26, rec.p2.kills);

            std::string chat_json = rec.ChatToJson();
            stmt.BindText(27, chat_json);

            // Details compressed blob
            stmt.BindNull(28);
            std::vector<uint8_t> details_blob = rec.DetailsToCompressedBlob();
            if (details_blob.empty()) {
                stmt.BindNull(29);
            } else {
                stmt.BindBlob(29, details_blob);
            }
            stmt.BindInt(30, details_blob.empty() ? 0 : 1);

            int step_res = stmt.Step();
            if (step_res != SQLITE_DONE) {
                std::cerr << "[DB ERROR] BatchSaveReplays step failed: " << sqlite3_errmsg(m_db) << "\n";
                txn.Rollback();
                return false;
            }
            stmt.Reset();
        }
    } catch (const std::exception& e) {
        std::cerr << "[DB ERROR] BatchSaveReplays exception: " << e.what() << "\n";
        txn.Rollback();
        return false;
    } catch (...) {
        std::cerr << "[DB ERROR] BatchSaveReplays unknown exception\n";
        txn.Rollback();
        return false;
    }

    if (!txn.Commit()) {
        std::cerr << "[DB ERROR] BatchSaveReplays COMMIT failed: " << sqlite3_errmsg(m_db) << "\n";
        return false;
    }

    BumpRevision();
    return true;
}

bool Database::HasReplay(const std::string& file_path) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db) return false;
    SqliteStmt stmt(m_db, "SELECT 1 FROM replays WHERE file_path = ? LIMIT 1;");
    if (!stmt.IsValid()) return false;
    stmt.BindText(1, file_path);
    return stmt.Step() == SQLITE_ROW;
}

int64_t Database::GetReplayMtime(const std::string& file_path) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db) return -1;
    SqliteStmt stmt(m_db, "SELECT file_mtime FROM replays WHERE file_path = ? LIMIT 1;");
    if (!stmt.IsValid()) return -1;
    stmt.BindText(1, file_path);
    if (stmt.Step() == SQLITE_ROW) {
        return stmt.ColumnInt64(0);
    }
    return -1;
}

bool Database::HasValidReplay(const std::string& file_path) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db) return false;
    const char* sql = "SELECT 1 FROM replays WHERE file_path = ? AND result IN ('Victory', 'Defeat', 'Unknown') AND p1_prestige >= 0 AND p1_prestige <= 3 AND p2_prestige >= 0 AND p2_prestige <= 3 AND brutal_plus >= 0 AND brutal_plus <= 6 AND p2_name IS NOT NULL AND p2_name != '' AND (p1_commander != 'Unknown' OR raw_map_name LIKE '[CM]%' OR raw_map_name LIKE '%RTC%') LIMIT 1;";
    SqliteStmt stmt(m_db, sql);
    if (!stmt.IsValid()) return false;
    stmt.BindText(1, file_path);
    return stmt.Step() == SQLITE_ROW;
}

std::unordered_set<std::string> Database::GetAllReplayPaths() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::unordered_set<std::string> paths;
    if (!m_db) return paths;

    SqliteStmt stmt(m_db, "SELECT file_path FROM replays;");
    if (stmt.IsValid()) {
        while (stmt.Step() == SQLITE_ROW) {
            std::string p = stmt.ColumnText(0);
            if (!p.empty()) paths.insert(p);
        }
    }
    return paths;
}

std::unordered_set<std::string> Database::GetValidReplayPaths() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::unordered_set<std::string> paths;
    if (!m_db) return paths;

    const char* sql = "SELECT file_path FROM replays WHERE result IN ('Victory', 'Defeat', 'Unknown') AND p1_prestige >= 0 AND p1_prestige <= 3 AND p2_prestige >= 0 AND p2_prestige <= 3 AND brutal_plus >= 0 AND brutal_plus <= 6 AND p2_name IS NOT NULL AND p2_name != '' AND (p1_commander != 'Unknown' OR raw_map_name LIKE '[CM]%' OR raw_map_name LIKE '%RTC%');";
    SqliteStmt stmt(m_db, sql);
    if (stmt.IsValid()) {
        while (stmt.Step() == SQLITE_ROW) {
            std::string p = stmt.ColumnText(0);
            if (!p.empty()) paths.insert(p);
        }
    }
    return paths;
}

std::unordered_map<std::string, int64_t> Database::GetValidReplayMtimes() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::unordered_map<std::string, int64_t> mtimes;
    if (!m_db) return mtimes;

    const char* sql = "SELECT file_path, file_mtime FROM replays WHERE result IN ('Victory', 'Defeat', 'Unknown') AND p1_prestige >= 0 AND p1_prestige <= 3 AND p2_prestige >= 0 AND p2_prestige <= 3 AND brutal_plus >= 0 AND brutal_plus <= 6 AND p2_name IS NOT NULL AND p2_name != '' AND (p1_commander != 'Unknown' OR raw_map_name LIKE '[CM]%' OR raw_map_name LIKE '%RTC%');";
    SqliteStmt stmt(m_db, sql);
    if (stmt.IsValid()) {
        while (stmt.Step() == SQLITE_ROW) {
            std::string p = stmt.ColumnText(0);
            int64_t mt = stmt.ColumnInt64(1);
            if (!p.empty()) mtimes[p] = mt;
        }
    }
    return mtimes;
}

std::unordered_map<std::string, int64_t> Database::GetIndexedReplayMtimes() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::unordered_map<std::string, int64_t> mtimes;
    if (!m_db) return mtimes;
    SqliteStmt stmt(m_db, "SELECT file_path, file_mtime FROM replays WHERE result IN ('Victory', 'Defeat');");
    if (stmt.IsValid()) {
        while (stmt.Step() == SQLITE_ROW) mtimes[stmt.ColumnText(0)] = stmt.ColumnInt64(1);
    }
    return mtimes;
}

std::unordered_map<std::string, ReplayFileState> Database::GetReplayFileStates() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::unordered_map<std::string, ReplayFileState> states;
    if (!m_db) return states;
    SqliteStmt stmt(m_db, "SELECT file_path, write_time, file_size, sha256, outcome FROM replay_file_states;");
    if (stmt.IsValid()) {
        while (stmt.Step() == SQLITE_ROW) {
            states[stmt.ColumnText(0)] = {stmt.ColumnInt64(1), stmt.ColumnInt64(2),
                stmt.ColumnText(3), stmt.ColumnText(4)};
        }
    }
    return states;
}

bool Database::SaveReplayFileState(const std::string& path, const ReplayFileState& state) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db || state.sha256.empty()) return false;
    if (state.outcome == "failed") {
        SqliteStmt failed(m_db, "INSERT OR IGNORE INTO replay_failed_hashes (sha256) VALUES (?);");
        if (!failed.IsValid()) return false;
        failed.BindText(1, state.sha256);
        if (failed.Step() != SQLITE_DONE) return false;
    }
    SqliteStmt stmt(m_db, "INSERT OR REPLACE INTO replay_file_states "
        "(file_path, write_time, file_size, sha256, outcome) VALUES (?, ?, ?, ?, ?);");
    if (!stmt.IsValid()) return false;
    stmt.BindText(1, path);
    stmt.BindInt64(2, state.write_time);
    stmt.BindInt64(3, state.file_size);
    stmt.BindText(4, state.sha256);
    stmt.BindText(5, state.outcome);
    return stmt.Step() == SQLITE_DONE;
}

bool Database::HasFailedReplayHash(const std::string& sha256) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db || sha256.empty()) return false;
    SqliteStmt stmt(m_db, "SELECT 1 FROM replay_failed_hashes WHERE sha256 = ? LIMIT 1;");
    if (!stmt.IsValid()) return false;
    stmt.BindText(1, sha256);
    return stmt.Step() == SQLITE_ROW;
}

bool Database::ClearReplayFileStates() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db) return false;
    SqliteTransaction txn(m_db);
    if (!txn.IsActive() || sqlite3_exec(m_db,
        "DELETE FROM replay_file_states; DELETE FROM replay_failed_hashes;", nullptr, nullptr, nullptr) != SQLITE_OK) return false;
    return txn.Commit();
}

std::vector<std::string> Database::GetCorruptedReplayPaths() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<std::string> paths;
    if (!m_db) return paths;

    bool need_diff_fix = true;
    SqliteStmt vstmt(m_db, "SELECT value FROM settings WHERE key = 'difficulty_fix_version' LIMIT 1;");
    if (vstmt.IsValid() && vstmt.Step() == SQLITE_ROW) {
        need_diff_fix = vstmt.ColumnInt(0) < 1;
    }

    std::string sql;
    if (need_diff_fix) {
        sql = "SELECT file_path FROM replays WHERE (p1_prestige < 0 OR p1_prestige > 3 OR p2_prestige < 0 OR p2_prestige > 3 OR brutal_plus < 0 OR brutal_plus > 6) OR (difficulty = 'Casual');";
    } else {
        sql = "SELECT file_path FROM replays WHERE p1_prestige < 0 OR p1_prestige > 3 OR p2_prestige < 0 OR p2_prestige > 3 OR brutal_plus < 0 OR brutal_plus > 6;";
    }

    SqliteStmt stmt(m_db, sql);
    if (stmt.IsValid()) {
        while (stmt.Step() == SQLITE_ROW) {
            std::string p = stmt.ColumnText(0);
            if (!p.empty()) paths.push_back(p);
        }
    }
    return paths;
}

std::unordered_set<std::string> Database::GetDifficultyRepairPaths() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::unordered_set<std::string> paths;
    if (!m_db) return paths;

    SqliteStmt stmt(m_db, "SELECT file_path FROM replays WHERE difficulty = 'Casual';");
    if (stmt.IsValid()) {
        while (stmt.Step() == SQLITE_ROW) {
            std::string p = stmt.ColumnText(0);
            if (!p.empty()) paths.emplace(p);
        }
    }
    return paths;
}

int Database::GetTotalReplayCount() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db) return 0;
    SqliteStmt stmt(m_db, "SELECT COUNT(*) FROM replays;");
    if (stmt.IsValid() && stmt.Step() == SQLITE_ROW) {
        return stmt.ColumnInt(0);
    }
    return 0;
}

std::vector<ReplayRecord> Database::QueryReplays(const std::string& search_query,
                                                  int limit, int offset,
                                                  int& out_total_count) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<ReplayRecord> results;
    out_total_count = 0;
    if (!m_db) return results;

    std::vector<std::string> terms = SplitTerms(search_query);

    std::string where_clause;
    if (!terms.empty()) {
        where_clause = " WHERE ";
        for (size_t i = 0; i < terms.size(); ++i) {
            if (i > 0) where_clause += " AND ";
            where_clause += "(map_name LIKE ? OR raw_map_name LIKE ? OR p1_name LIKE ? OR p2_name LIKE ? OR p1_commander LIKE ? OR p2_commander LIKE ? OR difficulty LIKE ? OR enemy_race LIKE ? OR result LIKE ? OR mutators_json LIKE ?)";
        }
    }

    // 1. Count total matches
    std::string count_sql = "SELECT COUNT(*) FROM replays" + where_clause + ";";
    SqliteStmt count_stmt(m_db, count_sql);
    if (count_stmt.IsValid()) {
        int param_idx = 1;
        for (const auto& term : terms) {
            std::string pat = "%" + term + "%";
            for (int k = 0; k < 10; ++k) {
                count_stmt.BindText(param_idx++, pat);
            }
        }
        if (count_stmt.Step() == SQLITE_ROW) {
            out_total_count = count_stmt.ColumnInt(0);
        }
    }

    // 2. Fetch page results
    std::string fetch_sql = "SELECT file_path, file_mtime, game_date, map_name, raw_map_name, duration_seconds, form_length, result, difficulty, brutal_plus, enemy_race, region, is_weekly, mutators_json, p1_name, p1_handle, p1_commander, p1_prestige, p1_apm, p1_kills, p2_name, p2_handle, p2_commander, p2_prestige, p2_apm, p2_kills, chat_json FROM replays" + where_clause + " ORDER BY file_mtime DESC, game_date DESC LIMIT ? OFFSET ?;";
    SqliteStmt stmt(m_db, fetch_sql);
    if (!stmt.IsValid()) {
        return results;
    }

    int param_idx = 1;
    for (const auto& term : terms) {
        std::string pat = "%" + term + "%";
        for (int k = 0; k < 10; ++k) {
            stmt.BindText(param_idx++, pat);
        }
    }
    stmt.BindInt(param_idx++, limit);
    stmt.BindInt(param_idx++, offset);

    while (stmt.Step() == SQLITE_ROW) {
        ReplayRecord r;
        r.file_path = stmt.ColumnText(0);
        r.file_mtime = stmt.ColumnInt64(1);
        r.game_date = stmt.ColumnText(2);
        r.map_name = stmt.ColumnText(3);
        r.raw_map_name = stmt.ColumnText(4);
        r.duration_seconds = stmt.ColumnInt(5);
        r.form_length = stmt.ColumnText(6);
        r.result = stmt.ColumnText(7);
        r.difficulty = stmt.ColumnText(8);
        r.brutal_plus = stmt.ColumnInt(9);
        r.enemy_race = stmt.ColumnText(10);
        r.region = stmt.ColumnText(11);
        r.is_weekly = (stmt.ColumnInt(12) != 0);

        std::string muts = stmt.ColumnText(13);
        if (!muts.empty()) r.MutatorsFromJson(muts);

        r.p1.name = stmt.ColumnText(14);
        r.p1.handle = stmt.ColumnText(15);
        r.p1.commander = stmt.ColumnText(16);
        r.p1.prestige = stmt.ColumnInt(17);
        r.p1.apm = stmt.ColumnInt(18);
        r.p1.kills = stmt.ColumnInt(19);

        r.p2.name = stmt.ColumnText(20);
        r.p2.handle = stmt.ColumnText(21);
        r.p2.commander = stmt.ColumnText(22);
        r.p2.prestige = stmt.ColumnInt(23);
        r.p2.apm = stmt.ColumnInt(24);
        r.p2.kills = stmt.ColumnInt(25);

        std::string chat = stmt.ColumnText(26);
        if (!chat.empty()) r.ChatFromJson(chat);

        results.push_back(std::move(r));
    }

    return results;
}

bool Database::LoadReplayDetails(ReplayRecord& replay) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db || replay.file_path.empty()) return false;

    SqliteStmt stmt(m_db, "SELECT details_blob, details_format, details_json FROM replays WHERE file_path = ? LIMIT 1;");
    if (!stmt.IsValid()) return false;
    stmt.BindText(1, replay.file_path);
    bool loaded = false;
    if (stmt.Step() == SQLITE_ROW) {
        const void* blob = stmt.ColumnBlob(0);
        const int blob_size = stmt.ColumnBytes(0);
        const int format = stmt.ColumnInt(1);
        if (blob && blob_size > 0 && format == 1) {
            loaded = replay.DetailsFromCompressedBlob(blob, static_cast<size_t>(blob_size));
        }
        if (!loaded) {
            std::string legacy = stmt.ColumnText(2);
            if (!legacy.empty()) {
                loaded = replay.DetailsFromJson(legacy);
            }
        }
    }
    return loaded;
}

bool Database::ClearReplayDetails(const std::string& file_path) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db || file_path.empty()) return false;
    SqliteStmt stmt(m_db, "UPDATE replays SET details_blob = NULL, details_format = 0, details_json = NULL WHERE file_path = ?;");
    if (!stmt.IsValid()) return false;
    stmt.BindText(1, file_path);
    const bool ok = stmt.Step() == SQLITE_DONE && sqlite3_changes(m_db) > 0;
    if (ok) BumpRevision();
    return ok;
}

bool Database::UpdateReplayChat(const std::string& file_path, const std::string& chat_json) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db) return false;
    SqliteStmt stmt(m_db, "UPDATE replays SET chat_json = ? WHERE file_path = ?;");
    if (!stmt.IsValid()) return false;
    stmt.BindText(1, chat_json);
    stmt.BindText(2, file_path);
    bool ok = (stmt.Step() == SQLITE_DONE);
    if (ok) BumpRevision();
    return ok;
}

void Database::AlignPlayerSlotsWithUserAccount() {
    // Deprecated: player slot authenticity is preserved as recorded in replay files.
    return;
}

} // namespace sc2
