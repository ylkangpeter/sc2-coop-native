#include <iostream>
#include <cassert>
#include <vector>
#include <filesystem>
#include "../src/db/database.h"

int main() {
    std::cout << "[TEST] Running SQLite Database tests..." << std::endl;

    std::string test_db = "test_coop.db";
    std::filesystem::remove(test_db);

    auto& db = sc2::Database::Instance();
    bool ok = db.Open(test_db);
    assert(ok);
    assert(db.IsOpen());

    // 1. Insert mock replays
    std::vector<sc2::ReplayRecord> recs;
    for (int i = 0; i < 50; ++i) {
        sc2::ReplayRecord r;
        r.file_path = "C:\\Replays\\game_" + std::to_string(i) + ".SC2Replay";
        r.file_mtime = 1700000000 + i * 3600;
        r.game_date = "2024-03-" + std::to_string(10 + i % 20);
        r.map_name = (i % 3 == 0) ? "Dead of Night" : ((i % 3 == 1) ? "Void Thrashing" : "Oblivion Express");
        r.raw_map_name = r.map_name;
        r.duration_seconds = 900 + i * 10;
        r.form_length = "15:00";
        r.result = (i % 4 != 0) ? "Victory" : "Defeat";
        r.difficulty = "Brutal";
        r.region = "CN";
        r.enemy_race = (i % 2 == 0) ? "Protoss" : "Zerg";
        r.is_weekly = (i % 5 == 0);

        r.p1.name = "MyPlayer";
        r.p1.commander = (i % 2 == 0) ? "Raynor" : "Karax";
        r.p1.prestige = (i % 4);
        r.p1.apm = 120 + i;

        r.p2.name = (i % 2 == 0) ? "Ally_Alice" : "Ally_Bob";
        r.p2.commander = (i % 2 == 0) ? "Stetmann" : "Tychus";
        r.p2.prestige = 1;
        r.p2.apm = 100 + i;

        recs.push_back(r);
    }

    ok = db.BatchSaveReplays(recs);
    assert(ok);

    int count = db.GetTotalReplayCount();
    assert(count == 50);
    std::cout << "Successfully saved and counted " << count << " replays." << std::endl;

    // 2. Query replays with search query
    int total_found = 0;
    auto dead_replays = db.QueryReplays("Dead", 10, 0, total_found);
    assert(total_found > 0);
    std::cout << "Query 'Dead' found " << total_found << " replays." << std::endl;

    auto karax_replays = db.QueryReplays("Karax", 10, 0, total_found);
    assert(total_found > 0);
    std::cout << "Query 'Karax' found " << total_found << " replays." << std::endl;

    // 3. Test Player aggregation
    int total_players = 0;
    auto players = db.QueryPlayers("", "wins", false, 10, 0, total_players);
    assert(total_players == 2);
    std::cout << "Total distinct allies: " << total_players << std::endl;
    for (const auto& p : players) {
        std::cout << "  Ally: " << p.name << " Games: " << p.total_games << " Wins: " << p.wins << " Winrate: " << p.winrate << "%" << std::endl;
        assert(p.total_games > 0);
    }

    // 4. Test Player Note
    db.SetPlayerNote("Ally_Alice", "Good Stetmann player with fast Gary");
    std::string note = db.GetPlayerNote("Ally_Alice");
    assert(note == "Good Stetmann player with fast Gary");
    std::cout << "Player note verified: " << note << std::endl;

    // 5. Test Stats Calculation
    sc2::StatsFilter filter;
    auto stats = db.CalculateStats(filter);
    std::cout << "Total games in stats (all): " << stats.total_games << " Overall winrate: " << stats.overall_winrate << "%" << std::endl;
    assert(stats.total_games == 50);
    assert(!stats.maps.empty());
    assert(!stats.my_commanders.empty());
    assert(!stats.ally_commanders.empty());

    // 5.1 Test Difficulty Filters
    sc2::StatsFilter filter_casual = filter;
    filter_casual.casual = true;
    filter_casual.normal = false;
    filter_casual.hard = false;
    filter_casual.brutal = false;
    filter_casual.brutal_plus = false;
    auto stats_casual = db.CalculateStats(filter_casual);
    std::cout << "Casual only games: " << stats_casual.total_games << std::endl;
    assert(stats_casual.total_games == 0);

    sc2::StatsFilter filter_brutal = filter;
    filter_brutal.casual = false;
    filter_brutal.normal = false;
    filter_brutal.hard = false;
    filter_brutal.brutal = true;
    filter_brutal.brutal_plus = false;
    auto stats_brutal = db.CalculateStats(filter_brutal);
    std::cout << "Brutal only games: " << stats_brutal.total_games << std::endl;
    assert(stats_brutal.total_games == 50);

    sc2::StatsFilter filter_none_diff = filter;
    filter_none_diff.casual = false;
    filter_none_diff.normal = false;
    filter_none_diff.hard = false;
    filter_none_diff.brutal = false;
    filter_none_diff.brutal_plus = false;
    auto stats_no_diff = db.CalculateStats(filter_none_diff);
    std::cout << "No difficulty selected games: " << stats_no_diff.total_games << std::endl;
    assert(stats_no_diff.total_games == 0);

    // 5.2 Test Region Filters
    sc2::StatsFilter filter_eu = filter;
    filter_eu.region_na = false;
    filter_eu.region_eu = true;
    filter_eu.region_kr = false;
    filter_eu.region_cn = false;
    auto stats_eu = db.CalculateStats(filter_eu);
    std::cout << "EU only games: " << stats_eu.total_games << std::endl;
    assert(stats_eu.total_games == 0);

    sc2::StatsFilter filter_cn = filter;
    filter_cn.region_na = false;
    filter_cn.region_eu = false;
    filter_cn.region_kr = false;
    filter_cn.region_cn = true;
    auto stats_cn = db.CalculateStats(filter_cn);
    std::cout << "CN only games: " << stats_cn.total_games << std::endl;
    assert(stats_cn.total_games == 50);

    // 5.3 Test Game Type Filters
    sc2::StatsFilter filter_normal = filter;
    filter_normal.normal_games = true;
    filter_normal.mutations = false;
    auto stats_normal = db.CalculateStats(filter_normal);
    std::cout << "Normal games only: " << stats_normal.total_games << std::endl;
    assert(stats_normal.total_games == 40);

    sc2::StatsFilter filter_mutations = filter;
    filter_mutations.normal_games = false;
    filter_mutations.mutations = true;
    auto stats_mutations = db.CalculateStats(filter_mutations);
    std::cout << "Mutations only: " << stats_mutations.total_games << std::endl;
    assert(stats_mutations.total_games == 10);

    // 5.4 Test Wins Only
    sc2::StatsFilter filter_wins = filter;
    filter_wins.wins_only = true;
    auto stats_wins = db.CalculateStats(filter_wins);
    std::cout << "Wins only: " << stats_wins.total_games << " wins: " << stats_wins.total_wins << std::endl;
    assert(stats_wins.total_games == 37);
    assert(stats_wins.total_losses == 0);

    // 6. Test Settings
    db.SetSetting("theme", "dark");
    db.SetSetting("font_scale", "1.2");
    assert(db.GetSetting("theme") == "dark");
    assert(db.GetSetting("font_scale") == "1.2");

    db.Close();
    std::filesystem::remove(test_db);

    std::cout << "[TEST] Database tests PASSED!\n" << std::endl;
    return 0;
}
