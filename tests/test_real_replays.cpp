#include <iostream>
#include <cassert>
#include <filesystem>
#include <chrono>
#include <vector>
#include <string>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif
#include "../src/replay/replay_parser.h"
#include "../src/db/database.h"
#include "../src/utils/win32_utils.h"

int main(int argc, char** argv) {
    std::cout << "========================================\n";
    std::cout << "TEST: Real SC2 Replay Parsing & DB Load\n";
    std::cout << "========================================\n";

    std::vector<std::filesystem::path> files;

    // 1. Check command-line argument (file or directory)
    std::filesystem::path cli_path;
#ifdef _WIN32
    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (wargv) {
        if (wargc > 1) {
            cli_path = wargv[1];
        }
        LocalFree(wargv);
    }
#else
    if (argc > 1) {
        cli_path = sc2::Win32Utils::Utf8ToWide(argv[1]);
    }
#endif

    if (!cli_path.empty() && std::filesystem::exists(cli_path)) {
        if (std::filesystem::is_regular_file(cli_path)) {
            files.push_back(cli_path);
        } else if (std::filesystem::is_directory(cli_path)) {
            for (const auto& entry : std::filesystem::directory_iterator(cli_path)) {
                if (entry.is_regular_file()) {
                    auto ext = entry.path().extension().wstring();
                    for (auto& c : ext) c = towlower(c);
                    if (ext == L".sc2replay") {
                        files.push_back(entry.path());
                        if (files.size() >= 25) break;
                    }
                }
            }
        }
    }

    // 2. Check SC2 Accounts folder
    if (files.empty()) {
        std::string acc = sc2::Win32Utils::FindDefaultAccountsFolder();
        if (!acc.empty() && std::filesystem::exists(sc2::Win32Utils::Utf8ToWide(acc))) {
            std::filesystem::path replay_dir;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(sc2::Win32Utils::Utf8ToWide(acc))) {
                if (entry.is_directory() && entry.path().filename() == L"Multiplayer") {
                    replay_dir = entry.path();
                    break;
                }
            }
            if (!replay_dir.empty() && std::filesystem::exists(replay_dir)) {
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
                if (files.empty()) {
                    for (const auto& entry : std::filesystem::directory_iterator(replay_dir)) {
                        if (entry.is_regular_file()) {
                            auto ext = entry.path().extension().string();
                            for (auto& c : ext) c = tolower(c);
                            if (ext == ".sc2replay") {
                                files.push_back(entry.path());
                                if (files.size() >= 25) break;
                            }
                        }
                    }
                }
            }
        }
    }

    // 3. Check fixture sample replay (tests/fixtures/sample.SC2Replay or tests/fixtures/)
    if (files.empty()) {
        const std::vector<std::filesystem::path> fixture_dirs = {
            "tests/fixtures",
            "../tests/fixtures",
            "../../tests/fixtures"
        };
        for (const auto& fd : fixture_dirs) {
            if (std::filesystem::exists(fd) && std::filesystem::is_directory(fd)) {
                auto cm_file = fd / L"[CM] 湮灭快车 - P.SC2Replay";
                if (std::filesystem::exists(cm_file)) {
                    files.push_back(cm_file);
                }
                for (const auto& entry : std::filesystem::directory_iterator(fd)) {
                    if (entry.is_regular_file()) {
                        auto ext = entry.path().extension().wstring();
                        for (auto& c : ext) c = towlower(c);
                        if (ext == L".sc2replay" && entry.path() != cm_file) {
                            files.push_back(entry.path());
                        }
                    }
                }
            }
            if (!files.empty()) break;
        }

        if (files.empty()) {
            const std::vector<std::filesystem::path> fixture_files = {
                "tests/fixtures/sample.SC2Replay",
                "../tests/fixtures/sample.SC2Replay",
                "../../tests/fixtures/sample.SC2Replay"
            };
            for (const auto& fp : fixture_files) {
                if (std::filesystem::exists(fp)) {
                    files.push_back(fp);
                    break;
                }
            }
        }
    }

    // 4. Fail if no replays found anywhere
    if (files.empty()) {
        std::cout << "[FAIL] No SC2 replay file found to test parser!\n";
        return 1;
    }

    // Initialize DB
    std::remove("test_real.db");
    bool db_ok = sc2::Database::Instance().Open("test_real.db");
    assert(db_ok);
    std::cout << "[OK] Database opened successfully.\n";

    std::cout << "Found " << files.size() << " sample replays to test.\n";

    // Parse and batch insert
    std::vector<sc2::ReplayRecord> parsed_records;
    auto t0 = std::chrono::high_resolution_clock::now();

    int success_count = 0;
    for (size_t i = 0; i < files.size(); ++i) {
        auto rec = sc2::ReplayParser::ParseFile(files[i].wstring());
        if (rec.has_value()) {
            success_count++;
            parsed_records.push_back(rec.value());
            std::cout << "[" << success_count << "] Parsed: "
                      << sc2::Win32Utils::WideToUtf8(files[i].filename().wstring())
                      << " | Map: " << rec->map_name
                      << " | Dur: " << rec->duration_seconds << "s"
                      << " | P1: " << rec->p1.name << " (" << rec->p1.commander << " " << rec->p1.prestige << ")"
                      << " | P2: " << rec->p2.name << " (" << rec->p2.commander << " " << rec->p2.prestige << ")"
                      << " | Mutators: " << rec->mutators.size()
                      << " | Result: " << rec->result << "\n";
        } else {
            std::cout << "[SKIP] Could not parse / not co-op: " << sc2::Win32Utils::WideToUtf8(files[i].filename().wstring()) << "\n";
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
    } else {
        auto any_games = sc2::Database::Instance().QueryReplays("", 10, 0, q_total);
        for (const auto& qg : any_games) {
            if (!qg.chat.empty()) {
                std::cout << "[DB] Queried game with chat: " << qg.map_name << ", chat msgs: " << qg.chat.size() << "\n";
                for (const auto& msg : qg.chat) {
                    std::cout << "  [Chat @" << msg.time_seconds << "s] " << msg.player_name << ": " << msg.text << "\n";
                }
                assert(!qg.chat.empty());
                std::cout << "[OK] Chat messages successfully persisted and retrieved from SQLite!\n";
                break;
            }
        }
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
