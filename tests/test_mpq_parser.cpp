#include <iostream>
#include <cassert>
#include <string>
#include <filesystem>
#include "../src/replay/mpq_reader.h"
#include "../src/replay/s2_decoder.h"
#include "../src/replay/replay_parser.h"
#include "../src/utils/win32_utils.h"

int main(int argc, char** argv) {
    std::cout << "[TEST] Running MPQ and Replay Parser test..." << std::endl;

    std::wstring sample_path;
    if (argc > 1) {
        sample_path = sc2::Win32Utils::Utf8ToWide(argv[1]);
    } else {
        std::string acc_folder = sc2::Win32Utils::FindDefaultAccountsFolder();
        if (!acc_folder.empty() && std::filesystem::exists(sc2::Win32Utils::Utf8ToWide(acc_folder))) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(sc2::Win32Utils::Utf8ToWide(acc_folder))) {
                if (entry.is_regular_file() && entry.path().extension() == L".SC2Replay") {
                    sample_path = entry.path().wstring();
                    break;
                }
            }
        }
    }

    if (sample_path.empty()) {
        std::cout << "[SKIP] No sample replay found on this machine.\n";
        return 0;
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
    std::cout << "Player 1: " << rec.p1.name << " (" << rec.p1.commander << " P" << rec.p1.prestige << ") APM: " << rec.p1.apm << std::endl;
    std::cout << "Player 2: " << rec.p2.name << " (" << rec.p2.commander << " P" << rec.p2.prestige << ") APM: " << rec.p2.apm << std::endl;
    std::cout << "Chat Messages: " << rec.chat.size() << std::endl;
    for (const auto& c : rec.chat) {
        std::cout << "  [" << c.time_seconds << "s] " << c.player_name << ": " << c.text << std::endl;
    }

    return 0;
}
