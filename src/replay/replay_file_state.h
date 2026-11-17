#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace sc2 {

struct ReplayFileState {
    int64_t write_time = 0; // Exact Windows FILETIME, not rounded epoch seconds.
    int64_t file_size = 0;
    std::string sha256;
    std::string outcome; // parsed or failed

    bool SameFileVersion(const ReplayFileState& other) const {
        return write_time == other.write_time && file_size == other.file_size;
    }
};

struct ReplayScanEntry {
    std::string path;
    ReplayFileState state;
};

// Hashing requires a readable file with no active writer. A sharing violation
// is transient and must never become a persistent failed-content marker.
std::optional<ReplayFileState> ReadReplayFileState(const std::wstring& path, bool hash_content = false);

} // namespace sc2
