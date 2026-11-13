#include "mpq_reader.h"
#include <windows.h>
#include <StormLib.h>
#include <iostream>

namespace sc2 {

bool MpqReader::ReadFile(const std::wstring& archive_path,
                         const std::string& filename_in_archive,
                         std::vector<uint8_t>& out_bytes) {
    HANDLE hArchive = nullptr;
    if (!SFileOpenArchive(archive_path.c_str(), 0, MPQ_OPEN_READ_ONLY, &hArchive)) {
        return false;
    }

    HANDLE hFile = nullptr;
    if (!SFileOpenFileEx(hArchive, filename_in_archive.c_str(), SFILE_OPEN_FROM_MPQ, &hFile)) {
        SFileCloseArchive(hArchive);
        return false;
    }

    DWORD fileSizeHigh = 0;
    DWORD fileSize = SFileGetFileSize(hFile, &fileSizeHigh);
    if (fileSize == SFILE_INVALID_SIZE || fileSize == 0) {
        SFileCloseFile(hFile);
        SFileCloseArchive(hArchive);
        return false;
    }

    out_bytes.resize(fileSize);
    DWORD bytesRead = 0;
    BOOL ok = SFileReadFile(hFile, out_bytes.data(), fileSize, &bytesRead, nullptr);

    SFileCloseFile(hFile);
    SFileCloseArchive(hArchive);

    if (!ok || bytesRead == 0) {
        out_bytes.clear();
        return false;
    }

    out_bytes.resize(bytesRead);
    return true;
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
    if (!SFileOpenArchive(archive_path.c_str(), 0, MPQ_OPEN_READ_ONLY, &hArchive)) {
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
