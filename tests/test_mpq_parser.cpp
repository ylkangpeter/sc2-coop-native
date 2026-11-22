#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif
#include "../src/replay/mpq_reader.h"
#include "../src/replay/s2_decoder.h"
#include "../src/replay/replay_parser.h"
#include "../src/utils/win32_utils.h"

int main(int argc, char** argv) {
    std::cout << "[TEST] Running MPQ and Replay Parser test..." << std::endl;

    std::wstring sample_path;

    // 1. Check command-line argument (support wide/UTF-8 path on Windows via CommandLineToArgvW)
#ifdef _WIN32
    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (wargv) {
        if (wargc > 1) {
            std::filesystem::path p(wargv[1]);
            if (std::filesystem::exists(p)) {
                sample_path = p.wstring();
            }
        }
        LocalFree(wargv);
    }
#else
    if (argc > 1) {
        std::filesystem::path p(sc2::Win32Utils::Utf8ToWide(argv[1]));
        if (std::filesystem::exists(p)) {
            sample_path = p.wstring();
        }
    }
#endif

    // 2. Check SC2 Accounts folder
    if (sample_path.empty()) {
        std::string acc_folder = sc2::Win32Utils::FindDefaultAccountsFolder();
        if (!acc_folder.empty() && std::filesystem::exists(sc2::Win32Utils::Utf8ToWide(acc_folder))) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(sc2::Win32Utils::Utf8ToWide(acc_folder))) {
                if (entry.is_regular_file()) {
                    auto ext = entry.path().extension().wstring();
                    for (auto& c : ext) c = towlower(c);
                    if (ext == L".sc2replay") {
                        sample_path = entry.path().wstring();
                        break;
                    }
                }
            }
        }
    }

    // 3. Check fixture sample replay (tests/fixtures/sample.SC2Replay or tests/fixtures/)
    if (sample_path.empty()) {
        const std::vector<std::filesystem::path> fixture_paths = {
            "tests/fixtures/sample.SC2Replay",
            "../tests/fixtures/sample.SC2Replay",
            "../../tests/fixtures/sample.SC2Replay"
        };
        for (const auto& fp : fixture_paths) {
            if (std::filesystem::exists(fp)) {
                sample_path = fp.wstring();
                break;
            }
        }
        if (sample_path.empty()) {
            const std::vector<std::filesystem::path> fixture_dirs = {
                "tests/fixtures",
                "../tests/fixtures",
                "../../tests/fixtures"
            };
            for (const auto& fd : fixture_dirs) {
                if (std::filesystem::exists(fd) && std::filesystem::is_directory(fd)) {
                    for (const auto& entry : std::filesystem::directory_iterator(fd)) {
                        if (entry.is_regular_file()) {
                            auto ext = entry.path().extension().wstring();
                            for (auto& c : ext) c = towlower(c);
                            if (ext == L".sc2replay") {
                                sample_path = entry.path().wstring();
                                break;
                            }
                        }
                    }
                }
                if (!sample_path.empty()) break;
            }
        }
    }

    // 4. Fail if no replay found anywhere
    if (sample_path.empty()) {
        std::cout << "[FAIL] No SC2 replay file found to test parser!" << std::endl;
        return 1;
    }

    auto rec_opt = sc2::ReplayParser::ParseFile(sample_path);
    if (!rec_opt) {
        std::cerr << "Failed to parse replay!" << std::endl;
        return 1;
    }

    const auto& rec = *rec_opt;
    std::cout << "\n=== PARSED REPLAY SUMMARY ===" << std::endl;
    std::cout << "Map: " << rec.map_name << " (Raw: " << rec.raw_map_name << ")" << std::endl;
    std::cout << "Date: " << rec.game_date << std::endl;
    std::cout << "Duration: " << rec.form_length << std::endl;
    std::cout << "Result: " << rec.result << std::endl;
    std::cout << "Difficulty: " << rec.difficulty << std::endl;
    std::cout << "Player 1: " << rec.p1.name << " (" << rec.p1.commander << " " << rec.p1.FormatLevel() << " P" << rec.p1.prestige << ") APM: " << rec.p1.apm << std::endl;
    std::cout << "Player 2: " << rec.p2.name << " (" << rec.p2.commander << " " << rec.p2.FormatLevel() << " P" << rec.p2.prestige << ") APM: " << rec.p2.apm << std::endl;
    std::cout << "Chat Messages: " << rec.chat.size() << std::endl;
    for (const auto& c : rec.chat) {
        std::cout << "  [" << c.time_seconds << "s] " << c.player_name << ": " << c.text << std::endl;
    }

    return 0;
}
