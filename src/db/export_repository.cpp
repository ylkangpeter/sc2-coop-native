#include "database.h"
#include <fstream>
#include <nlohmann/json.hpp>

namespace sc2 {

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
