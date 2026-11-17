#include "replay_scanner.h"
#include "replay_parser.h"
#include "replay_discovery.h"
#include "replay_watcher.h"
#include "../app_state.h"
#include "../db/database.h"
#include "../utils/win32_utils.h"
#include "../utils/diagnostics.h"

#include <filesystem>
#include <chrono>
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
    std::unique_lock<std::mutex> scan_lock(ReplayScanMutex());
    auto& app = AppState::Instance();
    auto& db = Database::Instance();
    auto set_status = [&](const std::string& message) {
        std::lock_guard<std::mutex> lock(app.scan_status_mutex);
        app.scan_status_message = message;
    };
    try {
        const auto folder = app.GetAccountFolder();
        if (!folder.empty()) {
            const auto root_w = Win32Utils::Utf8ToWide(folder);
            const auto root = ReplayScanRoot(root_w);
            if (force_reparse && (!db.ClearReplayFileStates() || !db.ResetReplayScan(root))) {
                throw std::runtime_error("Cannot reset replay scan state");
            }
            set_status("Finding changed replay files...");
            const auto since = db.GetReplayScanWatermark(root);
            auto found = DiscoverReplayChanges(root_w, since, m_stop_flag);
            if (!m_stop_flag.load()) {
                if (!db.QueueReplayChanges(root, found.complete ? found.through : since, found.entries)) {
                    throw std::runtime_error("Cannot save replay discovery progress");
                }
                const auto pending = db.GetPendingReplayChanges(root);
                app.scan_total = static_cast<int>(pending.size());
                for (const auto& item : pending) {
                    if (m_stop_flag.load()) break;
                    ++app.scan_progress;
                    // Let the live watcher drain notifications and prioritize a
                    // just-finished game during a long explicit library scan.
                    if (app.scan_progress.load() % 8 == 0) {
                        scan_lock.unlock();
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        scan_lock.lock();
                    }
                    const auto wide = Win32Utils::Utf8ToWide(item.path);
                    const auto observed = ReadReplayFileState(wide);
                    if (!observed) continue; // Retain durable work for the live watcher.
                    const auto saved = db.GetReplayFileState(item.path);
                    const int64_t age = ReplayEpochSeconds(ReplayFileTimeNow()) - ReplayEpochSeconds(observed->write_time);
                    // Leave recent results queued for the watcher to deliver their
                    // first popup, even if the startup scanner wins the parse race.
                    const bool recent = age >= -60 && age <= 300;
                    auto complete = [&](const ReplayFileState& state) {
                        if (db.SaveReplayFileState(item.path, state) && (!recent || state.outcome == "failed")) {
                            db.CompleteReplayChange(root, item);
                        }
                    };
                    if (saved && observed->SameFileVersion(*saved)) {
                        if (saved->outcome == "failed" || (!recent && db.HasReplay(item.path))) {
                            db.CompleteReplayChange(root, item);
                        }
                        continue;
                    }
                    auto fingerprint = ReadReplayFileState(wide, true);
                    if (!fingerprint) continue;
                    if (db.HasFailedReplayHash(fingerprint->sha256)) {
                        fingerprint->outcome = "failed";
                        complete(*fingerprint);
                        continue;
                    }
                    std::optional<ReplayRecord> parsed;
                    try { parsed = ReplayParser::ParseFile(wide); } catch (...) { parsed = std::nullopt; }
                    const auto after = ReadReplayFileState(wide);
                    if (!after || !after->SameFileVersion(*fingerprint)) continue;
                    if (parsed && parsed->result != "Unknown") {
                        if (db.SaveReplay(*parsed)) {
                            fingerprint->outcome = "parsed";
                            complete(*fingerprint);
                        }
                    } else if (!recent || force_reparse) {
                        fingerprint->outcome = "failed";
                        complete(*fingerprint);
                    } // Recent partial files stay queued for bounded watcher retries.
                    set_status("Processed " + std::to_string(app.scan_progress.load()) + " / " +
                        std::to_string(app.scan_total.load()) + " changed replays");
                }
                set_status(found.complete ? "Incremental scan complete" : "Scan incomplete; pending files will be retried");
            }
        }
        if (auto_repair_after && !m_stop_flag.load()) RunRepairInternal();
    } catch (const std::exception& e) {
        set_status(std::string("Scan error: ") + e.what());
        LogDiagnostic("scanner", e.what());
    }
    ReplayWatcher::Instance().CheckPendingChanges();
    m_is_scanning = false;
    app.is_scanning = false;
}

void ReplayScanner::RunRepairInternal() {
    try {
        Database::Instance().RepairWeeklyMutationsIfNeeded();

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
