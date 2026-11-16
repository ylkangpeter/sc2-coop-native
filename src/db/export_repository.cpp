#include "database.h"
#include "sqlite_raii.h"
#include <fstream>
#include <nlohmann/json.hpp>

namespace sc2 {

namespace {

static std::string ExportFileName(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace

bool Database::DumpAllToJson(const std::string& output_path) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db) return false;

    std::ofstream ofs(output_path);
    if (!ofs.is_open()) return false;

    SqliteStmt stmt(m_db, "SELECT file_path, game_date, map_name, duration_seconds, result, difficulty, "
                          "p1_name, p1_commander, p1_prestige, p1_apm, "
                          "p2_name, p2_commander, p2_prestige, p2_apm FROM replays ORDER BY game_date DESC;");
    if (!stmt.IsValid()) return false;

    ofs << "[\n";
    bool first = true;
    while (stmt.Step() == SQLITE_ROW) {
        if (!first) ofs << ",\n";
        first = false;
        nlohmann::json item;
        // Do not export the user's absolute Accounts path. The filename is
        // enough to identify the replay without leaking the local directory.
        item["file"] = ExportFileName(stmt.ColumnText(0));
        item["date"] = stmt.ColumnText(1);
        item["map"] = stmt.ColumnText(2);
        item["duration"] = stmt.ColumnInt(3);
        item["result"] = stmt.ColumnText(4);
        item["difficulty"] = stmt.ColumnText(5);
        item["p1_name"] = stmt.ColumnText(6);
        item["p1_commander"] = stmt.ColumnText(7);
        item["p1_prestige"] = stmt.ColumnInt(8);
        item["p1_apm"] = stmt.ColumnInt(9);
        item["p2_name"] = stmt.ColumnText(10);
        item["p2_commander"] = stmt.ColumnText(11);
        item["p2_prestige"] = stmt.ColumnInt(12);
        item["p2_apm"] = stmt.ColumnInt(13);
        ofs << "  " << item.dump();
    }
    ofs << "\n]\n";
    return true;
}

} // namespace sc2
