#pragma once

#include <windows.h>
#include <string>
#include <functional>
#include <unordered_map>
#include <chrono>

namespace sc2 {

enum class HotkeyAction {
    ToggleSummary = 1,
    PrevMatch,
    NextMatch,
    AllyInfo
};

class HotkeyManager {
public:
    static HotkeyManager& Instance();

    void Init(HWND hwnd);
    void Shutdown();

    // Register / re-register hotkeys based on AppState
    void RegisterAll();
    void UnregisterAll();

    void OnHotkey(int id);
    void SetCallback(HotkeyAction action, std::function<void()> callback);

    // Low-level keyboard hook callback handler
    bool HandleLowLevelKey(DWORD vk, bool is_sys_key, bool is_down);

    void ApplyRecordedHotkey(int action_id, UINT mod, UINT vk);
    void DispatchAction(HotkeyAction action);

    // Recording mode management
    void StartRecording(int action_id);
    void StopRecording();
    bool IsRecording() const { return m_recording_action > 0; }
    int GetRecordingAction() const { return m_recording_action; }

    // Human readable hotkey string (e.g. "Ctrl + Shift + H")
    static std::string FormatHotkey(UINT mod, UINT vk);

private:
    HotkeyManager() = default;
    ~HotkeyManager() = default;

    bool RegisterSingle(int id, UINT mod, UINT vk);
    void TriggerAction(HotkeyAction action);

    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd = nullptr;
    HHOOK m_hook = nullptr;

    int m_recording_action = -1; // 1 = Summary, 2 = Ally, 3 = Prev, 4 = Next

    std::unordered_map<int, HotkeyAction> m_action_map;
    std::unordered_map<HotkeyAction, std::function<void()>> m_callbacks;
    std::unordered_map<HotkeyAction, std::chrono::steady_clock::time_point> m_last_trigger_time;
};

} // namespace sc2
