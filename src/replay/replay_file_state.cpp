#include "replay_file_state.h"
#include <windows.h>
#include <bcrypt.h>
#include <array>

namespace sc2 {
namespace {
int64_t ToInt64(DWORD high, DWORD low) {
    return (static_cast<int64_t>(high) << 32) | low;
}
}

std::optional<ReplayFileState> ReadReplayFileState(const std::wstring& path, bool hash_content) {
    WIN32_FILE_ATTRIBUTE_DATA attrs{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attrs)) return std::nullopt;
    ReplayFileState state;
    state.write_time = ToInt64(attrs.ftLastWriteTime.dwHighDateTime, attrs.ftLastWriteTime.dwLowDateTime);
    state.file_size = ToInt64(attrs.nFileSizeHigh, attrs.nFileSizeLow);
    if (!hash_content) return state;

    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return std::nullopt;
    struct FileGuard { HANDLE h; ~FileGuard() { CloseHandle(h); } } file_guard{file};
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file, &info)) return std::nullopt;
    state.write_time = ToInt64(info.ftLastWriteTime.dwHighDateTime, info.ftLastWriteTime.dwLowDateTime);
    state.file_size = ToInt64(info.nFileSizeHigh, info.nFileSizeLow);

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return std::nullopt;
    struct AlgorithmGuard {
        BCRYPT_ALG_HANDLE h;
        ~AlgorithmGuard() { BCryptCloseAlgorithmProvider(h, 0); }
    } algorithm_guard{algorithm};
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) return std::nullopt;
    struct HashGuard { BCRYPT_HASH_HANDLE h; ~HashGuard() { BCryptDestroyHash(h); } } hash_guard{hash};
    std::array<unsigned char, 64 * 1024> buffer;
    int64_t total = 0;
    for (;;) {
        DWORD count = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr)) return std::nullopt;
        if (!count) break;
        if (BCryptHashData(hash, buffer.data(), count, 0) < 0) return std::nullopt;
        total += count;
    }
    if (total != state.file_size) return std::nullopt;
    std::array<unsigned char, 32> digest;
    if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) return std::nullopt;
    constexpr char hex[] = "0123456789abcdef";
    for (auto byte : digest) {
        state.sha256 += hex[byte >> 4];
        state.sha256 += hex[byte & 15];
    }
    return state;
}

} // namespace sc2
