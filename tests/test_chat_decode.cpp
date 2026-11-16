#include <iostream>
#include <vector>
#include <cstdint>
#include <cassert>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif
#include "../src/replay/mpq_reader.h"
#include "../src/replay/s2_decoder.h"
#include "../src/utils/win32_utils.h"

int main(int argc, char** argv) {
    std::wstring path;

    // 1. Check command-line argument (support wide/UTF-8 path on Windows via CommandLineToArgvW)
#ifdef _WIN32
    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (wargv) {
        if (wargc > 1) {
            std::filesystem::path p(wargv[1]);
            if (std::filesystem::exists(p)) {
                path = p.wstring();
            }
        }
        LocalFree(wargv);
    }
#else
    if (argc > 1) {
        std::filesystem::path p(sc2::Win32Utils::Utf8ToWide(argv[1]));
        if (std::filesystem::exists(p)) {
            path = p.wstring();
        }
    }
#endif

    // 2. Check SC2 Accounts folder
    if (path.empty()) {
        std::string acc_folder = sc2::Win32Utils::FindDefaultAccountsFolder();
        if (!acc_folder.empty() && std::filesystem::exists(sc2::Win32Utils::Utf8ToWide(acc_folder))) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(sc2::Win32Utils::Utf8ToWide(acc_folder))) {
                if (entry.is_regular_file()) {
                    auto ext = entry.path().extension().wstring();
                    for (auto& c : ext) c = towlower(c);
                    if (ext == L".sc2replay") {
                        path = entry.path().wstring();
                        break;
                    }
                }
            }
        }
    }

    // 3. Check fixture sample replay (tests/fixtures/sample.SC2Replay or tests/fixtures/)
    if (path.empty()) {
        const std::vector<std::filesystem::path> fixture_paths = {
            "tests/fixtures/sample.SC2Replay",
            "../tests/fixtures/sample.SC2Replay",
            "../../tests/fixtures/sample.SC2Replay"
        };
        for (const auto& fp : fixture_paths) {
            if (std::filesystem::exists(fp)) {
                path = fp.wstring();
                break;
            }
        }
        if (path.empty()) {
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
                                path = entry.path().wstring();
                                break;
                            }
                        }
                    }
                }
                if (!path.empty()) break;
            }
        }
    }

    // 4. Fail if no replay found anywhere
    if (path.empty()) {
        std::cout << "[FAIL] No SC2 replay file found to test parser!" << std::endl;
        return 1;
    }

    std::vector<uint8_t> msg_bytes;
    if (!sc2::MpqReader::ReadFile(path, "replay.message.events", msg_bytes)) {
        std::cout << "[SKIP] Selected replay has no replay.message.events. Test skipped.\n";
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
