#include "database.h"
#include <sqlite3.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <unordered_set>
#include "../replay/replay_parser.h"
#include "../replay/mpq_reader.h"
#include "../replay/s2_decoder.h"
#include "../data/weekly_mutations.h"
#include "../app_state.h"
#include <nlohmann/json.hpp>

namespace sc2 {

namespace {

static inline std::string safe_text(sqlite3_stmt* s, int col) {
    const unsigned char* p = sqlite3_column_text(s, col);
    return p ? reinterpret_cast<const char*>(p) : "";
}

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

static void SqliteIsUserHandle(sqlite3_context* context, int argc, sqlite3_value** argv) {
    if (argc < 1 || sqlite3_value_type(argv[0]) == SQLITE_NULL) {
        sqlite3_result_int(context, 0);
        return;
    }
    const unsigned char* text = sqlite3_value_text(argv[0]);
    if (!text || text[0] == '\0') {
        sqlite3_result_int(context, 0);
        return;
    }
    std::string handle(reinterpret_cast<const char*>(text));
    bool is_user = AppState::Instance().HasUserHandle(handle);
    sqlite3_result_int(context, is_user ? 1 : 0);
}

} // namespace

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
        const char* message = m_db ? sqlite3_errmsg(m_db) : "unknown SQLite open error";
        std::cerr << "Failed to open SQLite database: " << message << std::endl;
        if (m_db) sqlite3_close(m_db);
        m_db = nullptr;
        return false;
    }

    // Register custom SQLite scalar functions
    if (sqlite3_create_function(m_db, "is_user_handle", 1, SQLITE_UTF8, nullptr, SqliteIsUserHandle, nullptr, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to register SQLite user-handle function: " << sqlite3_errmsg(m_db) << std::endl;
        sqlite3_close(m_db);
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
            details_json TEXT,
            details_blob BLOB,
            details_format INTEGER DEFAULT 1
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
        CREATE INDEX IF NOT EXISTS idx_replays_mtime ON replays(file_mtime DESC);
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
        sqlite3_close(m_db);
        m_db = nullptr;
        return false;
    }

    auto has_column = [&](const char* column_name) {
        sqlite3_stmt* stmt = nullptr;
        bool found = false;
        if (sqlite3_prepare_v2(m_db, "PRAGMA table_info(replays);", -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const unsigned char* name = sqlite3_column_text(stmt, 1);
                if (name && std::strcmp(reinterpret_cast<const char*>(name), column_name) == 0) {
                    found = true;
                    break;
                }
            }
        }
        sqlite3_finalize(stmt);
        return found;
    };
    auto ensure_column = [&](const char* column_name, const char* definition) {
        if (has_column(column_name)) return true;
        std::string sql = "ALTER TABLE replays ADD COLUMN ";
        sql += definition;
        sql += ";";
        char* alter_error = nullptr;
        const int alter_rc = sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &alter_error);
        if (alter_rc != SQLITE_OK) {
            std::cerr << "Failed to migrate database column " << column_name << ": "
                      << (alter_error ? alter_error : sqlite3_errmsg(m_db)) << std::endl;
            sqlite3_free(alter_error);
            return false;
        }
        return true;
    };
    if (!ensure_column("details_json", "details_json TEXT") ||
        !ensure_column("details_blob", "details_blob BLOB") ||
        !ensure_column("details_format", "details_format INTEGER DEFAULT 1")) {
        sqlite3_close(m_db);
        m_db = nullptr;
        return false;
    }

    if (!MigrateDetailsStorage()) {
        std::cerr << "Failed to migrate replay details storage." << std::endl;
        sqlite3_close(m_db);
        m_db = nullptr;
        return false;
    }

    RepairWeeklyMutationsIfNeeded();

    // Sanitize corrupted historical commander names
    sqlite3_exec(m_db,
        "UPDATE replays SET p1_commander = 'Unknown' WHERE p1_commander NOT IN ('Raynor', 'Kerrigan', 'Artanis', 'Swann', 'Zagara', 'Vorazun', 'Karax', 'Abathur', 'Alarak', 'Nova', 'Stukov', 'Fenix', 'Dehaka', 'HanHorner', 'Horner', 'Tychus', 'Zeratul', 'Stetmann', 'Mengsk', 'Unknown');",
        nullptr, nullptr, nullptr);
    sqlite3_exec(m_db,
        "UPDATE replays SET p2_commander = 'Unknown' WHERE p2_commander NOT IN ('Raynor', 'Kerrigan', 'Artanis', 'Swann', 'Zagara', 'Vorazun', 'Karax', 'Abathur', 'Alarak', 'Nova', 'Stukov', 'Fenix', 'Dehaka', 'HanHorner', 'Horner', 'Tychus', 'Zeratul', 'Stetmann', 'Mengsk', 'Unknown');",
        nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "UPDATE replays SET p1_commander = 'HanHorner' WHERE p1_commander = 'Horner';", nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "UPDATE replays SET p2_commander = 'HanHorner' WHERE p2_commander = 'Horner';", nullptr, nullptr, nullptr);

    BumpRevision();
    return true;
}

bool Database::MigrateDetailsStorage() {
    if (!m_db) return false;

    bool already_migrated = false;
    sqlite3_stmt* version_stmt = nullptr;
    if (sqlite3_prepare_v2(m_db,
            "SELECT value FROM settings WHERE key = 'details_storage_version' LIMIT 1;",
            -1, &version_stmt, nullptr) == SQLITE_OK) {
        already_migrated = sqlite3_step(version_stmt) == SQLITE_ROW &&
            sqlite3_column_int(version_stmt, 0) >= 1;
    }
    sqlite3_finalize(version_stmt);
    if (already_migrated) return true;

    sqlite3_stmt* select_stmt = nullptr;
    const char* select_sql =
        "SELECT file_path, details_json FROM replays "
        "WHERE (details_blob IS NULL OR length(details_blob) = 0) "
        "AND details_json IS NOT NULL AND details_json != '';";
    if (sqlite3_prepare_v2(m_db, select_sql, -1, &select_stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    struct LegacyDetails { std::string path; std::string json; };
    std::vector<LegacyDetails> rows;
    while (sqlite3_step(select_stmt) == SQLITE_ROW) {
        const unsigned char* path = sqlite3_column_text(select_stmt, 0);
        const unsigned char* json = sqlite3_column_text(select_stmt, 1);
        if (path && json) rows.push_back({reinterpret_cast<const char*>(path), reinterpret_cast<const char*>(json)});
    }
    sqlite3_finalize(select_stmt);

    bool ok = sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_stmt* update_stmt = nullptr;
    if (ok && sqlite3_prepare_v2(m_db,
            "UPDATE replays SET details_blob = ?, details_format = 1 WHERE file_path = ?;",
            -1, &update_stmt, nullptr) != SQLITE_OK) ok = false;

    for (const auto& row : rows) {
        if (!ok) break;
        try {
            nlohmann::json::parse(row.json);
        } catch (...) {
            // Keep malformed legacy payloads available for the fallback parser.
            continue;
        }
        ReplayRecord details;
        const auto blob = details.DetailsFromJson(row.json)
            ? details.DetailsToCompressedBlob()
            : ReplayRecord::CompressJson(row.json);
        if (blob.empty()) continue;
        sqlite3_bind_blob(update_stmt, 1, blob.data(), static_cast<int>(blob.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(update_stmt, 2, row.path.c_str(), -1, SQLITE_TRANSIENT);
        ok = sqlite3_step(update_stmt) == SQLITE_DONE;
        sqlite3_reset(update_stmt);
    }
    if (update_stmt) sqlite3_finalize(update_stmt);
    if (ok) {
        // Drop the large legacy copy only after every row has been converted.
        ok = sqlite3_exec(m_db, "UPDATE replays SET details_json = NULL WHERE details_blob IS NOT NULL;",
                          nullptr, nullptr, nullptr) == SQLITE_OK;
    }
    if (ok) ok = sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK;
    else sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
    if (!ok) return false;

    sqlite3_exec(m_db,
        "INSERT INTO settings(key, value) VALUES('details_storage_version', '1') "
        "ON CONFLICT(key) DO UPDATE SET value = '1';",
        nullptr, nullptr, nullptr);

    // Reclaim pages occupied by the old TEXT payload. This runs once per database.
    if (!rows.empty()) sqlite3_exec(m_db, "VACUUM;", nullptr, nullptr, nullptr);
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

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "[DB ERROR] BatchSaveReplays prepare failed: " << sqlite3_errmsg(m_db) << "\n";
        return false;
    }

    if (sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        std::cerr << "[DB ERROR] BatchSaveReplays BEGIN TRANSACTION failed: " << sqlite3_errmsg(m_db) << "\n";
        sqlite3_finalize(stmt);
        return false;
    }

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

        // New rows use compacted, compressed details. Keep the legacy text column
        // NULL so list queries and future migrations never retain a second copy.
        sqlite3_bind_null(stmt, 28);
        std::vector<uint8_t> details_blob = rec.DetailsToCompressedBlob();
        if (details_blob.empty()) {
            sqlite3_bind_null(stmt, 29);
        } else {
            sqlite3_bind_blob(stmt, 29, details_blob.data(), static_cast<int>(details_blob.size()), SQLITE_TRANSIENT);
        }
        sqlite3_bind_int(stmt, 30, details_blob.empty() ? 0 : 1);

        int step_res = sqlite3_step(stmt);
        if (step_res != SQLITE_DONE) {
            std::cerr << "[DB ERROR] BatchSaveReplays step failed: " << sqlite3_errmsg(m_db) << "\n";
            sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
            sqlite3_finalize(stmt);
            return false;
        }
        sqlite3_reset(stmt);
    }

    int commit_res = sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
    if (commit_res != SQLITE_OK) {
        std::cerr << "[DB ERROR] BatchSaveReplays COMMIT failed: " << sqlite3_errmsg(m_db) << "\n";
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        sqlite3_finalize(stmt);
        return false;
    }

    sqlite3_finalize(stmt);
    BumpRevision();
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
    const char* sql = "SELECT 1 FROM replays WHERE file_path = ? AND result IN ('Victory', 'Defeat', 'Unknown') AND p1_prestige >= 0 AND p1_prestige <= 3 AND p2_prestige >= 0 AND p2_prestige <= 3 AND brutal_plus >= 0 AND brutal_plus <= 6 AND p2_name IS NOT NULL AND p2_name != '' AND (p1_commander != 'Unknown' OR raw_map_name LIKE '[CM]%' OR raw_map_name LIKE '%RTC%') LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, file_path.c_str(), -1, SQLITE_TRANSIENT);
    bool valid = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return valid;
}

std::unordered_set<std::string> Database::GetAllReplayPaths() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::unordered_set<std::string> paths;
    if (!m_db) return paths;

    const char* sql = "SELECT file_path FROM replays;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* p = sqlite3_column_text(stmt, 0);
            if (p) paths.insert(reinterpret_cast<const char*>(p));
        }
        sqlite3_finalize(stmt);
    }
    return paths;
}

std::unordered_set<std::string> Database::GetValidReplayPaths() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::unordered_set<std::string> paths;
    if (!m_db) return paths;

    const char* sql = "SELECT file_path FROM replays WHERE result IN ('Victory', 'Defeat', 'Unknown') AND p1_prestige >= 0 AND p1_prestige <= 3 AND p2_prestige >= 0 AND p2_prestige <= 3 AND brutal_plus >= 0 AND brutal_plus <= 6 AND p2_name IS NOT NULL AND p2_name != '' AND (p1_commander != 'Unknown' OR raw_map_name LIKE '[CM]%' OR raw_map_name LIKE '%RTC%');";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* p = sqlite3_column_text(stmt, 0);
            if (p) paths.insert(reinterpret_cast<const char*>(p));
        }
        sqlite3_finalize(stmt);
    }
    return paths;
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
    BumpRevision();
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
    std::string fetch_sql = "SELECT file_path, file_mtime, game_date, map_name, raw_map_name, duration_seconds, form_length, result, difficulty, brutal_plus, enemy_race, region, is_weekly, mutators_json, p1_name, p1_handle, p1_commander, p1_prestige, p1_apm, p1_kills, p2_name, p2_handle, p2_commander, p2_prestige, p2_apm, p2_kills, chat_json FROM replays" + where_clause + " ORDER BY file_mtime DESC, game_date DESC LIMIT ? OFFSET ?;";
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

    auto safe_text = [](sqlite3_stmt* s, int col) -> std::string {
        const unsigned char* p = sqlite3_column_text(s, col);
        return p ? reinterpret_cast<const char*>(p) : "";
    };

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ReplayRecord r;
        r.file_path = safe_text(stmt, 0);
        r.file_mtime = sqlite3_column_int64(stmt, 1);
        r.game_date = safe_text(stmt, 2);
        r.map_name = safe_text(stmt, 3);
        r.raw_map_name = safe_text(stmt, 4);
        r.duration_seconds = sqlite3_column_int(stmt, 5);
        r.form_length = safe_text(stmt, 6);
        r.result = safe_text(stmt, 7);
        r.difficulty = safe_text(stmt, 8);
        r.brutal_plus = sqlite3_column_int(stmt, 9);
        r.enemy_race = safe_text(stmt, 10);
        r.region = safe_text(stmt, 11);
        r.is_weekly = (sqlite3_column_int(stmt, 12) != 0);

        const char* muts = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 13));
        if (muts) r.MutatorsFromJson(muts);

        r.p1.name = safe_text(stmt, 14);
        r.p1.handle = safe_text(stmt, 15);
        r.p1.commander = safe_text(stmt, 16);
        r.p1.prestige = sqlite3_column_int(stmt, 17);
        r.p1.apm = sqlite3_column_int(stmt, 18);
        r.p1.kills = sqlite3_column_int(stmt, 19);

        r.p2.name = safe_text(stmt, 20);
        r.p2.handle = safe_text(stmt, 21);
        r.p2.commander = safe_text(stmt, 22);
        r.p2.prestige = sqlite3_column_int(stmt, 23);
        r.p2.apm = sqlite3_column_int(stmt, 24);
        r.p2.kills = sqlite3_column_int(stmt, 25);

        const char* chat = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 26));
        if (chat) r.ChatFromJson(chat);

        results.push_back(r);
    }

    sqlite3_finalize(stmt);
    return results;
}

bool Database::LoadReplayDetails(ReplayRecord& replay) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db || replay.file_path.empty()) return false;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT details_blob, details_format, details_json FROM replays WHERE file_path = ? LIMIT 1;";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, replay.file_path.c_str(), -1, SQLITE_TRANSIENT);
    bool loaded = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const void* blob = sqlite3_column_blob(stmt, 0);
        const int blob_size = sqlite3_column_bytes(stmt, 0);
        const int format = sqlite3_column_int(stmt, 1);
        if (blob && blob_size > 0 && format == 1) {
            loaded = replay.DetailsFromCompressedBlob(blob, static_cast<size_t>(blob_size));
        }
        if (!loaded) {
            const unsigned char* legacy = sqlite3_column_text(stmt, 2);
            if (legacy) {
                loaded = replay.DetailsFromJson(reinterpret_cast<const char*>(legacy));
            }
        }
    }
    sqlite3_finalize(stmt);
    return loaded;
}

bool Database::ClearReplayDetails(const std::string& file_path) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db || file_path.empty()) return false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db,
            "UPDATE replays SET details_blob = NULL, details_format = 0, details_json = NULL WHERE file_path = ?;",
            -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, file_path.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(m_db) > 0;
    sqlite3_finalize(stmt);
    if (ok) BumpRevision();
    return ok;
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

    std::string count_sql = R"(
        WITH ally_replays AS (
            SELECT
                CASE WHEN is_user_handle(p.p2_handle) OR (p.p2_handle != '' AND instr(p.file_path, p.p2_handle) > 0)
                     THEN p.p1_name ELSE p.p2_name END as player_name
            FROM replays p
        )
        SELECT COUNT(DISTINCT a.player_name)
        FROM ally_replays a
        LEFT JOIN player_notes n ON a.player_name = n.player_name
        WHERE a.player_name IS NOT NULL AND a.player_name != ''
          AND a.player_name NOT IN ('莫比斯军团', '埃蒙的部队', "Amon's Forces", 'Moebius Forces')
          AND a.player_name != '埃蒙'
    )";
    if (!search_filter.empty()) {
        count_sql += " AND (a.player_name LIKE ? OR n.note LIKE ?)";
    }
    count_sql += ";";

    sqlite3_stmt* count_stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, count_sql.c_str(), -1, &count_stmt, nullptr) == SQLITE_OK) {
        if (!search_filter.empty()) {
            std::string pat = "%" + search_filter + "%";
            sqlite3_bind_text(count_stmt, 1, pat.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(count_stmt, 2, pat.c_str(), -1, SQLITE_TRANSIENT);
        }
        if (sqlite3_step(count_stmt) == SQLITE_ROW) {
            out_total_count = sqlite3_column_int(count_stmt, 0);
        }
        sqlite3_finalize(count_stmt);
    } else {
        std::cerr << "[DB ERROR] QueryPlayers count prepare failed: " << sqlite3_errmsg(m_db) << "\n";
    }

    std::string order_sql = "wins DESC";
    if (sort_column == "wins") order_sql = sort_ascending ? "wins ASC" : "wins DESC";
    else if (sort_column == "losses") order_sql = sort_ascending ? "losses ASC" : "losses DESC";
    else if (sort_column == "winrate") order_sql = sort_ascending ? "winrate ASC" : "winrate DESC";
    else if (sort_column == "apm") order_sql = sort_ascending ? "median_apm ASC" : "median_apm DESC";
    else if (sort_column == "name") order_sql = sort_ascending ? "player_name ASC" : "player_name DESC";
    else if (sort_column == "total_games") order_sql = sort_ascending ? "total_games ASC" : "total_games DESC";

    std::string query_sql = R"(
        WITH ally_replays AS (
            SELECT
                CASE WHEN is_user_handle(p.p2_handle) OR (p.p2_handle != '' AND instr(p.file_path, p.p2_handle) > 0)
                     THEN p.p1_name ELSE p.p2_name END as player_name,
                CASE WHEN is_user_handle(p.p2_handle) OR (p.p2_handle != '' AND instr(p.file_path, p.p2_handle) > 0)
                     THEN p.p1_apm ELSE p.p2_apm END as player_apm,
                p.result,
                p.file_mtime
            FROM replays p
        )
        SELECT a.player_name,
               COUNT(*) as total_games,
               SUM(CASE WHEN a.result = 'Victory' THEN 1 ELSE 0 END) as wins,
               SUM(CASE WHEN a.result = 'Defeat' THEN 1 ELSE 0 END) as losses,
               ROUND(100.0 * SUM(CASE WHEN a.result = 'Victory' THEN 1 ELSE 0 END) /
                     NULLIF(SUM(CASE WHEN a.result IN ('Victory', 'Defeat') THEN 1 ELSE 0 END), 0), 1) as winrate,
               ROUND(AVG(a.player_apm)) as median_apm,
               MAX(a.file_mtime) as last_played,
               COALESCE(n.note, '') as note
        FROM ally_replays a
        LEFT JOIN player_notes n ON a.player_name = n.player_name
        WHERE a.player_name IS NOT NULL AND a.player_name != ''
          AND a.player_name NOT IN ('莫比斯军团', '埃蒙的部队', "Amon's Forces", 'Moebius Forces')
          AND a.player_name != '埃蒙'
    )";
    if (!search_filter.empty()) {
        query_sql += " AND (a.player_name LIKE ? OR n.note LIKE ?)";
    }
    query_sql += " GROUP BY a.player_name ORDER BY " + order_sql + " LIMIT ? OFFSET ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, query_sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "[DB ERROR] QueryPlayers prepare failed: " << sqlite3_errmsg(m_db) << "\n";
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
        pr.name = safe_text(stmt, 0);
        pr.total_games = sqlite3_column_int(stmt, 1);
        pr.wins = sqlite3_column_int(stmt, 2);
        pr.losses = sqlite3_column_int(stmt, 3);
        pr.winrate = sqlite3_column_double(stmt, 4);
        pr.median_apm = sqlite3_column_int(stmt, 5);
        pr.last_played_mtime = sqlite3_column_int64(stmt, 6);
        pr.note = safe_text(stmt, 7);
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
    if (ok) BumpRevision();
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
    if (ok) BumpRevision();
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

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return sum;
    }

    int pidx = 1;
    if (!date_from_val.empty()) {
        sqlite3_bind_text(stmt, pidx++, date_from_val.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (!date_to_val.empty()) {
        sqlite3_bind_text(stmt, pidx++, date_to_val.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (!ally_pattern.empty()) {
        sqlite3_bind_text(stmt, pidx++, ally_pattern.c_str(), -1, SQLITE_TRANSIENT);
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

    static const std::unordered_set<std::string> kKnownCommanders = {
        "Raynor", "Kerrigan", "Artanis", "Swann", "Zagara", "Vorazun",
        "Karax", "Abathur", "Alarak", "Nova", "Stukov", "Fenix",
        "Dehaka", "HanHorner", "Tychus", "Zeratul", "Stetmann", "Mengsk"
    };

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* m_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* r_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        int dur = sqlite3_column_int(stmt, 2);
        const char* p1c_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        int p1_apm = sqlite3_column_int(stmt, 4);
        const char* p2c_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        int p2_apm = sqlite3_column_int(stmt, 6);
        const char* diff_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        int bp = sqlite3_column_int(stmt, 8);
        const char* reg_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        const char* path_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
        const char* p2h_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
        const char* p1h_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 12));
        const char* date_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 13));

        bool p2_is_me = (p2h_str && p2h_str[0] != '\0' && (AppState::Instance().HasUserHandle(p2h_str) || (path_str && strstr(path_str, p2h_str) != nullptr)));
        bool p1_is_user = (p1h_str && p1h_str[0] != '\0' && AppState::Instance().HasUserHandle(p1h_str));
        bool p2_is_user = (p2h_str && p2h_str[0] != '\0' && AppState::Instance().HasUserHandle(p2h_str));
        if (p1_is_user && !p2_is_user) {
            p2_is_me = false;
        }

        const char* my_cmdr_str = p2_is_me ? p2c_str : p1c_str;
        int my_apm = p2_is_me ? p2_apm : p1_apm;
        const char* ally_cmdr_str = p2_is_me ? p1c_str : p2c_str;
        int ally_apm = p2_is_me ? p1_apm : p2_apm;

        std::string map_name = m_str ? m_str : "Unknown";
        std::string result = r_str ? r_str : "Defeat";
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
        if (date_str && date_str[0] != '\0') {
            const std::string date(date_str);
            if (sum.first_game_date.empty() || date < sum.first_game_date) sum.first_game_date = date;
            if (sum.last_game_date.empty() || date > sum.last_game_date) sum.last_game_date = date;
            active_dates.insert(date.size() >= 10 ? date.substr(0, 10) : date);
        }

        // Maps
        auto& ma = map_map[map_name];
        if (is_victory) ma.wins++; else ma.losses++;
        ma.total_seconds += dur;
        if (dur > 0 && (ma.fastest_seconds == 0 || dur < ma.fastest_seconds)) {
            ma.fastest_seconds = dur;
        }

        // My Commander
        if (my_cmdr_str && my_cmdr_str[0] != '\0' && strcmp(my_cmdr_str, "Unknown") != 0) {
            std::string cmdr = (strcmp(my_cmdr_str, "Horner") == 0) ? "HanHorner" : my_cmdr_str;
            if (kKnownCommanders.count(cmdr)) {
                auto& c = p1_map[cmdr];
                c.total_games++;
                if (is_victory) c.wins++; else c.losses++;
                c.total_apm += my_apm;
                c.total_duration_seconds += valid_duration;
            }
        }

        // Ally Commander
        if (ally_cmdr_str && ally_cmdr_str[0] != '\0' && strcmp(ally_cmdr_str, "Unknown") != 0) {
            std::string cmdr = (strcmp(ally_cmdr_str, "Horner") == 0) ? "HanHorner" : ally_cmdr_str;
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
        } else if (diff_str) {
            d_label = diff_str;
        }
        if (!d_label.empty() && d_label.find('/') == std::string::npos) {
            auto& da = diff_map[d_label];
            da.total++;
            if (is_victory) da.wins++; else da.losses++;
        }

        // Region
        if (reg_str && (strcmp(reg_str, "NA") == 0 || strcmp(reg_str, "EU") == 0 || strcmp(reg_str, "KR") == 0 || strcmp(reg_str, "CN") == 0)) {
            auto& ra = reg_map[reg_str];
            ra.total++;
            if (is_victory) ra.wins++; else ra.losses++;
        }
    }
    sqlite3_finalize(stmt);

    sum.avg_duration_seconds = sum.total_games > 0
        ? static_cast<int>(sum.total_duration_seconds / sum.total_games)
        : 0;
    sum.active_days = static_cast<int>(active_dates.size());

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

Database::WeeklyRecord Database::QueryWeeklyRecord(const std::string& map_name, const std::vector<std::string>& mutators) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    WeeklyRecord wr;
    if (!m_db) return wr;

    std::string sql = "SELECT result, difficulty, file_path, duration_seconds FROM replays WHERE map_name = ? AND is_weekly = 1";
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
        std::string res = safe_text(stmt, 0);
        std::string diff = safe_text(stmt, 1);
        std::string fp = safe_text(stmt, 2);
        int dur = sqlite3_column_int(stmt, 3);

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

    sqlite3_finalize(stmt);
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
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
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
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ReplayWeeklyItem it;
        const char* r = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* d = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* f = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const char* m = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const char* mj = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        if (r) it.result = r;
        if (d) it.difficulty = d;
        if (f) it.file_path = f;
        if (m) it.map_name = m;
        if (mj) it.mutators_json = mj;
        it.duration_seconds = sqlite3_column_int(stmt, 5);
        items.push_back(std::move(it));
    }
    sqlite3_finalize(stmt);

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
    int weekly_count = 0;
    sqlite3_stmt* check_stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM replays WHERE is_weekly = 1;", -1, &check_stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(check_stmt) == SQLITE_ROW) {
            weekly_count = sqlite3_column_int(check_stmt, 0);
        }
        sqlite3_finalize(check_stmt);
    }
    if (weekly_count > 0) return; // Already populated

    sqlite3_stmt* fetch_stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT file_path FROM replays;", -1, &fetch_stmt, nullptr) != SQLITE_OK) {
        return;
    }
    std::vector<std::string> paths;
    while (sqlite3_step(fetch_stmt) == SQLITE_ROW) {
        const char* p = reinterpret_cast<const char*>(sqlite3_column_text(fetch_stmt, 0));
        if (p) paths.emplace_back(p);
    }
    sqlite3_finalize(fetch_stmt);

    if (paths.empty()) return;

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

    if (updates.empty()) return;

    sqlite3_stmt* update_stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "UPDATE replays SET is_weekly = 1, mutators_json = ? WHERE file_path = ?;", -1, &update_stmt, nullptr) != SQLITE_OK) {
        return;
    }

    sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    for (const auto& [mj, path] : updates) {
        sqlite3_bind_text(update_stmt, 1, mj.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(update_stmt, 2, path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(update_stmt);
        sqlite3_reset(update_stmt);
    }
    sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
    sqlite3_finalize(update_stmt);
    BumpRevision();
}

// Deprecated / Obsolete:
// Slot swapping was deprecated in favor of preserving authentic replay player slots (p1 and p2)
// exactly as recorded in the replay data (see commit 15a63de).
// Instead of physically mutating the stored rows in SQLite, the UI and query layers
// (e.g., TabGames, ModalGameDetail, QueryPlayers, and CalculateStats) now dynamically identify
// which slot belongs to the local user ("Me") and which belongs to the partner ("Ally") based on
// user account handles and file paths.
void Database::AlignPlayerSlotsWithUserAccount() {
    return;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db) return;

    const char* select_sql = R"(
        SELECT file_path, p1_name, p1_handle, p1_commander, p1_prestige, p1_apm, p1_kills,
                          p2_name, p2_handle, p2_commander, p2_prestige, p2_apm, p2_kills,
                          details_json, chat_json
        FROM replays
        WHERE p2_handle != '' AND instr(file_path, p2_handle) > 0 AND (p1_handle = '' OR instr(file_path, p1_handle) = 0);
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, select_sql, -1, &stmt, nullptr) != SQLITE_OK) return;

    struct InvertedRow {
        std::string file_path;
        std::string p1_name, p1_handle, p1_commander;
        int p1_prestige = 0, p1_apm = 0, p1_kills = 0;
        std::string p2_name, p2_handle, p2_commander;
        int p2_prestige = 0, p2_apm = 0, p2_kills = 0;
        std::string details_json;
        std::string chat_json;
    };

    std::vector<InvertedRow> rows;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        InvertedRow r;
        r.file_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        r.p1_name = sqlite3_column_text(stmt, 1) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)) : "";
        r.p1_handle = sqlite3_column_text(stmt, 2) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)) : "";
        r.p1_commander = sqlite3_column_text(stmt, 3) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)) : "";
        r.p1_prestige = sqlite3_column_int(stmt, 4);
        r.p1_apm = sqlite3_column_int(stmt, 5);
        r.p1_kills = sqlite3_column_int(stmt, 6);

        r.p2_name = sqlite3_column_text(stmt, 7) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7)) : "";
        r.p2_handle = sqlite3_column_text(stmt, 8) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8)) : "";
        r.p2_commander = sqlite3_column_text(stmt, 9) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9)) : "";
        r.p2_prestige = sqlite3_column_int(stmt, 10);
        r.p2_apm = sqlite3_column_int(stmt, 11);
        r.p2_kills = sqlite3_column_int(stmt, 12);

        r.details_json = sqlite3_column_text(stmt, 13) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 13)) : "";
        r.chat_json = sqlite3_column_text(stmt, 14) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 14)) : "";
        rows.push_back(std::move(r));
    }
    sqlite3_finalize(stmt);

    if (rows.empty()) return;

    sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    const char* update_sql = R"(
        UPDATE replays
        SET p1_name = ?, p1_handle = ?, p1_commander = ?, p1_prestige = ?, p1_apm = ?, p1_kills = ?,
            p2_name = ?, p2_handle = ?, p2_commander = ?, p2_prestige = ?, p2_apm = ?, p2_kills = ?,
            details_json = ?, chat_json = ?
        WHERE file_path = ?;
    )";

    sqlite3_stmt* ustmt = nullptr;
    if (sqlite3_prepare_v2(m_db, update_sql, -1, &ustmt, nullptr) == SQLITE_OK) {
        for (const auto& r : rows) {
            std::string new_details = r.details_json;
            if (!new_details.empty()) {
                try {
                    auto j = nlohmann::json::parse(new_details);
                    if (j.contains("p1_prestige_name") && j.contains("p2_prestige_name")) {
                        std::swap(j["p1_prestige_name"], j["p2_prestige_name"]);
                    }
                    if (j.contains("p1_masteries") && j.contains("p2_masteries")) {
                        std::swap(j["p1_masteries"], j["p2_masteries"]);
                    }
                    if (j.contains("p1_units") && j.contains("p2_units")) {
                        std::swap(j["p1_units"], j["p2_units"]);
                    }
                    if (j.contains("p1_series") && j.contains("p2_series")) {
                        std::swap(j["p1_series"], j["p2_series"]);
                    }
                    new_details = j.dump();
                } catch (...) {}
            }

            std::string new_chat = r.chat_json;
            if (!new_chat.empty()) {
                try {
                    auto cj = nlohmann::json::parse(new_chat);
                    if (cj.is_array()) {
                        for (auto& item : cj) {
                            if (item.contains("pid")) {
                                int pid = item["pid"].get<int>();
                                if (pid == 1) {
                                    item["pid"] = 2;
                                    if (!r.p1_name.empty()) item["name"] = r.p1_name;
                                } else if (pid == 2) {
                                    item["pid"] = 1;
                                    if (!r.p2_name.empty()) item["name"] = r.p2_name;
                                }
                            }
                        }
                        new_chat = cj.dump();
                    }
                } catch (...) {}
            }

            sqlite3_bind_text(ustmt, 1, r.p2_name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(ustmt, 2, r.p2_handle.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(ustmt, 3, r.p2_commander.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(ustmt, 4, r.p2_prestige);
            sqlite3_bind_int(ustmt, 5, r.p2_apm);
            sqlite3_bind_int(ustmt, 6, r.p2_kills);

            sqlite3_bind_text(ustmt, 7, r.p1_name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(ustmt, 8, r.p1_handle.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(ustmt, 9, r.p1_commander.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(ustmt, 10, r.p1_prestige);
            sqlite3_bind_int(ustmt, 11, r.p1_apm);
            sqlite3_bind_int(ustmt, 12, r.p1_kills);

            sqlite3_bind_text(ustmt, 13, new_details.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(ustmt, 14, new_chat.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(ustmt, 15, r.file_path.c_str(), -1, SQLITE_TRANSIENT);

            sqlite3_step(ustmt);
            sqlite3_reset(ustmt);
        }
        sqlite3_finalize(ustmt);
    }
    sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
    BumpRevision();
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
