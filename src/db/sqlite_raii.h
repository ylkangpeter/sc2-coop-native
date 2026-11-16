#pragma once

#include <sqlite3.h>
#include <string>
#include <vector>
#include <stdexcept>
#include <utility>

namespace sc2 {

// Helper to safely extract string from SQLite column without null dereference
inline std::string SqliteSafeText(sqlite3_stmt* stmt, int col) {
    if (!stmt) return "";
    const unsigned char* txt = sqlite3_column_text(stmt, col);
    return txt ? reinterpret_cast<const char*>(txt) : "";
}

// RAII wrapper for sqlite3_stmt
class SqliteStmt {
public:
    SqliteStmt() = default;

    explicit SqliteStmt(sqlite3* db, const char* sql) {
        Prepare(db, sql);
    }

    explicit SqliteStmt(sqlite3* db, const std::string& sql) {
        Prepare(db, sql.c_str());
    }

    ~SqliteStmt() {
        Finalize();
    }

    SqliteStmt(const SqliteStmt&) = delete;
    SqliteStmt& operator=(const SqliteStmt&) = delete;

    SqliteStmt(SqliteStmt&& other) noexcept : m_stmt(other.m_stmt) {
        other.m_stmt = nullptr;
    }

    SqliteStmt& operator=(SqliteStmt&& other) noexcept {
        if (this != &other) {
            Finalize();
            m_stmt = other.m_stmt;
            other.m_stmt = nullptr;
        }
        return *this;
    }

    bool Prepare(sqlite3* db, const char* sql) {
        Finalize();
        if (!db || !sql) return false;
        return sqlite3_prepare_v2(db, sql, -1, &m_stmt, nullptr) == SQLITE_OK;
    }

    bool Prepare(sqlite3* db, const std::string& sql) {
        return Prepare(db, sql.c_str());
    }

    void Finalize() {
        if (m_stmt) {
            sqlite3_finalize(m_stmt);
            m_stmt = nullptr;
        }
    }

    sqlite3_stmt* Get() const { return m_stmt; }
    operator sqlite3_stmt*() const { return m_stmt; }
    bool IsValid() const { return m_stmt != nullptr; }

    int Step() { return sqlite3_step(m_stmt); }
    int Reset() { return sqlite3_reset(m_stmt); }
    int ClearBindings() { return sqlite3_clear_bindings(m_stmt); }

    // Binding helpers (1-based index)
    int BindText(int idx, const std::string& val) {
        return sqlite3_bind_text(m_stmt, idx, val.c_str(), -1, SQLITE_TRANSIENT);
    }

    int BindText(int idx, const char* val) {
        if (!val) return sqlite3_bind_null(m_stmt, idx);
        return sqlite3_bind_text(m_stmt, idx, val, -1, SQLITE_TRANSIENT);
    }

    int BindInt(int idx, int val) {
        return sqlite3_bind_int(m_stmt, idx, val);
    }

    int BindInt64(int idx, int64_t val) {
        return sqlite3_bind_int64(m_stmt, idx, val);
    }

    int BindDouble(int idx, double val) {
        return sqlite3_bind_double(m_stmt, idx, val);
    }

    int BindNull(int idx) {
        return sqlite3_bind_null(m_stmt, idx);
    }

    int BindBlob(int idx, const void* data, int size) {
        return sqlite3_bind_blob(m_stmt, idx, data, size, SQLITE_TRANSIENT);
    }

    int BindBlob(int idx, const std::vector<uint8_t>& bytes) {
        return sqlite3_bind_blob(m_stmt, idx, bytes.data(), static_cast<int>(bytes.size()), SQLITE_TRANSIENT);
    }

    // Column extraction helpers (0-based index)
    std::string ColumnText(int col) const {
        return SqliteSafeText(m_stmt, col);
    }

    int ColumnInt(int col) const {
        return sqlite3_column_int(m_stmt, col);
    }

    int64_t ColumnInt64(int col) const {
        return sqlite3_column_int64(m_stmt, col);
    }

    double ColumnDouble(int col) const {
        return sqlite3_column_double(m_stmt, col);
    }

    const void* ColumnBlob(int col) const {
        return sqlite3_column_blob(m_stmt, col);
    }

    int ColumnBytes(int col) const {
        return sqlite3_column_bytes(m_stmt, col);
    }

    std::vector<uint8_t> ColumnBlobBytes(int col) const {
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(sqlite3_column_blob(m_stmt, col));
        int bytes = sqlite3_column_bytes(m_stmt, col);
        if (!ptr || bytes <= 0) return {};
        return std::vector<uint8_t>(ptr, ptr + bytes);
    }

private:
    sqlite3_stmt* m_stmt = nullptr;
};

// RAII wrapper for SQLite transactions
class SqliteTransaction {
public:
    explicit SqliteTransaction(sqlite3* db) : m_db(db) {
        if (m_db) {
            m_started = (sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr) == SQLITE_OK);
        }
    }

    ~SqliteTransaction() {
        if (m_started && !m_committed && m_db) {
            sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        }
    }

    SqliteTransaction(const SqliteTransaction&) = delete;
    SqliteTransaction& operator=(const SqliteTransaction&) = delete;

    bool Commit() {
        if (!m_started || m_committed || !m_db) return false;
        m_committed = (sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK);
        return m_committed;
    }

    void Rollback() {
        if (m_started && !m_committed && m_db) {
            sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
            m_committed = true; // Mark done so destructor doesn't re-rollback
        }
    }

    bool IsActive() const { return m_started && !m_committed; }

private:
    sqlite3* m_db = nullptr;
    bool m_started = false;
    bool m_committed = false;
};

} // namespace sc2
