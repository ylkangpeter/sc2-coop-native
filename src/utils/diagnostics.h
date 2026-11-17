#pragma once

#include "../app_state.h"
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <string>

namespace sc2 {

// Local, bounded diagnostics for the background replay -> UI notification path.
inline void LogDiagnostic(const char* component, const std::string& message) {
    if (!AppState::Instance().enable_logging) return;
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    std::error_code ec;
    auto size = std::filesystem::file_size("overlay_diagnostics.log", ec);
    std::ofstream log("overlay_diagnostics.log",
        !ec && size > 1024 * 1024 ? std::ios::trunc : std::ios::app);
    const auto now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    log << std::put_time(&local, "%Y-%m-%d %H:%M:%S")
        << " [" << component << "] " << message << '\n';
}

} // namespace sc2
