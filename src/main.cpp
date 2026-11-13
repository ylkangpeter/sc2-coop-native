#include <windows.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include "app_state.h"
#include "ui/ui_manager.h"
#include "db/database.h"
#include <backends/imgui_impl_win32.h>

static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* pException) {
    std::ofstream f("crash_log.txt", std::ios::app);
    if (f.is_open()) {
        f << "Unhandled Exception: 0x" << std::hex << pException->ExceptionRecord->ExceptionCode << std::endl;
        f << "Address: 0x" << pException->ExceptionRecord->ExceptionAddress << std::endl;
        f.close();
    }
    wchar_t buf[256];
    wsprintfW(buf, L"Fatal Crash: Exception code 0x%08X at 0x%p.\nDetails written to crash_log.txt.",
              pException->ExceptionRecord->ExceptionCode, pException->ExceptionRecord->ExceptionAddress);
    MessageBoxW(nullptr, buf, L"SC2 Co-op Native Error", MB_ICONERROR | MB_OK);
    return EXCEPTION_EXECUTE_HANDLER;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    SetUnhandledExceptionFilter(CrashHandler);

    // Enable modern Per-Monitor V2 DPI Awareness for crisp High-DPI scaling
    ImGui_ImplWin32_EnableDpiAwareness();

    // Single instance check
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"SC2CoopNativeSingleInstanceMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existingWnd = FindWindowW(L"SC2CoopNativeClass", nullptr);
        if (existingWnd) {
            ShowWindow(existingWnd, SW_RESTORE);
            SetForegroundWindow(existingWnd);
        }
        if (hMutex) {
            CloseHandle(hMutex);
        }
        return 0;
    }

    try {
        // Initialize application state and database
        sc2::AppState::Instance().Init();

        // Initialize UI and graphics pipeline
        if (!sc2::UIManager::Instance().Init(hInstance, nCmdShow)) {
            MessageBoxW(nullptr, L"Failed to initialize DirectX 11 graphics device or window.", L"SC2 Co-op Native Error", MB_ICONERROR | MB_OK);
            if (hMutex) {
                ReleaseMutex(hMutex);
                CloseHandle(hMutex);
            }
            return 1;
        }

        // Main event loop
        sc2::UIManager::Instance().Run();

        // Clean shutdown
        sc2::UIManager::Instance().Shutdown();
        sc2::AppState::Instance().StopScan();
        sc2::AppState::Instance().SaveSettings();
        sc2::Database::Instance().Close();
    } catch (const std::exception& e) {
        std::ofstream f("crash_log.txt", std::ios::app);
        if (f.is_open()) {
            f << "C++ Standard Exception: " << e.what() << std::endl;
            f.close();
        }
        int len = MultiByteToWideChar(CP_UTF8, 0, e.what(), -1, nullptr, 0);
        std::wstring wmsg(len, 0);
        MultiByteToWideChar(CP_UTF8, 0, e.what(), -1, &wmsg[0], len);
        MessageBoxW(nullptr, wmsg.c_str(), L"SC2 Co-op Native Exception", MB_ICONERROR | MB_OK);
        if (hMutex) {
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
        }
        return 1;
    } catch (...) {
        MessageBoxW(nullptr, L"An unknown fatal exception occurred.", L"SC2 Co-op Native Error", MB_ICONERROR | MB_OK);
        if (hMutex) {
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
        }
        return 1;
    }

    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    return wWinMain(hInstance, hPrevInstance, GetCommandLineW(), nCmdShow);
}
