#include <windows.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include "app_state.h"
#include "ui/ui_manager.h"
#include "db/database.h"
#include "utils/win32_utils.h"
#include <backends/imgui_impl_win32.h>

static const char* FileNameOnly(const char* path) {
    if (!path) return "UnknownModule";
    const char* slash = strrchr(path, '\\');
    const char* forward_slash = strrchr(path, '/');
    if (forward_slash && (!slash || forward_slash > slash)) slash = forward_slash;
    return slash ? slash + 1 : path;
}

static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* pException) {
    HANDLE hFile = CreateFileA("crash_log.txt", FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        char log_buf[2048];
        DWORD bytesWritten = 0;

        HMODULE hMod = nullptr;
        char modName[MAX_PATH] = "UnknownModule";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)pException->ExceptionRecord->ExceptionAddress, &hMod) && hMod) {
            GetModuleFileNameA(hMod, modName, MAX_PATH);
        }
        const char* modFileName = FileNameOnly(modName);
        uintptr_t offset = (uintptr_t)pException->ExceptionRecord->ExceptionAddress - (uintptr_t)hMod;

        int len = snprintf(log_buf, sizeof(log_buf),
            "\n================ CRASH REPORT ================\n"
            "Timestamp: %04d-%02d-%02d %02d:%02d:%02d.%03d\n"
            "Unhandled Exception: 0x%08X\n"
            "Fault Address: 0x%p\n"
            "Fault Module: %s (+0x%zx)\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            (unsigned int)pException->ExceptionRecord->ExceptionCode,
            pException->ExceptionRecord->ExceptionAddress,
            modFileName, offset);
        if (len > 0) WriteFile(hFile, log_buf, len, &bytesWritten, nullptr);

        if (pException->ExceptionRecord->ExceptionCode == 0xC0000005 && pException->ExceptionRecord->NumberParameters >= 2) {
            ULONG_PTR op = pException->ExceptionRecord->ExceptionInformation[0];
            ULONG_PTR targetAddr = pException->ExceptionRecord->ExceptionInformation[1];
            const char* opStr = (op == 0) ? "READ" : ((op == 1) ? "WRITE" : "EXECUTE");
            len = snprintf(log_buf, sizeof(log_buf), "Access Violation: %s at address 0x%zx\n", opStr, targetAddr);
            if (len > 0) WriteFile(hFile, log_buf, len, &bytesWritten, nullptr);
        }

        const char* traceHdr = "Stack Backtrace:\n";
        WriteFile(hFile, traceHdr, (DWORD)strlen(traceHdr), &bytesWritten, nullptr);

        void* stack[32];
        USHORT frames = CaptureStackBackTrace(0, 32, stack, nullptr);
        for (USHORT i = 0; i < frames; ++i) {
            HMODULE frameMod = nullptr;
            char frameModName[MAX_PATH] = "Unknown";
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   (LPCSTR)stack[i], &frameMod) && frameMod) {
                GetModuleFileNameA(frameMod, frameModName, MAX_PATH);
            }
            const char* frameFileName = FileNameOnly(frameModName);
            uintptr_t frameOffset = (uintptr_t)stack[i] - (uintptr_t)frameMod;
            len = snprintf(log_buf, sizeof(log_buf), "  #%u %p (%s + 0x%zx)\n", (unsigned int)i, stack[i], frameFileName, frameOffset);
            if (len > 0) WriteFile(hFile, log_buf, len, &bytesWritten, nullptr);
        }

        const char* sep = "==============================================\n\n";
        WriteFile(hFile, sep, (DWORD)strlen(sep), &bytesWritten, nullptr);
        CloseHandle(hFile);
    }

    wchar_t buf[256];
    _snwprintf_s(buf, _TRUNCATE, L"Fatal Crash: Exception code 0x%08X at 0x%p.\nDetailed diagnostic backtrace written to crash_log.txt.",
                 (unsigned int)pException->ExceptionRecord->ExceptionCode, pException->ExceptionRecord->ExceptionAddress);
    MessageBoxW(nullptr, buf, L"SC2 Co-op Native Error", MB_ICONERROR | MB_OK);
    return EXCEPTION_EXECUTE_HANDLER;
}

static void CleanAppShutdown() {
    if (!sc2::Database::Instance().IsOpen()) return;
    static std::atomic<bool> s_cleaned{false};
    bool expected = false;
    if (!s_cleaned.compare_exchange_strong(expected, true)) {
        return;
    }
    try {
        sc2::UIManager::Instance().Shutdown();
    } catch (...) {}
    try {
        sc2::AppState::Instance().StopScan();
    } catch (...) {}
    try {
        sc2::AppState::Instance().SaveSettings();
    } catch (...) {}
    try {
        sc2::Database::Instance().Close();
    } catch (...) {}
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    SetUnhandledExceptionFilter(CrashHandler);

    // Ensure working directory is always the executable's directory
    wchar_t exePath[MAX_PATH] = { 0 };
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
        std::filesystem::path p(exePath);
        SetCurrentDirectoryW(p.parent_path().c_str());
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Enable modern Per-Monitor V2 DPI Awareness for crisp High-DPI scaling
    ImGui_ImplWin32_EnableDpiAwareness();

    // Single instance check. A named mutex is released by the OS even if the
    // process crashes, so it cannot become stale like a lock file.
    SetLastError(ERROR_SUCCESS);
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Local\\SC2CoopNativeSingleInstanceMutex");
    const DWORD mutex_error = GetLastError();
    if (!hMutex) {
        MessageBoxW(nullptr, L"Failed to create the single-instance mutex.",
                    L"SC2 Co-op Native Error", MB_ICONERROR | MB_OK);
        CoUninitialize();
        return 1;
    }

    if (mutex_error == ERROR_ALREADY_EXISTS) {
        HWND existingWnd = nullptr;
        for (int attempt = 0; attempt < 40 && !existingWnd; ++attempt) {
            existingWnd = FindWindowW(L"SC2CoopNativeClass", nullptr);
            if (!existingWnd) Sleep(50);
        }
        if (existingWnd) {
            ShowWindow(existingWnd, SW_RESTORE);
            SetForegroundWindow(existingWnd);
        }
        CloseHandle(hMutex);
        CoUninitialize();
        return 0;
    }

    try {
        // Initialize application state and database
        sc2::AppState::Instance().Init();

        // Initialize UI and graphics pipeline
        if (!sc2::UIManager::Instance().Init(hInstance, nCmdShow)) {
            MessageBoxW(nullptr, L"Failed to initialize DirectX 11 graphics device or window.", L"SC2 Co-op Native Error", MB_ICONERROR | MB_OK);
            CleanAppShutdown();
            if (hMutex) {
                ReleaseMutex(hMutex);
                CloseHandle(hMutex);
            }
            CoUninitialize();
            return 1;
        }

        // Main event loop
        sc2::UIManager::Instance().Run();

        // Clean shutdown
        CleanAppShutdown();
    } catch (const std::exception& e) {
        std::ofstream f("crash_log.txt", std::ios::app);
        if (f.is_open()) {
            f << "C++ Standard Exception: " << e.what() << std::endl;
            f.close();
        }
        std::wstring wmsg = sc2::Win32Utils::Utf8ToWide(e.what());
        MessageBoxW(nullptr, wmsg.c_str(), L"SC2 Co-op Native Exception", MB_ICONERROR | MB_OK);
        CleanAppShutdown();
        if (hMutex) {
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
        }
        CoUninitialize();
        return 1;
    } catch (...) {
        MessageBoxW(nullptr, L"An unknown fatal exception occurred.", L"SC2 Co-op Native Error", MB_ICONERROR | MB_OK);
        CleanAppShutdown();
        if (hMutex) {
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
        }
        CoUninitialize();
        return 1;
    }

    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    CoUninitialize();
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    return wWinMain(hInstance, hPrevInstance, GetCommandLineW(), nCmdShow);
}
