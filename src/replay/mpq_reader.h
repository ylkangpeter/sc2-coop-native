#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace sc2 {

class MpqReader {
public:
    // Read raw file contents from inside an MPQ archive (.SC2Replay)
    static bool ReadFile(const std::wstring& archive_path,
                         const std::string& filename_in_archive,
                         std::vector<uint8_t>& out_bytes);

    // Read and parse JSON file from MPQ (e.g. "replay.gamemetadata.json")
    static bool ReadJson(const std::wstring& archive_path,
                         const std::string& filename_in_archive,
                         nlohmann::json& out_json);

    // List all files in the MPQ archive
    static std::vector<std::string> ListFiles(const std::wstring& archive_path);
};

} // namespace sc2
