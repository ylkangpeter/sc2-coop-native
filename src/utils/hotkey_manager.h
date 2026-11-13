#pragma once

#include <windows.h>
#include <string>
#include <functional>
#include <unordered_map>

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

private:
    HotkeyManager() = default;
    ~HotkeyManager() = default;

    bool RegisterSingle(int id, UINT mod, UINT vk);

    HWND m_hwnd = nullptr;
    std::unordered_map<int, HotkeyAction> m_action_map;
    std::unordered_map<HotkeyAction, std::function<void()>> m_callbacks;
};

} // namespace sc2
