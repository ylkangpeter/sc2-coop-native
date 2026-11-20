#pragma once

#include <string>
#include <optional>
#include "replay_types.h"

namespace sc2 {

enum class ReplayParseDepth { Summary, Full };

class ReplayParser {
public:
    // Parse single .SC2Replay file
    static std::optional<ReplayRecord> ParseFile(const std::wstring& replay_path,
                                                 ReplayParseDepth depth = ReplayParseDepth::Full);

    // Format seconds into "MM:SS" or "HH:MM:SS"
    static std::string FormatDuration(int seconds);

    // Format file timestamp to readable string "YYYY-MM-DD HH:MM"
    static std::string FormatTimestamp(int64_t mtime);
};

} // namespace sc2
