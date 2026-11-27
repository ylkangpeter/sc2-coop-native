#include <iostream>
#include <cassert>
#include <chrono>
#include <vector>
#include <filesystem>
#include "../vendor/sqlite3/sqlite3.h"
#include "../src/db/database.h"
#include "../src/app_state.h"
#include "../src/replay/replay_summary_cache.h"

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
    assert(stats.shortest_duration_seconds == 900);
    assert(stats.longest_duration_seconds == 1390);
    assert(stats.daily_activity.size() == 20);
    const auto march_10 = std::chrono::sys_seconds{
        std::chrono::sys_days{std::chrono::year{2024} / 3 / 10}
    }.time_since_epoch().count();
    for (std::size_t day = 0; day < stats.daily_activity.size(); ++day) {
        const auto& activity = stats.daily_activity[day];
        assert(activity.day_epoch_seconds == march_10 + static_cast<int64_t>(day) * 86400);
        assert(activity.total_games == (day < 10 ? 3 : 2));
    }
    assert(stats.daily_activity.front().total_duration_seconds == 3300);

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
    assert(stats_no_diff.shortest_duration_seconds == 0);
    assert(stats_no_diff.daily_activity.empty());

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

    // 7. Test GetValidReplayPaths & HasValidReplay
    {
        sc2::ReplayRecord r_valid;
        r_valid.file_path = "C:\\Replays\\valid_game.SC2Replay";
        r_valid.game_date = "2024-03-25";
        r_valid.map_name = "Void Thrashing";
        r_valid.raw_map_name = "Void Thrashing";
        r_valid.duration_seconds = 800;
        r_valid.result = "Victory";
        r_valid.difficulty = "Brutal";
        r_valid.brutal_plus = 0;
        r_valid.p1.name = "P1";
        r_valid.p1.commander = "Raynor";
        r_valid.p1.prestige = 0;
        r_valid.p2.name = "P2";
        r_valid.p2.commander = "Kerrigan";
        r_valid.p2.prestige = 1;

        sc2::ReplayRecord r_corrupt_prestige = r_valid;
        r_corrupt_prestige.file_path = "C:\\Replays\\corrupt_prestige.SC2Replay";
        r_corrupt_prestige.p1.prestige = -1;

        sc2::ReplayRecord r_missing_p2 = r_valid;
        r_missing_p2.file_path = "C:\\Replays\\missing_p2.SC2Replay";
        r_missing_p2.p2.name = "";

        sc2::ReplayRecord r_unknown_cmdr = r_valid;
        r_unknown_cmdr.file_path = "C:\\Replays\\unknown_cmdr.SC2Replay";
        r_unknown_cmdr.p1.commander = "Unknown";
        r_unknown_cmdr.raw_map_name = "Void Thrashing";

        sc2::ReplayRecord r_arcade_cmdr = r_valid;
        r_arcade_cmdr.file_path = "C:\\Replays\\arcade_cmdr.SC2Replay";
        r_arcade_cmdr.p1.commander = "Unknown";
        r_arcade_cmdr.raw_map_name = "[CM] Oblivion Express";

        db.BatchSaveReplays({r_valid, r_corrupt_prestige, r_missing_p2, r_unknown_cmdr, r_arcade_cmdr});

        assert(db.HasValidReplay(r_valid.file_path) == true);
        assert(db.HasValidReplay(r_corrupt_prestige.file_path) == false);
        assert(db.HasValidReplay(r_missing_p2.file_path) == false);
        assert(db.HasValidReplay(r_unknown_cmdr.file_path) == false);
        assert(db.HasValidReplay(r_arcade_cmdr.file_path) == true);

        auto val_paths = db.GetValidReplayPaths();
        assert(val_paths.count(r_valid.file_path) == 1);
        assert(val_paths.count(r_corrupt_prestige.file_path) == 0);
        assert(val_paths.count(r_missing_p2.file_path) == 0);
        assert(val_paths.count(r_unknown_cmdr.file_path) == 0);
        assert(val_paths.count(r_arcade_cmdr.file_path) == 1);
        std::cout << "Replay validation tests passed!" << std::endl;
    }

    // 8. Test is_user_handle & robust ally identification
    {
        std::string my_handle = "2-S2-1-123456";
        {
            std::lock_guard<std::mutex> lock(sc2::AppState::Instance().user_handles_mutex);
            sc2::AppState::Instance().user_handles.insert(my_handle);
        }

        sc2::ReplayRecord r_swap;
        r_swap.file_path = "C:\\Replays\\swapped_slots.SC2Replay"; // handle is NOT in file_path
        r_swap.game_date = "2025-01-01 12:00:00";
        r_swap.map_name = "Dead of Night";
        r_swap.raw_map_name = "Dead of Night";
        r_swap.duration_seconds = 950;
        r_swap.result = "Victory";
        r_swap.difficulty = "Brutal";
        r_swap.region = "CN";
        r_swap.p1.name = "RealAlly";
        r_swap.p1.handle = "2-S2-1-999999";
        r_swap.p1.commander = "Artanis";
        r_swap.p1.prestige = 0;
        r_swap.p1.apm = 150;
        r_swap.p2.name = "MyRealSelf";
        r_swap.p2.handle = my_handle; // user is in p2!
        r_swap.p2.commander = "Raynor";
        r_swap.p2.prestige = 0;
        r_swap.p2.apm = 220;

        db.BatchSaveReplays({r_swap});

        int total_p = 0;
        auto p_list = db.QueryPlayers("RealAlly", "wins", false, 10, 0, total_p);
        assert(total_p == 1);
        assert(p_list.size() == 1);
        assert(p_list[0].name == "RealAlly");
        std::cout << "Ally correctly identified as RealAlly even when user is in P2 slot without handle in file path!" << std::endl;

        // Also test CalculateStats p2_is_me
        sc2::StatsFilter sf;
        sf.date_from = "2025-01-01";
        sf.date_to = "2025-01-01";
        auto st = db.CalculateStats(sf);
        assert(st.total_games == 1);
        // My commander should be Raynor (p2), ally should be Artanis (p1)
        assert(st.my_commanders.size() == 1 && st.my_commanders[0].commander == "Raynor");
        assert(st.ally_commanders.size() == 1 && st.ally_commanders[0].commander == "Artanis");
        std::cout << "CalculateStats p2_is_me successfully matched user handle for commander stats!" << std::endl;
    }

    // 9. Test "Unknown" game results not counted as Defeat / Loss
    {
        sc2::ReplayRecord r_unk;
        r_unk.file_path = "C:\\Replays\\unknown_result.SC2Replay";
        r_unk.game_date = "2025-01-02 12:00:00";
        r_unk.map_name = "Void Launch";
        r_unk.raw_map_name = "Void Launch";
        r_unk.duration_seconds = 300;
        r_unk.result = "Unknown";
        r_unk.difficulty = "Brutal";
        r_unk.p1.name = "Me";
        r_unk.p1.commander = "Mengsk";
        r_unk.p1.prestige = 0;
        r_unk.p2.name = "UnknownResultAlly";
        r_unk.p2.commander = "Zagara";
        r_unk.p2.prestige = 0;

        sc2::ReplayRecord r_vic = r_unk;
        r_vic.file_path = "C:\\Replays\\vic_result.SC2Replay";
        r_vic.result = "Victory";

        db.BatchSaveReplays({r_unk, r_vic});

        // QueryPlayers winrate for UnknownResultAlly
        int total_p = 0;
        auto p_list = db.QueryPlayers("UnknownResultAlly", "wins", false, 10, 0, total_p);
        assert(p_list.size() == 1);
        // Wins = 1, Losses = 0, total_games = 2, winrate should be 100.0% (1 win out of 1 valid game), not 50%!
        assert(p_list[0].wins == 1);
        assert(p_list[0].losses == 0);
        assert(p_list[0].winrate == 100.0);
        std::cout << "QueryPlayers winrate ignores Unknown games: " << p_list[0].winrate << "%" << std::endl;

        // CalculateStats on 2025-01-02
        sc2::StatsFilter sf;
        sf.date_from = "2025-01-02";
        sf.date_to = "2025-01-02";
        auto st = db.CalculateStats(sf);
        // The "Unknown" replay must be skipped!
        assert(st.total_games == 1);
        assert(st.total_wins == 1);
        assert(st.total_losses == 0);
        assert(st.shortest_duration_seconds == 300);
        assert(st.daily_activity.size() == 1);
        assert(st.daily_activity.front().total_games == 1);
        assert(st.daily_activity.front().total_duration_seconds == 300);
        std::cout << "CalculateStats completely skips Unknown result games!" << std::endl;
    }

    // 10. Test QueryWeeklyRecord completeness & DiffRank
    {
        std::string wm_map = "Miner Evacuation";
        std::vector<std::string> muts = {"Polarity", "Speed Freaks"};

        sc2::ReplayRecord r1;
        r1.file_path = "C:\\Replays\\weekly_1.SC2Replay";
        r1.game_date = "2025-01-03 12:00:00";
        r1.map_name = wm_map;
        r1.raw_map_name = wm_map;
        r1.duration_seconds = 1200;
        r1.result = "Victory";
        r1.difficulty = "Hard";
        r1.is_weekly = true;
        r1.MutatorsFromJson("[\"Polarity\", \"Speed Freaks\"]");
        r1.p1.name = "P1";
        r1.p1.commander = "Nova";
        r1.p2.name = "P2";
        r1.p2.commander = "Dehaka";

        sc2::ReplayRecord r2 = r1;
        r2.file_path = "C:\\Replays\\weekly_2.SC2Replay";
        r2.duration_seconds = 900;
        r2.difficulty = "Brutal+2";

        sc2::ReplayRecord r3 = r1;
        r3.file_path = "C:\\Replays\\weekly_3.SC2Replay";
        r3.duration_seconds = 600;
        r3.difficulty = "Normal"; // lower diff, faster win

        sc2::ReplayRecord r4 = r1;
        r4.file_path = "C:\\Replays\\weekly_4.SC2Replay";
        r4.duration_seconds = 400;
        r4.result = "Defeat";
        r4.difficulty = "Brutal+4";

        db.BatchSaveReplays({r1, r2, r3, r4});

        auto wr = db.QueryWeeklyRecord(wm_map, muts);
        assert(wr.wins == 3);
        assert(wr.losses == 1);
        assert(wr.winrate == 75.0);
        // highest difficulty among victories should be Brutal+2, not Normal (which was the last victory inserted)
        assert(wr.highest_diff == "Brutal+2");
        // fastest win should be 600 seconds, not defeat's 400s or 0
        assert(wr.fastest_win_seconds == 600);
        assert(wr.replay_files.size() == 4);
        std::cout << "QueryWeeklyRecord verified: highest_diff=" << wr.highest_diff
                  << " fastest_win=" << wr.fastest_win_seconds << "s winrate=" << wr.winrate << "%" << std::endl;
    }

    // The weekly catalog must exclude custom mutations with the same map and mutators.
    {
        sc2::ReplayRecord weekly;
        weekly.file_path = "catalog_weekly_win.SC2Replay";
        weekly.map_name = "Rifts to Korhal";
        weekly.raw_map_name = weekly.map_name;
        weekly.result = "Victory";
        weekly.difficulty = "Hard";
        weekly.duration_seconds = 1000;
        weekly.is_weekly = true;
        weekly.MutatorsFromJson("[\"Twister\", \"Void Rifts\"]");

        auto loss = weekly;
        loss.file_path = "catalog_weekly_loss.SC2Replay";
        loss.result = "Defeat";

        auto tie = weekly;
        tie.file_path = "catalog_weekly_tie.SC2Replay";
        tie.result = "Tie";

        auto custom = weekly;
        custom.file_path = "catalog_custom_win.SC2Replay";
        custom.is_weekly = false;
        custom.difficulty = "Brutal+6";
        custom.duration_seconds = 100;

        auto custom_loss = custom;
        custom_loss.file_path = "catalog_custom_loss.SC2Replay";
        custom_loss.result = "Defeat";

        auto unknown = weekly;
        unknown.file_path = "catalog_unknown.SC2Replay";
        unknown.map_name = "Oblivion Express";
        unknown.result = "Unknown";
        unknown.MutatorsFromJson("[\"Darkness\", \"Outbreak\", \"Walking Infested\"]");
        assert(db.BatchSaveReplays({weekly, loss, tie, custom, custom_loss, unknown}));

        const auto records = db.QueryAllWeeklyRecords();
        assert(records.count(4) == 1);
        const auto& record = records.at(4);
        assert(record.wins == 1 && record.losses == 1);
        assert(record.winrate == 50.0);
        assert(record.highest_diff == "Hard");
        assert(record.fastest_win_seconds == 1000);
        assert(record.replay_files.size() == 3);
        assert(records.count(1) == 1);
        assert(records.at(1).replay_files.size() == 1);
        assert(records.at(1).wins == 0 && records.at(1).losses == 0);
    }

    // 11. Verify compressed details round-trip and series downsampling.
    {
        sc2::ReplayRecord detailed;
        detailed.file_path = "C:\\Replays\\compressed_details.SC2Replay";
        detailed.game_date = "2024-04-01 12:00:00";
        detailed.map_name = "Dead of Night";
        detailed.raw_map_name = detailed.map_name;
        detailed.result = "Victory";
        detailed.difficulty = "Brutal";
        detailed.p1.name = "MyPlayer";
        detailed.p1.commander = "Raynor";
        detailed.has_level_data = true;
        detailed.has_summary_data = true;
        detailed.bonus_total = 3;
        detailed.bonus_earned = 1;
        detailed.p1.contributions = {{"TychusCoop", 0}, {"TychusReaper", 0}, {"voidrifts", 26}};
        detailed.p1.commander_level = 15;
        detailed.p1.commander_mastery_level = 523;
        detailed.p2.name = "Ally_Alice";
        detailed.p2.commander = "Karax";
        detailed.p2.commander_level = 12;
        detailed.p2.commander_mastery_level = 0;
        assert(detailed.p1.FormatLevel() == "{523}");
        assert(detailed.p1.IsLevelMaxed() == true);
        assert(detailed.p2.FormatLevel() == "{12}");
        assert(detailed.p2.IsLevelMaxed() == false);
        for (int i = 0; i < 1024; ++i) {
            detailed.p1_series.time_stamps.push_back(static_cast<float>(i));
            detailed.p1_series.army_value.push_back(static_cast<float>(i * 2));
            detailed.p1_series.supply_used.push_back(static_cast<float>(i));
            detailed.p1_series.kill_count.push_back(static_cast<float>(i / 2));
            detailed.p1_series.collection_rate.push_back(100.0f + static_cast<float>(i));
        }
        detailed.p1.units.push_back({"Marine", 10, 1, 8, 0.5f});
        assert(db.SaveReplay(detailed));

        int history_total = 0;
        auto history_levels = db.QueryReplays("MyPlayer", 1000, 0, history_total);
        auto history_it = std::find_if(history_levels.begin(), history_levels.end(), [&](const auto& row) {
            return row.file_path == detailed.file_path;
        });
        assert(history_it != history_levels.end());
        const auto original_result = history_it->result;
        sc2::LoadHistoryLevels(*history_it, db);
        assert(history_it->p1.FormatLevel() == "{523}" && history_it->p1.IsLevelMaxed());
        assert(history_it->p2.FormatLevel() == "{12}" && !history_it->p2.IsLevelMaxed());
        assert(history_it->result == original_result && history_it->p1.units.empty());

        sc2::ReplayRecord loaded;
        loaded.file_path = detailed.file_path;
        loaded.p1.commander = detailed.p1.commander;
        loaded.p2.commander = detailed.p2.commander;
        assert(db.LoadReplayDetails(loaded));
        assert(loaded.has_summary_data && loaded.bonus_earned == 1 && loaded.bonus_total == 3);
        assert(loaded.p1.contributions.size() == 3 && loaded.p1.contributions[1].icon == "TychusReaper");
        assert(loaded.p1.contributions[2].count == 26 && loaded.p2.contributions.empty());
        assert(loaded.has_level_data == true);
        assert(loaded.p1.units.size() == 1);
        assert(loaded.p1.commander_level == 15);
        assert(loaded.p1.commander_mastery_level == 523);
        assert(loaded.p2.commander_level == 12);
        assert(loaded.p2.commander_mastery_level == 0);
        assert(loaded.p1.FormatLevel() == "{523}");
        assert(loaded.p1.IsLevelMaxed() == true);
        assert(loaded.p2.FormatLevel() == "{12}");
        assert(loaded.p2.IsLevelMaxed() == false);
        assert(loaded.p1_series.time_stamps.size() == sc2::ReplayRecord::kMaxStoredSeriesPoints);
        int64_t row_id = 0;
        assert(db.LoadReplayDetails(loaded, &row_id));
        loaded.p2.commander_level = 13;
        assert(db.UpdateReplayDetails(loaded, row_id));
        sc2::ReplayRecord updated;
        updated.file_path = loaded.file_path;
        assert(db.LoadReplayDetails(updated) && updated.p2.commander_level == 13);
        assert(db.SaveReplay(detailed));
        assert(!db.UpdateReplayDetails(loaded, row_id));
        sc2::ReplayRecord legacy;
        assert(legacy.DetailsFromJson("{}") && !legacy.has_level_data);
        auto blob = legacy.DetailsToCompressedBlob();
        assert(legacy.DetailsFromCompressedBlob(blob.data(), blob.size()) && !legacy.has_level_data);
        auto json = nlohmann::json::parse(detailed.DetailsToJson());
        json.erase("level_cache_version");
        json.erase("has_level_data");
        json["p1_commander_mastery_level"] = 0;
        assert(legacy.DetailsFromJson(json.dump()) && !legacy.has_level_data);
        assert(legacy.p1.commander_level == 0 && legacy.p2.commander_level == 12);
        json["has_level_data"] = true;
        assert(legacy.DetailsFromJson(json.dump()) && legacy.has_level_data && legacy.p1.commander_level == 15);
        json["p1_commander_level"] = json["p2_commander_level"] = 0;
        assert(legacy.DetailsFromJson(json.dump()) && !legacy.has_level_data);
        json["level_cache_version"] = 2;
        assert(legacy.DetailsFromJson(json.dump()) && legacy.has_level_data);
        json["has_level_data"] = false;
        assert(legacy.DetailsFromJson(json.dump()) && !legacy.has_level_data);
        std::cout << "Compressed details round-trip verified." << std::endl;
    }

    // A historical v1 summary is reparsed through the same entry used by the overlay.
    {
        const auto fixture = std::filesystem::path(L"tests/fixtures/sample.SC2Replay");
        auto parsed = sc2::ReplayParser::ParseFile(fixture.wstring());
        assert(parsed && parsed->has_summary_data);
        assert(db.SaveReplay(*parsed));
        auto stale_json = nlohmann::json::parse(parsed->DetailsToJson());
        assert(stale_json["summary_version"] == sc2::ReplayRecord::kSummaryCacheVersion);
        stale_json["summary_version"] = 1;
        stale_json["bonus_earned"] = 99;
        stale_json["enemy_comp"] = "stale cache sentinel";
        const auto stale_text = stale_json.dump();
        sqlite3* raw_db = nullptr;
        assert(sqlite3_open(test_db.c_str(), &raw_db) == SQLITE_OK);
        sqlite3_stmt* stmt = nullptr;
        assert(sqlite3_prepare_v2(raw_db, "UPDATE replays SET details_blob=NULL, details_format=0, details_json=? WHERE file_path=?", -1, &stmt, nullptr) == SQLITE_OK);
        sqlite3_bind_text(stmt, 1, stale_text.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, parsed->file_path.c_str(), -1, SQLITE_TRANSIENT);
        assert(sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(raw_db) == 1);
        sqlite3_finalize(stmt);
        sqlite3_close(raw_db);

        int total = 0;
        auto history = db.QueryReplays("", 1000, 0, total);
        auto entry = std::find_if(history.begin(), history.end(), [&](const auto& r) {
            return r.file_path == parsed->file_path;
        });
        assert(entry != history.end());
        sc2::LoadSummaryDetails(*entry, db);
        assert(entry->has_summary_data && entry->enemy_comp == parsed->enemy_comp);
        assert(entry->p1.kills == parsed->p1.kills && entry->p2.kills == parsed->p2.kills);
        sc2::ReplayRecord reloaded;
        reloaded.file_path = parsed->file_path;
        assert(db.LoadReplayDetails(reloaded));
        assert(reloaded.has_summary_data && reloaded.enemy_comp == parsed->enemy_comp);
        assert(reloaded.bonus_earned == parsed->bonus_earned && reloaded.bonus_total == parsed->bonus_total);
        assert(reloaded.p1.kills == parsed->p1.kills && reloaded.p2.kills == parsed->p2.kills);
    }

    // Legacy epoch placeholders must not move the career calendar to 1970.
    const auto original_calendar = db.CalculateStats(sc2::StatsFilter{});
    auto epoch = recs.front();
    epoch.file_path = "missing-date.SC2Replay";
    epoch.game_date = "1970-01-01 08:00";
    assert(db.SaveReplay(epoch));
    const auto with_epoch = db.CalculateStats(sc2::StatsFilter{});
    assert(with_epoch.total_games == original_calendar.total_games + 1);
    assert(with_epoch.first_game_date == original_calendar.first_game_date);
    assert(with_epoch.daily_activity.front().day_epoch_seconds == original_calendar.daily_activity.front().day_epoch_seconds);

    db.Close();
    std::filesystem::remove(test_db);

    std::cout << "[TEST] Database tests PASSED!\n" << std::endl;
    return 0;
}
