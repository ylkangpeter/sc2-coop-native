#include "mpq_reader.h"
#include "../utils/win32_utils.h"
#include <windows.h>
#include <StormLib.h>
#include <iostream>

namespace sc2 {

static bool OpenArchive(const std::wstring& path, HANDLE* archive) {
    // SFileSetArchiveMarkers initializes StormLib's global crypto tables.
    // Zero marker values preserve its defaults. C++ serializes first use.
    static const bool initialized = [] {
        SFILE_MARKERS markers{};
        markers.dwSize = sizeof(markers);
        return SFileSetArchiveMarkers(&markers);
    }();
    return initialized && SFileOpenArchive(path.c_str(), 0, MPQ_OPEN_READ_ONLY, archive);
}

bool MpqReader::ReadFile(const std::wstring& archive_path,
                         const std::string& filename_in_archive,
                         std::vector<uint8_t>& out_bytes) {
    HANDLE hArchive = nullptr;
    if (!OpenArchive(archive_path, &hArchive)) {
        return false;
    }

    HANDLE hFile = nullptr;
    if (!SFileOpenFileEx(hArchive, filename_in_archive.c_str(), SFILE_OPEN_FROM_MPQ, &hFile)) {
        SFileCloseArchive(hArchive);
        return false;
    }

    struct HandleGuard {
        HANDLE hA;
        HANDLE hF;
        ~HandleGuard() {
            if (hF) SFileCloseFile(hF);
            if (hA) SFileCloseArchive(hA);
        }
    } guard{hArchive, hFile};

    DWORD fileSizeHigh = 0;
    DWORD fileSize = SFileGetFileSize(hFile, &fileSizeHigh);
    if (fileSizeHigh > 0 || fileSize == SFILE_INVALID_SIZE || fileSize == 0 || fileSize > 100 * 1024 * 1024) {
        return false;
    }

    try {
        out_bytes.resize(fileSize);
    } catch (...) {
        return false;
    }

    DWORD bytesRead = 0;
    BOOL ok = SFileReadFile(hFile, out_bytes.data(), fileSize, &bytesRead, nullptr);

    if (!ok || bytesRead == 0) {
        out_bytes.clear();
        return false;
    }

    out_bytes.resize(bytesRead);
    return true;
}

bool MpqReader::ReadFile(const std::string& archive_path_utf8,
                         const std::string& filename_in_archive,
                         std::vector<uint8_t>& out_bytes) {
    return ReadFile(Win32Utils::Utf8ToWide(archive_path_utf8), filename_in_archive, out_bytes);
}

bool MpqReader::ReadJson(const std::wstring& archive_path,
                         const std::string& filename_in_archive,
                         nlohmann::json& out_json) {
    std::vector<uint8_t> bytes;
    if (!ReadFile(archive_path, filename_in_archive, bytes)) {
        return false;
    }

    try {
        std::string json_str(bytes.begin(), bytes.end());
        out_json = nlohmann::json::parse(json_str);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::vector<std::string> MpqReader::ListFiles(const std::wstring& archive_path) {
    std::vector<std::string> files;
    HANDLE hArchive = nullptr;
    if (!OpenArchive(archive_path, &hArchive)) {
        return files;
    }

    SFILE_FIND_DATA findData;
    HANDLE hFind = SFileFindFirstFile(hArchive, "*", &findData, nullptr);
    if (hFind != nullptr) {
        do {
            if (findData.cFileName[0] != '\0') {
                files.push_back(findData.cFileName);
            }
        } while (SFileFindNextFile(hFind, &findData));
        SFileFindClose(hFind);
    }

    SFileCloseArchive(hArchive);
    return files;
}

} // namespace sc2
