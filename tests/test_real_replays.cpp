#include <iostream>
#include <cassert>
#include <filesystem>
#include <chrono>
#include "../src/replay/replay_parser.h"
#include "../src/db/database.h"
#include "../src/utils/win32_utils.h"

int main(int argc, char** argv) {
    std::cout << "========================================\n";
    std::cout << "TEST: Real SC2 Replay Parsing & DB Load\n";
    std::cout << "========================================\n";

    std::filesystem::path replay_dir;
    if (argc > 1) {
        replay_dir = sc2::Win32Utils::Utf8ToWide(argv[1]);
    } else {
        std::string acc = sc2::Win32Utils::FindDefaultAccountsFolder();
        if (!acc.empty() && std::filesystem::exists(sc2::Win32Utils::Utf8ToWide(acc))) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(sc2::Win32Utils::Utf8ToWide(acc))) {
                if (entry.is_directory() && entry.path().filename() == L"Multiplayer") {
                    replay_dir = entry.path();
                    break;
                }
            }
        }
    }

    if (replay_dir.empty() || !std::filesystem::exists(replay_dir)) {
        std::cout << "[SKIP] No Multiplayer replay directory found on this system.\n";
        return 0;
    }

    // 1. Initialize DB
    std::remove("test_real.db");
    bool db_ok = sc2::Database::Instance().Open("test_real.db");
    assert(db_ok);
    std::cout << "[OK] Database opened successfully.\n";

    // 2. Collect sample replays (official matchmade co-op games)
    std::vector<std::filesystem::path> files;
    auto cm_file = replay_dir / L"[CM] 湮灭快车 - P.SC2Replay";
    if (std::filesystem::exists(cm_file)) {
        files.push_back(cm_file);
    }
    for (const auto& entry : std::filesystem::directory_iterator(replay_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".SC2Replay") {
            auto wfilename = entry.path().filename().wstring();
            if (wfilename.find(L"亡者之夜 (") != std::wstring::npos) {
                files.push_back(entry.path());
                if (files.size() >= 25) break;
            }
        }
    }
    std::cout << "Found " << files.size() << " sample replays to test.\n";
    assert(!files.empty());

    // 3. Parse and batch insert
    std::vector<sc2::ReplayRecord> parsed_records;
    auto t0 = std::chrono::high_resolution_clock::now();

    int success_count = 0;
    for (size_t i = 0; i < files.size(); ++i) {
        auto rec = sc2::ReplayParser::ParseFile(files[i].wstring());
        if (rec.has_value()) {
            success_count++;
            parsed_records.push_back(rec.value());
            std::cout << "[" << success_count << "] Parsed: " 
                      << files[i].filename().string() 
                      << " | Map: " << rec->map_name
                      << " | Dur: " << rec->duration_seconds << "s"
                      << " | P1: " << rec->p1.name << " (" << rec->p1.commander << " " << rec->p1.prestige << ")"
                      << " | P2: " << rec->p2.name << " (" << rec->p2.commander << " " << rec->p2.prestige << ")"
                      << " | Mutators: " << rec->mutators.size()
                      << " | Result: " << rec->result << "\n";
        } else {
            std::cout << "[SKIP] Could not parse / not co-op: " << files[i].filename().string() << "\n";
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::cout << "\nParsed " << success_count << " / " << files.size() << " replays in " 
              << elapsed_ms << " ms (" << (elapsed_ms / files.size()) << " ms/replay)\n";

    assert(success_count > 0);

    // 4. Batch Save to Database
    bool save_ok = sc2::Database::Instance().BatchSaveReplays(parsed_records);
    assert(save_ok);
    std::cout << "[OK] Batch saved to SQLite.\n";

    // 5. Test Queries: Count & Search
    int total_replays = sc2::Database::Instance().GetTotalReplayCount();
    std::cout << "[DB] Total replays in DB: " << total_replays << "\n";
    assert(total_replays == success_count);

    // Verify Chat was saved and can be queried back
    int q_total = 0;
    auto queried_games = sc2::Database::Instance().QueryReplays("湮灭快车", 10, 0, q_total);
    if (!queried_games.empty()) {
        const auto& qg = queried_games[0];
        std::cout << "[DB] Queried game with chat: " << qg.map_name << ", chat msgs: " << qg.chat.size() << "\n";
        for (const auto& msg : qg.chat) {
            std::cout << "  [Chat @" << msg.time_seconds << "s] " << msg.player_name << ": " << msg.text << "\n";
        }
        assert(!qg.chat.empty());
        assert(qg.chat[0].text == "-money");
        std::cout << "[OK] Chat messages successfully persisted and retrieved from SQLite!\n";
    }

    // 6. Test Player Aggregation
    int total_allies = 0;
    auto allies = sc2::Database::Instance().QueryPlayers("", "total_games", false, 100, 0, total_allies);
    std::cout << "[DB] Unique allies found: " << allies.size() << " (Total: " << total_allies << ")\n";
    for (const auto& ally : allies) {
        std::cout << "  Ally: " << ally.name << " | Games: " << ally.total_games 
                  << " | Winrate: " << ally.winrate << "% | APM: " << ally.median_apm << "\n";
    }

    // 7. Test Player Notes
    if (!allies.empty()) {
        std::string test_name = allies[0].name;
        sc2::Database::Instance().SetPlayerNote(test_name, "Great Raynor player!");
        assert(sc2::Database::Instance().GetPlayerNote(test_name) == "Great Raynor player!");
        std::cout << "[OK] Player note persisted and verified.\n";
    }

    // 8. Test Stats Aggregation
    sc2::StatsFilter filter;
    auto summary = sc2::Database::Instance().CalculateStats(filter);
    std::cout << "[STATS] Total Games: " << summary.total_games 
              << " | Wins: " << summary.total_wins 
              << " | Win Rate: " << summary.overall_winrate << "%\n";
    std::cout << "[STATS] Top Played Maps: " << summary.maps.size() << "\n";
    for (size_t i = 0; i < summary.maps.size() && i < 3; ++i) {
        std::cout << "  " << summary.maps[i].map_name << ": " << (summary.maps[i].wins + summary.maps[i].losses) << " games, " 
                  << summary.maps[i].winrate << "% win\n";
    }

    sc2::Database::Instance().Close();
    std::remove("test_real.db");
    std::cout << "\n>>> ALL REAL REPLAY TESTS PASSED 100%! <<<\n";
    return 0;
}
