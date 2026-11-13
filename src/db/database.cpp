#include "database.h"
#include <sqlite3.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include "../replay/replay_parser.h"

namespace sc2 {

Database& Database::Instance() {
    static Database db;
    return db;
}

Database::~Database() {
    Close();
}

void Database::Close() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

bool Database::Open(const std::string& db_path) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }

    int rc = sqlite3_open_v2(db_path.c_str(), &m_db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                             nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to open SQLite database: " << sqlite3_errmsg(m_db) << std::endl;
        m_db = nullptr;
        return false;
    }

    // Set performance PRAGMAs
    sqlite3_exec(m_db, "PRAGMA journal_mode = WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "PRAGMA synchronous = NORMAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "PRAGMA temp_store = MEMORY;", nullptr, nullptr, nullptr);

    // Create tables
    const char* schema = R"(
        CREATE TABLE IF NOT EXISTS replays (
            file_path TEXT PRIMARY KEY,
            file_mtime INTEGER,
            game_date TEXT,
            map_name TEXT,
            raw_map_name TEXT,
            duration_seconds INTEGER,
            form_length TEXT,
            result TEXT,
            difficulty TEXT,
            brutal_plus INTEGER,
            enemy_race TEXT,
            region TEXT,
            is_weekly INTEGER,
            mutators_json TEXT,
            p1_name TEXT, p1_handle TEXT, p1_commander TEXT, p1_prestige INTEGER, p1_apm INTEGER, p1_kills INTEGER,
            p2_name TEXT, p2_handle TEXT, p2_commander TEXT, p2_prestige INTEGER, p2_apm INTEGER, p2_kills INTEGER,
            chat_json TEXT,
            details_json TEXT
        );

        CREATE TABLE IF NOT EXISTS player_notes (
            player_name TEXT PRIMARY KEY,
            note TEXT,
            updated_at INTEGER
        );

        CREATE TABLE IF NOT EXISTS settings (
            key TEXT PRIMARY KEY,
            value TEXT
        );

        CREATE INDEX IF NOT EXISTS idx_replays_date ON replays(game_date DESC);
        CREATE INDEX IF NOT EXISTS idx_replays_map ON replays(map_name);
        CREATE INDEX IF NOT EXISTS idx_replays_result ON replays(result);
        CREATE INDEX IF NOT EXISTS idx_replays_p1 ON replays(p1_name);
        CREATE INDEX IF NOT EXISTS idx_replays_p2 ON replays(p2_name);
        CREATE INDEX IF NOT EXISTS idx_replays_com1 ON replays(p1_commander);
        CREATE INDEX IF NOT EXISTS idx_replays_com2 ON replays(p2_commander);
    )";

    char* errmsg = nullptr;
    rc = sqlite3_exec(m_db, schema, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to initialize database schema: " << (errmsg ? errmsg : "") << std::endl;
        sqlite3_free(errmsg);
        return false;
    }

    // Auto-migrate schema: add details_json if older DB exists
    sqlite3_exec(m_db, "ALTER TABLE replays ADD COLUMN details_json TEXT;", nullptr, nullptr, nullptr);

    return true;
}

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
            chat_json, details_json
        ) VALUES (
            ?, ?, ?, ?, ?,
            ?, ?, ?, ?, ?,
            ?, ?, ?, ?,
            ?, ?, ?, ?, ?, ?,
            ?, ?, ?, ?, ?, ?,
            ?, ?
        );
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    for (const auto& rec : recs) {
        sqlite3_bind_text(stmt, 1, rec.file_path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, rec.file_mtime);
        sqlite3_bind_text(stmt, 3, rec.game_date.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, rec.map_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, rec.raw_map_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 6, rec.duration_seconds);
        sqlite3_bind_text(stmt, 7, rec.form_length.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 8, rec.result.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 9, rec.difficulty.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 10, (rec.brutal_plus >= 1 && rec.brutal_plus <= 6) ? rec.brutal_plus : 0);
        sqlite3_bind_text(stmt, 11, rec.enemy_race.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 12, rec.region.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 13, rec.is_weekly ? 1 : 0);

        std::string mut_json = rec.MutatorsToJson();
        sqlite3_bind_text(stmt, 14, mut_json.c_str(), -1, SQLITE_TRANSIENT);

        sqlite3_bind_text(stmt, 15, rec.p1.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 16, rec.p1.handle.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 17, rec.p1.commander.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 18, rec.p1.prestige);
        sqlite3_bind_int(stmt, 19, rec.p1.apm);
        sqlite3_bind_int(stmt, 20, rec.p1.kills);

        sqlite3_bind_text(stmt, 21, rec.p2.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 22, rec.p2.handle.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 23, rec.p2.commander.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 24, rec.p2.prestige);
        sqlite3_bind_int(stmt, 25, rec.p2.apm);
        sqlite3_bind_int(stmt, 26, rec.p2.kills);

        std::string chat_json = rec.ChatToJson();
        sqlite3_bind_text(stmt, 27, chat_json.c_str(), -1, SQLITE_TRANSIENT);

        std::string details_json = rec.DetailsToJson();
        sqlite3_bind_text(stmt, 28, details_json.c_str(), -1, SQLITE_TRANSIENT);

        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    }

    sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
    sqlite3_finalize(stmt);
    return true;
}

bool Database::HasReplay(const std::string& file_path) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db) return false;
    const char* sql = "SELECT 1 FROM replays WHERE file_path = ? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, file_path.c_str(), -1, SQLITE_TRANSIENT);
    bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return exists;
}

bool Database::HasValidReplay(const std::string& file_path) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db) return false;
    const char* sql = "SELECT 1 FROM replays WHERE file_path = ? AND p1_prestige >= 0 AND p1_prestige <= 3 AND p2_prestige >= 0 AND p2_prestige <= 3 AND brutal_plus >= 0 AND brutal_plus <= 6 AND p1_commander != 'Unknown' LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, file_path.c_str(), -1, SQLITE_TRANSIENT);
    bool valid = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return valid;
}

std::vector<std::string> Database::GetCorruptedReplayPaths() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<std::string> paths;
    if (!m_db) return paths;

    const char* sql = "SELECT file_path FROM replays WHERE p1_prestige < 0 OR p1_prestige > 3 OR p2_prestige < 0 OR p2_prestige > 3 OR brutal_plus < 0 OR brutal_plus > 6;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* p = sqlite3_column_text(stmt, 0);
            if (p) paths.push_back(reinterpret_cast<const char*>(p));
        }
        sqlite3_finalize(stmt);
    }
    return paths;
}

void Database::SanitizeInvalidPrestiges() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db) return;
    const char* sql = "UPDATE replays SET p1_prestige = 0 WHERE p1_prestige < 0 OR p1_prestige > 3;"
                      "UPDATE replays SET p2_prestige = 0 WHERE p2_prestige < 0 OR p2_prestige > 3;"
                      "UPDATE replays SET brutal_plus = 0 WHERE brutal_plus < 0 OR brutal_plus > 6;";
    sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr);
}

int Database::GetTotalReplayCount() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db) return 0;
    const char* sql = "SELECT COUNT(*) FROM replays;";
    sqlite3_stmt* stmt = nullptr;
    int count = 0;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return count;
}

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
    sqlite3_stmt* count_stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, count_sql.c_str(), -1, &count_stmt, nullptr) == SQLITE_OK) {
        int param_idx = 1;
        for (const auto& term : terms) {
            std::string pat = "%" + term + "%";
            for (int k = 0; k < 10; ++k) {
                sqlite3_bind_text(count_stmt, param_idx++, pat.c_str(), -1, SQLITE_TRANSIENT);
            }
        }
        if (sqlite3_step(count_stmt) == SQLITE_ROW) {
            out_total_count = sqlite3_column_int(count_stmt, 0);
        }
        sqlite3_finalize(count_stmt);
    }

    // 2. Fetch page results
    std::string fetch_sql = "SELECT file_path, file_mtime, game_date, map_name, raw_map_name, duration_seconds, form_length, result, difficulty, brutal_plus, enemy_race, region, is_weekly, mutators_json, p1_name, p1_handle, p1_commander, p1_prestige, p1_apm, p1_kills, p2_name, p2_handle, p2_commander, p2_prestige, p2_apm, p2_kills, chat_json, details_json FROM replays" + where_clause + " ORDER BY game_date DESC LIMIT ? OFFSET ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, fetch_sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }

    int param_idx = 1;
    for (const auto& term : terms) {
        std::string pat = "%" + term + "%";
        for (int k = 0; k < 10; ++k) {
            sqlite3_bind_text(stmt, param_idx++, pat.c_str(), -1, SQLITE_TRANSIENT);
        }
    }
    sqlite3_bind_int(stmt, param_idx++, limit);
    sqlite3_bind_int(stmt, param_idx++, offset);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ReplayRecord r;
        r.file_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        r.file_mtime = sqlite3_column_int64(stmt, 1);
        r.game_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        r.map_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        r.raw_map_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        r.duration_seconds = sqlite3_column_int(stmt, 5);
        r.form_length = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        r.result = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        r.difficulty = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        r.brutal_plus = sqlite3_column_int(stmt, 9);
        const char* enemy = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
        r.enemy_race = enemy ? enemy : "";
        const char* reg = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
        r.region = reg ? reg : "";
        r.is_weekly = (sqlite3_column_int(stmt, 12) != 0);

        const char* muts = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 13));
        if (muts) r.MutatorsFromJson(muts);

        r.p1.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 14));
        r.p1.handle = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 15));
        r.p1.commander = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 16));
        r.p1.prestige = sqlite3_column_int(stmt, 17);
        r.p1.apm = sqlite3_column_int(stmt, 18);
        r.p1.kills = sqlite3_column_int(stmt, 19);

        const char* p2_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 20));
        r.p2.name = p2_name ? p2_name : "";
        const char* p2_h = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 21));
        r.p2.handle = p2_h ? p2_h : "";
        const char* p2_c = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 22));
        r.p2.commander = p2_c ? p2_c : "";
        r.p2.prestige = sqlite3_column_int(stmt, 23);
        r.p2.apm = sqlite3_column_int(stmt, 24);
        r.p2.kills = sqlite3_column_int(stmt, 25);

        const char* chat = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 26));
        if (chat) r.ChatFromJson(chat);

        const char* details = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 27));
        if (details) r.DetailsFromJson(details);

        results.push_back(r);
    }

    sqlite3_finalize(stmt);
    return results;
}

std::vector<PlayerRecord> Database::QueryPlayers(const std::string& search_filter,
                                                  const std::string& sort_column,
                                                  bool sort_ascending,
                                                  int limit, int offset,
                                                  int& out_total_count) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<PlayerRecord> players;
    out_total_count = 0;
    if (!m_db) return players;

    std::string filter_clause;
    if (!search_filter.empty()) {
        filter_clause = " WHERE p.p2_name LIKE ? OR n.note LIKE ? ";
    }

    std::string count_sql = "SELECT COUNT(DISTINCT p2_name) FROM replays WHERE p2_name IS NOT NULL AND p2_name != '' AND p2_name NOT LIKE '%埃蒙%';" ;
    sqlite3_stmt* count_stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, count_sql.c_str(), -1, &count_stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(count_stmt) == SQLITE_ROW) {
            out_total_count = sqlite3_column_int(count_stmt, 0);
        }
        sqlite3_finalize(count_stmt);
    }

    std::string order_sql = "wins DESC";
    if (sort_column == "wins") order_sql = sort_ascending ? "wins ASC" : "wins DESC";
    else if (sort_column == "losses") order_sql = sort_ascending ? "losses ASC" : "losses DESC";
    else if (sort_column == "winrate") order_sql = sort_ascending ? "winrate ASC" : "winrate DESC";
    else if (sort_column == "apm") order_sql = sort_ascending ? "median_apm ASC" : "median_apm DESC";
    else if (sort_column == "name") order_sql = sort_ascending ? "player_name ASC" : "player_name DESC";

    std::string query_sql = R"(
        SELECT p.p2_name as player_name,
               COUNT(*) as total_games,
               SUM(CASE WHEN p.result = 'Victory' THEN 1 ELSE 0 END) as wins,
               SUM(CASE WHEN p.result = 'Defeat' THEN 1 ELSE 0 END) as losses,
               ROUND(100.0 * SUM(CASE WHEN p.result = 'Victory' THEN 1 ELSE 0 END) / COUNT(*), 1) as winrate,
               ROUND(AVG(p.p2_apm)) as median_apm,
               MAX(p.file_mtime) as last_played,
               COALESCE(n.note, '') as note
        FROM replays p
        LEFT JOIN player_notes n ON p.p2_name = n.player_name
        WHERE p.p2_name IS NOT NULL AND p.p2_name != '' AND p.p2_name NOT LIKE '%埃蒙%'
    )";
    if (!search_filter.empty()) {
        query_sql += " AND (p.p2_name LIKE ? OR n.note LIKE ?)";
    }
    query_sql += " GROUP BY p.p2_name ORDER BY " + order_sql + " LIMIT ? OFFSET ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, query_sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return players;
    }

    int pidx = 1;
    if (!search_filter.empty()) {
        std::string pat = "%" + search_filter + "%";
        sqlite3_bind_text(stmt, pidx++, pat.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, pidx++, pat.c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_int(stmt, pidx++, limit);
    sqlite3_bind_int(stmt, pidx++, offset);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PlayerRecord pr;
        pr.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        pr.total_games = sqlite3_column_int(stmt, 1);
        pr.wins = sqlite3_column_int(stmt, 2);
        pr.losses = sqlite3_column_int(stmt, 3);
        pr.winrate = sqlite3_column_double(stmt, 4);
        pr.median_apm = sqlite3_column_int(stmt, 5);
        pr.last_played_mtime = sqlite3_column_int64(stmt, 6);
        const char* note_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        pr.note = note_str ? note_str : "";
        players.push_back(pr);
    }

    sqlite3_finalize(stmt);
    return players;
}

bool Database::SetPlayerNote(const std::string& player_name, const std::string& note) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db) return false;
    const char* sql = "INSERT OR REPLACE INTO player_notes (player_name, note, updated_at) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, player_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, note.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, time(nullptr));

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

std::string Database::GetPlayerNote(const std::string& player_name) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db) return "";
    const char* sql = "SELECT note FROM player_notes WHERE player_name = ? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    std::string res;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, player_name.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* n = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (n) res = n;
        }
        sqlite3_finalize(stmt);
    }
    return res;
}

bool Database::UpdateReplayChat(const std::string& file_path, const std::string& chat_json) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db) return false;
    const char* sql = "UPDATE replays SET chat_json = ? WHERE file_path = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, chat_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, file_path.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::SetSetting(const std::string& key, const std::string& value) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db) return false;
    const char* sql = "INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

std::string Database::GetSetting(const std::string& key, const std::string& default_val) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db) return default_val;
    const char* sql = "SELECT value FROM settings WHERE key = ? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    std::string res = default_val;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (val) res = val;
        }
        sqlite3_finalize(stmt);
    }
    return res;
}

StatsSummary Database::CalculateStats(const StatsFilter& filter) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    StatsSummary sum;
    if (!m_db) return sum;

    // Build dynamic WHERE clause based on StatsFilter
    std::string where_clause = " WHERE 1=1";

    // 1. Difficulty filter (Exclusion-based matching Python MassReplayAnalysis)
    if (!filter.casual && !filter.normal && !filter.hard && !filter.brutal && !filter.brutal_plus) {
        where_clause += " AND 1=0";
    } else {
        if (!filter.casual) where_clause += " AND NOT (difficulty LIKE '%Casual%' AND (brutal_plus IS NULL OR brutal_plus = 0))";
        if (!filter.normal) where_clause += " AND NOT (difficulty LIKE '%Normal%' AND (brutal_plus IS NULL OR brutal_plus = 0))";
        if (!filter.hard) where_clause += " AND NOT (difficulty LIKE '%Hard%' AND (brutal_plus IS NULL OR brutal_plus = 0))";
        if (!filter.brutal) where_clause += " AND NOT (difficulty LIKE '%Brutal%' AND (brutal_plus IS NULL OR brutal_plus = 0))";
        if (!filter.brutal_plus) where_clause += " AND (brutal_plus IS NULL OR brutal_plus = 0)";
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

    // 5. Date range and duration filters
    auto escape_sql = [](const std::string& input) {
        std::string out = input;
        size_t pos = 0;
        while ((pos = out.find('\'', pos)) != std::string::npos) {
            out.replace(pos, 1, "''");
            pos += 2;
        }
        return out;
    };

    if (!filter.date_from.empty()) {
        where_clause += " AND game_date >= '" + escape_sql(filter.date_from) + "'";
    }
    if (!filter.date_to.empty()) {
        std::string to = filter.date_to;
        if (to.length() == 10) to += " 23:59:59";
        where_clause += " AND game_date <= '" + escape_sql(to) + "'";
    }

    if (filter.min_length_minutes > 0) {
        where_clause += " AND duration_seconds >= " + std::to_string(filter.min_length_minutes * 60);
    }
    if (filter.max_length_minutes > 0 && filter.max_length_minutes < 1000) {
        where_clause += " AND duration_seconds <= " + std::to_string(filter.max_length_minutes * 60);
    }
    if (!filter.ally_name_filter.empty()) {
        where_clause += " AND p2_name LIKE '%" + escape_sql(filter.ally_name_filter) + "%'";
    }

    // Overall win / loss count
    std::string sql_total = "SELECT COUNT(*), "
                            "SUM(CASE WHEN result='Victory' THEN 1 ELSE 0 END), "
                            "SUM(CASE WHEN result='Defeat' THEN 1 ELSE 0 END) "
                            "FROM replays" + where_clause + ";";
    sqlite3_stmt* s_tot = nullptr;
    if (sqlite3_prepare_v2(m_db, sql_total.c_str(), -1, &s_tot, nullptr) == SQLITE_OK) {
        if (sqlite3_step(s_tot) == SQLITE_ROW) {
            sum.total_games = sqlite3_column_int(s_tot, 0);
            sum.total_wins = sqlite3_column_int(s_tot, 1);
            sum.total_losses = sqlite3_column_int(s_tot, 2);
            if (sum.total_games > 0) {
                sum.overall_winrate = (100.0 * sum.total_wins) / sum.total_games;
            }
        }
        sqlite3_finalize(s_tot);
    }

    // Maps breakdown
    std::string sql_maps = "SELECT map_name, "
                           "MIN(duration_seconds) as fastest, "
                           "AVG(duration_seconds) as avg_dur, "
                           "SUM(CASE WHEN result='Victory' THEN 1 ELSE 0 END) as wins, "
                           "SUM(CASE WHEN result='Defeat' THEN 1 ELSE 0 END) as losses, "
                           "COUNT(*) as total "
                           "FROM replays" + where_clause + " "
                           "GROUP BY map_name "
                           "ORDER BY total DESC;";
    sqlite3_stmt* s_maps = nullptr;
    if (sqlite3_prepare_v2(m_db, sql_maps.c_str(), -1, &s_maps, nullptr) == SQLITE_OK) {
        while (sqlite3_step(s_maps) == SQLITE_ROW) {
            MapStat ms;
            ms.map_name = reinterpret_cast<const char*>(sqlite3_column_text(s_maps, 0));
            ms.fastest_seconds = sqlite3_column_int(s_maps, 1);
            ms.fastest_str = ReplayParser::FormatDuration(ms.fastest_seconds);
            ms.avg_seconds = sqlite3_column_int(s_maps, 2);
            ms.avg_str = ReplayParser::FormatDuration(ms.avg_seconds);
            ms.wins = sqlite3_column_int(s_maps, 3);
            ms.losses = sqlite3_column_int(s_maps, 4);
            int tot = sqlite3_column_int(s_maps, 5);
            ms.winrate = tot > 0 ? (100.0 * ms.wins / tot) : 0.0;
            ms.frequency = sum.total_games > 0 ? (100.0 * tot / sum.total_games) : 0.0;
            sum.maps.push_back(ms);
        }
        sqlite3_finalize(s_maps);
    }

    // My commanders breakdown (P1)
    std::string sql_com = "SELECT p1_commander, "
                          "COUNT(*) as total, "
                          "SUM(CASE WHEN result='Victory' THEN 1 ELSE 0 END) as wins, "
                          "SUM(CASE WHEN result='Defeat' THEN 1 ELSE 0 END) as losses, "
                          "AVG(p1_apm) as avg_apm "
                          "FROM replays" + where_clause + " AND p1_commander != '' AND p1_commander != 'Unknown' "
                          "GROUP BY p1_commander "
                          "ORDER BY total DESC;";
    sqlite3_stmt* s_com = nullptr;
    if (sqlite3_prepare_v2(m_db, sql_com.c_str(), -1, &s_com, nullptr) == SQLITE_OK) {
        while (sqlite3_step(s_com) == SQLITE_ROW) {
            CommanderStat cs;
            cs.commander = reinterpret_cast<const char*>(sqlite3_column_text(s_com, 0));
            cs.total_games = sqlite3_column_int(s_com, 1);
            cs.wins = sqlite3_column_int(s_com, 2);
            cs.losses = sqlite3_column_int(s_com, 3);
            cs.winrate = cs.total_games > 0 ? (100.0 * cs.wins / cs.total_games) : 0.0;
            cs.frequency = sum.total_games > 0 ? (100.0 * cs.total_games / sum.total_games) : 0.0;
            cs.avg_apm = sqlite3_column_int(s_com, 4);
            sum.my_commanders.push_back(cs);
        }
        sqlite3_finalize(s_com);
    }

    // Allied commanders breakdown (P2)
    std::string sql_ally_com = "SELECT p2_commander, "
                               "COUNT(*) as total, "
                               "SUM(CASE WHEN result='Victory' THEN 1 ELSE 0 END) as wins, "
                               "SUM(CASE WHEN result='Defeat' THEN 1 ELSE 0 END) as losses, "
                               "AVG(p2_apm) as avg_apm "
                               "FROM replays" + where_clause + " AND p2_commander != '' AND p2_commander != 'Unknown' "
                               "GROUP BY p2_commander "
                               "ORDER BY total DESC;";
    sqlite3_stmt* s_acom = nullptr;
    if (sqlite3_prepare_v2(m_db, sql_ally_com.c_str(), -1, &s_acom, nullptr) == SQLITE_OK) {
        while (sqlite3_step(s_acom) == SQLITE_ROW) {
            CommanderStat cs;
            cs.commander = reinterpret_cast<const char*>(sqlite3_column_text(s_acom, 0));
            cs.total_games = sqlite3_column_int(s_acom, 1);
            cs.wins = sqlite3_column_int(s_acom, 2);
            cs.losses = sqlite3_column_int(s_acom, 3);
            cs.winrate = cs.total_games > 0 ? (100.0 * cs.wins / cs.total_games) : 0.0;
            cs.frequency = sum.total_games > 0 ? (100.0 * cs.total_games / sum.total_games) : 0.0;
            cs.avg_apm = sqlite3_column_int(s_acom, 4);
            sum.ally_commanders.push_back(cs);
        }
        sqlite3_finalize(s_acom);
    }

    // Difficulty breakdown (matches Python SCO StatsTab)
    const std::vector<std::string> kDifficulties = {
        "Casual", "Normal", "Hard", "Brutal",
        "Brutal+1", "Brutal+2", "Brutal+3", "Brutal+4", "Brutal+5", "Brutal+6"
    };

    std::string sql_diff = "SELECT "
                           "CASE WHEN (brutal_plus >= 1 AND brutal_plus <= 6) THEN 'Brutal+' || brutal_plus ELSE difficulty END as d_label, "
                           "SUM(CASE WHEN result='Victory' THEN 1 ELSE 0 END) as wins, "
                           "SUM(CASE WHEN result='Defeat' THEN 1 ELSE 0 END) as losses, "
                           "COUNT(*) as total "
                           "FROM replays" + where_clause + " "
                           "AND difficulty NOT LIKE '%/%' "
                           "GROUP BY d_label;";
    sqlite3_stmt* s_diff = nullptr;
    std::unordered_map<std::string, DifficultyStat> diff_map;
    if (sqlite3_prepare_v2(m_db, sql_diff.c_str(), -1, &s_diff, nullptr) == SQLITE_OK) {
        while (sqlite3_step(s_diff) == SQLITE_ROW) {
            std::string label = reinterpret_cast<const char*>(sqlite3_column_text(s_diff, 0));
            DifficultyStat ds;
            ds.difficulty = label;
            ds.wins = sqlite3_column_int(s_diff, 1);
            ds.losses = sqlite3_column_int(s_diff, 2);
            ds.total = sqlite3_column_int(s_diff, 3);
            ds.winrate = ds.total > 0 ? (100.0 * ds.wins / ds.total) : 0.0;
            diff_map[label] = ds;
        }
        sqlite3_finalize(s_diff);
    }

    for (const auto& d : kDifficulties) {
        auto it = diff_map.find(d);
        if (it != diff_map.end()) {
            sum.difficulties.push_back(it->second);
        }
    }

    // Region breakdown (matches Python SCO StatsTab)
    const std::vector<std::string> kRegions = {"NA", "EU", "KR", "CN"};
    std::string sql_reg = "SELECT region, "
                          "SUM(CASE WHEN result='Victory' THEN 1 ELSE 0 END) as wins, "
                          "SUM(CASE WHEN result='Defeat' THEN 1 ELSE 0 END) as losses, "
                          "COUNT(*) as total "
                          "FROM replays" + where_clause + " "
                          "AND region IN ('NA', 'EU', 'KR', 'CN') "
                          "GROUP BY region;";
    sqlite3_stmt* s_reg = nullptr;
    std::unordered_map<std::string, RegionStat> reg_map;
    if (sqlite3_prepare_v2(m_db, sql_reg.c_str(), -1, &s_reg, nullptr) == SQLITE_OK) {
        while (sqlite3_step(s_reg) == SQLITE_ROW) {
            std::string reg = reinterpret_cast<const char*>(sqlite3_column_text(s_reg, 0));
            RegionStat rs;
            rs.region = reg;
            rs.wins = sqlite3_column_int(s_reg, 1);
            rs.losses = sqlite3_column_int(s_reg, 2);
            rs.total = sqlite3_column_int(s_reg, 3);
            rs.winrate = rs.total > 0 ? (100.0 * rs.wins / rs.total) : 0.0;
            reg_map[reg] = rs;
        }
        sqlite3_finalize(s_reg);
    }

    for (const auto& r : kRegions) {
        auto it = reg_map.find(r);
        if (it != reg_map.end()) {
            sum.regions.push_back(it->second);
        }
    }

    return sum;
}

Database::WeeklyRecord Database::QueryWeeklyRecord(const std::string& map_name, const std::vector<std::string>& mutators) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    WeeklyRecord wr;
    if (!m_db) return wr;

    std::string sql = "SELECT result, difficulty, file_path FROM replays WHERE map_name = ? AND is_weekly = 1";
    for (size_t i = 0; i < mutators.size(); ++i) {
        sql += " AND mutators_json LIKE ?";
    }
    sql += ";";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return wr;
    }

    int pidx = 1;
    sqlite3_bind_text(stmt, pidx++, map_name.c_str(), -1, SQLITE_TRANSIENT);
    std::vector<std::string> patterns;
    patterns.reserve(mutators.size());
    for (const auto& m : mutators) {
        patterns.push_back("%" + m + "%");
    }
    for (const auto& pat : patterns) {
        sqlite3_bind_text(stmt, pidx++, pat.c_str(), -1, SQLITE_TRANSIENT);
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string res = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        std::string diff = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        std::string fp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

        if (res == "Victory") {
            wr.wins++;
            wr.highest_diff = diff;
        } else {
            wr.losses++;
        }
        wr.replay_files.push_back(fp);
    }

    sqlite3_finalize(stmt);
    int tot = wr.wins + wr.losses;
    if (tot > 0) {
        wr.winrate = (100.0 * wr.wins) / tot;
    }
    return wr;
}

bool Database::DumpAllToJson(const std::string& output_path) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db) return false;
    int dummy = 0;
    auto all_replays = QueryReplays("", 100000, 0, dummy);

    nlohmann::json root = nlohmann::json::array();
    for (const auto& r : all_replays) {
        nlohmann::json item;
        item["file"] = r.file_path;
        item["date"] = r.game_date;
        item["map"] = r.map_name;
        item["duration"] = r.duration_seconds;
        item["result"] = r.result;
        item["difficulty"] = r.difficulty;
        item["p1_name"] = r.p1.name;
        item["p1_commander"] = r.p1.commander;
        item["p1_prestige"] = r.p1.prestige;
        item["p1_apm"] = r.p1.apm;
        item["p2_name"] = r.p2.name;
        item["p2_commander"] = r.p2.commander;
        item["p2_prestige"] = r.p2.prestige;
        item["p2_apm"] = r.p2.apm;
        root.push_back(item);
    }

    std::ofstream ofs(output_path);
    if (!ofs.is_open()) return false;
    ofs << root.dump(2);
    return true;
}

} // namespace sc2
