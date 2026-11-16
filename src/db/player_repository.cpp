#include "database.h"
#include "sqlite_raii.h"
#include <sqlite3.h>
#include <iostream>
#include <ctime>

namespace sc2 {

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

    SqliteStmt count_stmt(m_db, count_sql);
    if (count_stmt.IsValid()) {
        if (!search_filter.empty()) {
            std::string pat = "%" + search_filter + "%";
            count_stmt.BindText(1, pat);
            count_stmt.BindText(2, pat);
        }
        if (count_stmt.Step() == SQLITE_ROW) {
            out_total_count = count_stmt.ColumnInt(0);
        }
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

    SqliteStmt stmt(m_db, query_sql);
    if (!stmt.IsValid()) {
        std::cerr << "[DB ERROR] QueryPlayers prepare failed: " << sqlite3_errmsg(m_db) << "\n";
        return players;
    }

    int pidx = 1;
    if (!search_filter.empty()) {
        std::string pat = "%" + search_filter + "%";
        stmt.BindText(pidx++, pat);
        stmt.BindText(pidx++, pat);
    }
    stmt.BindInt(pidx++, limit);
    stmt.BindInt(pidx++, offset);

    while (stmt.Step() == SQLITE_ROW) {
        PlayerRecord pr;
        pr.name = stmt.ColumnText(0);
        pr.total_games = stmt.ColumnInt(1);
        pr.wins = stmt.ColumnInt(2);
        pr.losses = stmt.ColumnInt(3);
        pr.winrate = stmt.ColumnDouble(4);
        pr.median_apm = stmt.ColumnInt(5);
        pr.last_played_mtime = stmt.ColumnInt64(6);
        pr.note = stmt.ColumnText(7);
        players.push_back(std::move(pr));
    }

    return players;
}

bool Database::SetPlayerNote(const std::string& player_name, const std::string& note) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db) return false;
    SqliteStmt stmt(m_db, "INSERT OR REPLACE INTO player_notes (player_name, note, updated_at) VALUES (?, ?, ?);");
    if (!stmt.IsValid()) return false;

    stmt.BindText(1, player_name);
    stmt.BindText(2, note);
    stmt.BindInt64(3, time(nullptr));

    bool ok = (stmt.Step() == SQLITE_DONE);
    if (ok) BumpRevision();
    return ok;
}

std::string Database::GetPlayerNote(const std::string& player_name) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db) return "";
    SqliteStmt stmt(m_db, "SELECT note FROM player_notes WHERE player_name = ? LIMIT 1;");
    if (!stmt.IsValid()) return "";
    stmt.BindText(1, player_name);
    if (stmt.Step() == SQLITE_ROW) {
        return stmt.ColumnText(0);
    }
    return "";
}

} // namespace sc2
