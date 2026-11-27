#pragma once

#include "replay_parser.h"
#include "../db/database.h"
#include "../utils/win32_utils.h"
#include <utility>

namespace sc2 {

// Load only the level fields needed by the history list, leaving its row data intact.
inline void LoadHistoryLevels(ReplayRecord& record, Database& db) {
    if (record.file_path.empty()) return;
    ReplayRecord details = record;
    int64_t row_id = 0;
    const bool loaded = db.LoadReplayDetails(details, &row_id);
    if (!details.has_level_data && ReplayParser::RefreshCommanderLevels(details) && loaded && row_id) {
        db.UpdateReplayDetails(details, row_id);
    }
    record.p1.commander_level = details.p1.commander_level;
    record.p1.commander_mastery_level = details.p1.commander_mastery_level;
    record.p2.commander_level = details.p2.commander_level;
    record.p2.commander_mastery_level = details.p2.commander_mastery_level;
    record.has_level_data = details.has_level_data;
}

// Resolve details for an entry from the history list, refreshing stale caches.
inline void LoadSummaryDetails(ReplayRecord& record, Database& db) {
    if (record.file_path.empty()) return;
    int64_t row_id = 0;
    const bool loaded = db.LoadReplayDetails(record, &row_id);
    if (!loaded || !record.has_summary_data) {
        auto full = ReplayParser::ParseFile(Win32Utils::Utf8ToWide(record.file_path));
        if (full && full->file_mtime == record.file_mtime && (full->has_summary_data || !loaded)) {
            record = std::move(*full);
            if (row_id && record.has_summary_data) db.UpdateReplayDetails(record, row_id);
        }
    } else if (!record.has_level_data && ReplayParser::RefreshCommanderLevels(record)) {
        db.UpdateReplayDetails(record, row_id);
    }
}

} // namespace sc2
