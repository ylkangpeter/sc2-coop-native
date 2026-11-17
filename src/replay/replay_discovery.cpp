#include "replay_discovery.h"
#include "../utils/win32_utils.h"
#include <algorithm>
#include <cwctype>

namespace sc2 {
namespace {
int64_t FileTime(const FILETIME& ft) {
    return (static_cast<int64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

bool IsReplay(const std::filesystem::path& path) {
    auto ext = path.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), std::towlower);
    return ext == L".sc2replay";
}

void Collect(const std::filesystem::path& path, const WIN32_FIND_DATAW& data,
             int64_t since, std::vector<ReplayScanEntry>& entries) {
    if (!IsReplay(path) || std::max(FileTime(data.ftLastWriteTime), FileTime(data.ftCreationTime)) < since) return;
    entries.push_back({Win32Utils::WideToUtf8(path.wstring()),
        {FileTime(data.ftLastWriteTime),
         (static_cast<int64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow, {}, {}}});
}
} // namespace

std::mutex& ReplayScanMutex() {
    static std::mutex mutex;
    return mutex;
}

std::string ReplayScanRoot(const std::wstring& path) {
    auto root = std::filesystem::absolute(path).lexically_normal().make_preferred().wstring();
    while (root.size() > 3 && (root.back() == L'\\' || root.back() == L'/')) root.pop_back();
    std::transform(root.begin(), root.end(), root.begin(), std::towlower);
    return Win32Utils::WideToUtf8(root);
}

int64_t ReplayFileTimeNow() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return FileTime(ft);
}

int64_t ReplayEpochSeconds(int64_t file_time) {
    return file_time > 0 ? file_time / 10000000LL - 11644473600LL : 0;
}

ReplayDiscovery DiscoverReplayChanges(const std::wstring& path, int64_t since,
                                      const std::atomic<bool>& stop, bool stop_value) {
    const auto stopped = [&] { return stop.load() == stop_value; };
    ReplayDiscovery result;
    result.through = ReplayFileTimeNow(); // Capture before enumeration, never after.
    since = since > result.through ? 0 : std::max(since, int64_t{0});
    WIN32_FILE_ATTRIBUTE_DATA attrs{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attrs)) {
        result.complete = false;
        return result;
    }
    if (!(attrs.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        WIN32_FIND_DATAW data{};
        data.ftLastWriteTime = attrs.ftLastWriteTime;
        data.ftCreationTime = attrs.ftCreationTime;
        data.nFileSizeHigh = attrs.nFileSizeHigh;
        data.nFileSizeLow = attrs.nFileSizeLow;
        Collect(path, data, since, result.entries);
        return result;
    }

    std::vector<std::filesystem::path> dirs{path};
    while (!dirs.empty() && !stopped()) {
        auto dir = std::move(dirs.back());
        dirs.pop_back();
        WIN32_FIND_DATAW data{};
        HANDLE find = FindFirstFileExW((dir / L"*").c_str(), FindExInfoBasic, &data,
            FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (find == INVALID_HANDLE_VALUE) {
            if (GetLastError() != ERROR_FILE_NOT_FOUND) result.complete = false;
            continue;
        }
        struct FindGuard { HANDLE h; ~FindGuard() { FindClose(h); } } guard{find};
        do {
            if (stopped()) break;
            if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
            const auto child = dir / data.cFileName;
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                // Do not follow junctions into cycles or outside the configured tree.
                if (!(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) dirs.push_back(child);
            } else {
                Collect(child, data, since, result.entries);
            }
        } while (FindNextFileW(find, &data));
        if (!stopped() && GetLastError() != ERROR_NO_MORE_FILES) result.complete = false;
    }
    if (stopped()) result.complete = false;
    return result;
}

bool ReplayDirectoryNotifications::Arm() {
    ResetEvent(m_event);
    m_overlapped = {};
    m_overlapped.hEvent = m_event;
    return ReadDirectoryChangesW(m_directory, m_buffer.data(), static_cast<DWORD>(sizeof(m_buffer)), TRUE,
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE |
        FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION,
        nullptr, &m_overlapped, nullptr) != FALSE;
}

bool ReplayDirectoryNotifications::Open(const std::wstring& root) {
    Close();
    m_directory = CreateFileW(root.c_str(), FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (!IsOpen()) return false;
    m_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_event || !Arm()) { Close(); return false; }
    return true;
}

void ReplayDirectoryNotifications::Close() {
    if (IsOpen()) {
        CancelIoEx(m_directory, &m_overlapped);
        DWORD bytes;
        GetOverlappedResult(m_directory, &m_overlapped, &bytes, TRUE);
        CloseHandle(m_directory);
        m_directory = INVALID_HANDLE_VALUE;
    }
    if (m_event) { CloseHandle(m_event); m_event = nullptr; }
}

ReplayDirectoryNotifications::Changes ReplayDirectoryNotifications::Poll() {
    Changes changes;
    if (!IsOpen() || WaitForSingleObject(m_event, 0) != WAIT_OBJECT_0) return changes;
    DWORD bytes = 0;
    if (!GetOverlappedResult(m_directory, &m_overlapped, &bytes, FALSE)) {
        changes.rescan = true;
        Close();
        return changes;
    }
    if (!bytes) changes.rescan = true; // Overflow: recover via the database watermark.
    size_t offset = 0;
    while (offset + offsetof(FILE_NOTIFY_INFORMATION, FileName) <= bytes) {
        const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(
            reinterpret_cast<const char*>(m_buffer.data()) + offset);
        const size_t length = offsetof(FILE_NOTIFY_INFORMATION, FileName) + info->FileNameLength;
        if (offset + length > bytes || info->FileNameLength % sizeof(wchar_t)) {
            changes.rescan = true;
            break;
        }
        if (info->Action == FILE_ACTION_ADDED || info->Action == FILE_ACTION_MODIFIED ||
            info->Action == FILE_ACTION_RENAMED_NEW_NAME) {
            changes.paths.emplace_back(info->FileName, info->FileNameLength / sizeof(wchar_t));
        }
        if (!info->NextEntryOffset) break;
        if (info->NextEntryOffset < length || info->NextEntryOffset % sizeof(DWORD)) {
            changes.rescan = true;
            break;
        }
        offset += info->NextEntryOffset;
    }
    // Re-arm before inspecting files or parsing replays, so work cannot open a watch gap.
    if (!Arm()) { changes.rescan = true; Close(); }
    return changes;
}

} // namespace sc2
