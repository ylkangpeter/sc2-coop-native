#include <iostream>
#include <vector>
#include <cstdint>
#include <cassert>
#include <filesystem>
#include "../src/replay/mpq_reader.h"
#include "../src/replay/s2_decoder.h"
#include "../src/utils/win32_utils.h"

int main(int argc, char** argv) {
    std::wstring path;
    if (argc > 1) {
        path = sc2::Win32Utils::Utf8ToWide(argv[1]);
    } else {
        std::string acc_folder = sc2::Win32Utils::FindDefaultAccountsFolder();
        if (!acc_folder.empty() && std::filesystem::exists(sc2::Win32Utils::Utf8ToWide(acc_folder))) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(sc2::Win32Utils::Utf8ToWide(acc_folder))) {
                if (entry.is_regular_file() && entry.path().extension() == L".SC2Replay") {
                    path = entry.path().wstring();
                    break;
                }
            }
        }
    }

    if (path.empty()) {
        std::cout << "[SKIP] No sample SC2Replay found on this system.\n";
        return 0;
    }

    std::vector<uint8_t> msg_bytes;
    if (!sc2::MpqReader::ReadFile(path, "replay.message.events", msg_bytes)) {
        std::cout << "Note: Selected replay has no replay.message.events\n";
        return 0;
    }

    std::cout << "Read " << msg_bytes.size() << " bytes\n";
    auto msgs = sc2::S2Decoder::DecodeMessageEvents(msg_bytes);
    std::cout << "Decoded " << msgs.size() << " chat messages:\n";
    for (const auto& m : msgs) {
        std::cout << "  [" << m.gameloop << "] Player " << m.player_id << ": " << m.text << "\n";
    }

    std::cout << "CHAT DECODE TEST FINISHED SUCCESSFULLY!\n";
    return 0;
}
