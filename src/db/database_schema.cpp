#include "database.h"
#include "sqlite_raii.h"
#include <sqlite3.h>
#include <iostream>
#include <cstring>
#include "../app_state.h"
#include <nlohmann/json.hpp>

namespace sc2 {

namespace {

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
        sqlite3_stmt* pStmt = nullptr;
        while ((pStmt = sqlite3_next_stmt(m_db, nullptr)) != nullptr) {
            sqlite3_finalize(pStmt);
        }
        sqlite3_close_v2(m_db);
        m_db = nullptr;
    }
}

bool Database::Open(const std::string& db_path) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_db) {
        sqlite3_stmt* pStmt = nullptr;
        while ((pStmt = sqlite3_next_stmt(m_db, nullptr)) != nullptr) {
            sqlite3_finalize(pStmt);
        }
        sqlite3_close_v2(m_db);
        m_db = nullptr;
    }

    int rc = sqlite3_open_v2(db_path.c_str(), &m_db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                             nullptr);
    if (rc != SQLITE_OK) {
        const char* message = m_db ? sqlite3_errmsg(m_db) : "unknown SQLite open error";
        std::cerr << "Failed to open SQLite database: " << message << std::endl;
        Close();
        return false;
    }

    // Register custom SQLite scalar functions
    if (sqlite3_create_function(m_db, "is_user_handle", 1, SQLITE_UTF8, nullptr, SqliteIsUserHandle, nullptr, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to register SQLite user-handle function: " << sqlite3_errmsg(m_db) << std::endl;
        Close();
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

        CREATE TABLE IF NOT EXISTS replay_file_states (
            file_path TEXT PRIMARY KEY,
            write_time INTEGER NOT NULL,
            file_size INTEGER NOT NULL,
            sha256 TEXT NOT NULL,
            outcome TEXT NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_replay_file_hash ON replay_file_states(sha256, outcome);
        CREATE TABLE IF NOT EXISTS replay_failed_hashes (
            sha256 TEXT PRIMARY KEY
        );
        CREATE TABLE IF NOT EXISTS replay_scan_progress (
            root TEXT PRIMARY KEY,
            last_update INTEGER NOT NULL
        );
        CREATE TABLE IF NOT EXISTS replay_scan_pending (
            root TEXT NOT NULL,
            file_path TEXT NOT NULL,
            write_time INTEGER NOT NULL,
            file_size INTEGER NOT NULL,
            PRIMARY KEY (root, file_path)
        );
        INSERT OR IGNORE INTO replay_failed_hashes
            SELECT sha256 FROM replay_file_states WHERE outcome = 'failed';

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
        Close();
        return false;
    }

    auto has_column = [&](const char* column_name) {
        SqliteStmt stmt(m_db, "PRAGMA table_info(replays);");
        if (stmt.IsValid()) {
            while (stmt.Step() == SQLITE_ROW) {
                std::string col = stmt.ColumnText(1);
                if (col == column_name) return true;
            }
        }
        return false;
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
        Close();
        return false;
    }

    if (!MigrateDetailsStorage()) {
        std::cerr << "Failed to migrate replay details storage." << std::endl;
        Close();
        return false;
    }

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
    SqliteStmt version_stmt(m_db, "SELECT value FROM settings WHERE key = 'details_storage_version' LIMIT 1;");
    if (version_stmt.IsValid() && version_stmt.Step() == SQLITE_ROW) {
        already_migrated = version_stmt.ColumnInt(0) >= 1;
    }
    if (already_migrated) return true;

    const char* select_sql =
        "SELECT file_path, details_json FROM replays "
        "WHERE (details_blob IS NULL OR length(details_blob) = 0) "
        "AND details_json IS NOT NULL AND details_json != '';";
    SqliteStmt select_stmt(m_db, select_sql);
    if (!select_stmt.IsValid()) {
        return false;
    }

    struct LegacyDetails { std::string path; std::string json; };
    std::vector<LegacyDetails> rows;
    while (select_stmt.Step() == SQLITE_ROW) {
        std::string path = select_stmt.ColumnText(0);
        std::string json = select_stmt.ColumnText(1);
        if (!path.empty() && !json.empty()) rows.push_back({path, json});
    }
    select_stmt.Finalize();

    SqliteTransaction txn(m_db);
    SqliteStmt update_stmt(m_db, "UPDATE replays SET details_blob = ?, details_format = 1 WHERE file_path = ?;");
    bool ok = update_stmt.IsValid();

    for (const auto& row : rows) {
        if (!ok) break;
        try {
            nlohmann::json::parse(row.json);
        } catch (...) {
            continue;
        }
        ReplayRecord details;
        const auto blob = details.DetailsFromJson(row.json)
            ? details.DetailsToCompressedBlob()
            : ReplayRecord::CompressJson(row.json);
        if (blob.empty()) continue;
        update_stmt.BindBlob(1, blob);
        update_stmt.BindText(2, row.path);
        ok = (update_stmt.Step() == SQLITE_DONE);
        update_stmt.Reset();
    }
    update_stmt.Finalize();

    if (ok) {
        ok = (sqlite3_exec(m_db, "UPDATE replays SET details_json = NULL WHERE details_blob IS NOT NULL;",
                           nullptr, nullptr, nullptr) == SQLITE_OK);
    }
    if (ok && txn.Commit()) {
        SetSetting("details_storage_version", "1");
        if (!rows.empty()) sqlite3_exec(m_db, "VACUUM;", nullptr, nullptr, nullptr);
        return true;
    }
    return false;
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

void Database::MarkDifficultyFixComplete() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db) return;
    SetSetting("difficulty_fix_version", "1");
    BumpRevision();
}

bool Database::SetSetting(const std::string& key, const std::string& value) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db) return false;
    SqliteStmt stmt(m_db, "INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?);");
    if (!stmt.IsValid()) return false;
    stmt.BindText(1, key);
    stmt.BindText(2, value);
    bool ok = (stmt.Step() == SQLITE_DONE);
    return ok;
}

std::string Database::GetSetting(const std::string& key, const std::string& default_val) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db) return default_val;
    SqliteStmt stmt(m_db, "SELECT value FROM settings WHERE key = ? LIMIT 1;");
    if (!stmt.IsValid()) return default_val;
    stmt.BindText(1, key);
    if (stmt.Step() == SQLITE_ROW) {
        return stmt.ColumnText(0);
    }
    return default_val;
}

} // namespace sc2
