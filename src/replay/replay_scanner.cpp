#include "replay_scanner.h"
#include "replay_parser.h"
#include "../app_state.h"
#include "../db/database.h"
#include "../utils/win32_utils.h"

#include <filesystem>
#include <iostream>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <sys/stat.h>

namespace sc2 {

ReplayScanner& ReplayScanner::Instance() {
    static ReplayScanner instance;
    return instance;
}

ReplayScanner::~ReplayScanner() {
    Stop();
}

void ReplayScanner::Stop() {
    m_stop_flag = true;
    if (m_worker_thread.joinable()) {
        m_worker_thread.join();
    }
    m_is_scanning = false;
    AppState::Instance().is_scanning = false;
}

void ReplayScanner::StartScan(bool force_reparse, bool auto_repair_after) {
    std::lock_guard<std::mutex> lock(m_start_stop_mutex);
    if (m_is_scanning.load()) {
        return;
    }
    Stop();

    m_stop_flag = false;
    m_is_scanning = true;
    auto& app = AppState::Instance();
    app.is_scanning = true;
    app.scan_progress = 0;
    app.scan_total = 0;

    m_worker_thread = std::thread([this, force_reparse, auto_repair_after]() {
        RunScan(force_reparse, auto_repair_after);
    });
}

void ReplayScanner::StartRepair() {
    std::lock_guard<std::mutex> lock(m_start_stop_mutex);
    if (m_is_scanning.load()) {
        return;
    }
    Stop();

    m_stop_flag = false;
    m_is_scanning = true;
    AppState::Instance().is_scanning = true;

    m_worker_thread = std::thread([this]() {
        RunRepairInternal();
        m_is_scanning = false;
        AppState::Instance().is_scanning = false;
    });
}

void ReplayScanner::RunScan(bool force_reparse, bool auto_repair_after) {
    auto& app = AppState::Instance();
    std::wstring root_w = Win32Utils::Utf8ToWide(app.GetAccountFolder());

    std::error_code ec;
    if (root_w.empty() || !std::filesystem::exists(root_w, ec) || ec) {
        m_is_scanning = false;
        app.is_scanning = false;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(app.scan_status_mutex);
        app.scan_status_message = "Finding replay files...";
    }

    std::vector<std::filesystem::path> replay_paths;
    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 root_w, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (ec || m_stop_flag.load()) break;
            std::error_code entry_ec;
            if (entry.is_regular_file(entry_ec) && !entry_ec) {
                auto ext = entry.path().extension().string();
                for (auto& c : ext) c = static_cast<char>(tolower(c));
                if (ext == ".sc2replay") {
                    replay_paths.push_back(entry.path());
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Directory scan exception: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Directory scan unknown exception" << std::endl;
    }

    if (m_stop_flag.load()) {
        m_is_scanning = false;
        app.is_scanning = false;
        return;
    }

    // Identify which replays actually need parsing (file_path + file_mtime check)
    std::vector<std::filesystem::path> to_parse;
    if (force_reparse) {
        to_parse = std::move(replay_paths);
    } else {
        auto valid_mtimes = Database::Instance().GetValidReplayMtimes();
        for (auto& p : replay_paths) {
            if (m_stop_flag.load()) break;
            std::string u8path = Win32Utils::WideToUtf8(p.wstring());
            auto it = valid_mtimes.find(u8path);
            if (it == valid_mtimes.end()) {
                to_parse.push_back(std::move(p));
            } else {
                struct _stat64 st;
                if (_wstat64(p.c_str(), &st) == 0) {
                    if (st.st_mtime > it->second) {
                        to_parse.push_back(std::move(p));
                    }
                }
            }
        }
    }

    if (m_stop_flag.load()) {
        m_is_scanning = false;
        app.is_scanning = false;
        return;
    }

    if (to_parse.empty()) {
        {
            std::lock_guard<std::mutex> lock(app.scan_status_mutex);
            app.scan_status_message = "All replays up to date (" + std::to_string(replay_paths.size()) + " total)";
        }
    } else {
        app.scan_total = static_cast<int>(to_parse.size());
        app.scan_progress = 0;
        int processed = 0;
        std::vector<ReplayRecord> batch;

        for (const auto& p : to_parse) {
            if (m_stop_flag.load()) break;

            try {
                auto parsed = ReplayParser::ParseFile(p.wstring());
                if (parsed.has_value() && parsed->result != "Unknown") {
                    batch.push_back(parsed.value());
                    if (batch.size() >= 30) {
                        Database::Instance().BatchSaveReplays(batch);
                        batch.clear();
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "Parse exception for " << p.string() << ": " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "Unknown parse exception for " << p.string() << std::endl;
            }

            processed++;
            app.scan_progress = processed;

            if (processed % 10 == 0 || processed == app.scan_total.load()) {
                std::lock_guard<std::mutex> lock(app.scan_status_mutex);
                app.scan_status_message = "Parsed " + std::to_string(processed) + " / " +
                                          std::to_string(app.scan_total.load()) + " replays";
            }
        }

        if (!batch.empty() && !m_stop_flag.load()) {
            Database::Instance().BatchSaveReplays(batch);
            batch.clear();
        }

        if (!m_stop_flag.load()) {
            std::lock_guard<std::mutex> lock(app.scan_status_mutex);
            app.scan_status_message = "Scan complete (" + std::to_string(processed) + " replays parsed)";
        }
    }

    // Sequence auto-repair after scan so they don't run concurrently
    if (auto_repair_after && !m_stop_flag.load()) {
        RunRepairInternal();
    }

    m_is_scanning = false;
    app.is_scanning = false;
}

void ReplayScanner::RunRepairInternal() {
    try {
        auto corrupted = Database::Instance().GetCorruptedReplayPaths();
        if (corrupted.empty() || m_stop_flag.load()) return;

        const auto difficulty_paths = Database::Instance().GetDifficultyRepairPaths();
        std::unordered_set<std::string> failed_difficulty_paths;
        std::vector<ReplayRecord> batch;

        for (const auto& u8path : corrupted) {
            if (m_stop_flag.load()) break;
            const bool is_difficulty_candidate = difficulty_paths.find(u8path) != difficulty_paths.end();
            std::wstring wpath = Win32Utils::Utf8ToWide(u8path);
            std::error_code ec;
            if (std::filesystem::exists(wpath, ec) && !ec) {
                try {
                    auto parsed = ReplayParser::ParseFile(wpath);
                    if (parsed.has_value() && parsed->result != "Unknown") {
                        batch.push_back(parsed.value());
                        if (batch.size() >= 30) {
                            if (!Database::Instance().BatchSaveReplays(batch)) {
                                for (const auto& replay : batch) {
                                    if (difficulty_paths.find(replay.file_path) != difficulty_paths.end()) {
                                        failed_difficulty_paths.insert(replay.file_path);
                                    }
                                }
                            }
                            batch.clear();
                        }
                    } else if (is_difficulty_candidate) {
                        failed_difficulty_paths.insert(u8path);
                    }
                } catch (...) {
                    if (is_difficulty_candidate) {
                        failed_difficulty_paths.insert(u8path);
                    }
                }
            } else if (is_difficulty_candidate) {
                failed_difficulty_paths.insert(u8path);
            }
        }

        if (!batch.empty() && !m_stop_flag.load()) {
            if (!Database::Instance().BatchSaveReplays(batch)) {
                for (const auto& replay : batch) {
                    if (difficulty_paths.find(replay.file_path) != difficulty_paths.end()) {
                        failed_difficulty_paths.insert(replay.file_path);
                    }
                }
            }
            batch.clear();
        }

        if (!m_stop_flag.load()) {
            Database::Instance().SanitizeInvalidPrestiges();

            if (failed_difficulty_paths.empty()) {
                Database::Instance().MarkDifficultyFixComplete();
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "RunRepairInternal exception: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "RunRepairInternal unknown exception" << std::endl;
    }
}

} // namespace sc2
