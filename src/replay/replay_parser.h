#pragma once

#include <string>
#include <optional>
#include "replay_types.h"

namespace sc2 {

class ReplayParser {
public:
    // Parse single .SC2Replay file
    static std::optional<ReplayRecord> ParseFile(const std::wstring& replay_path);

    // Format seconds into "MM:SS" or "HH:MM:SS"
    static std::string FormatDuration(int seconds);

    // Format file timestamp to readable string "YYYY-MM-DD HH:MM"
    static std::string FormatTimestamp(int64_t mtime);
};

} // namespace sc2
