#include "hotkey_manager.h"
#include "../app_state.h"
#include <iostream>

#define HOTKEY_SUMMARY_ID      2001
#define HOTKEY_SUMMARY_ALT_ID  2002
#define HOTKEY_PREV_ID         2003
#define HOTKEY_PREV_ALT_ID     2004
#define HOTKEY_NEXT_ID         2005
#define HOTKEY_NEXT_ALT_ID     2006
#define HOTKEY_ALLY_ID         2007
#define HOTKEY_ALLY_ALT_ID     2008

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif

namespace sc2 {

HotkeyManager& HotkeyManager::Instance() {
    static HotkeyManager instance;
    return instance;
}

void HotkeyManager::Init(HWND hwnd) {
    m_hwnd = hwnd;
    RegisterAll();
}

void HotkeyManager::Shutdown() {
    UnregisterAll();
    m_hwnd = nullptr;
}

bool HotkeyManager::RegisterSingle(int id, UINT mod, UINT vk) {
    if (!m_hwnd) return false;
    if (RegisterHotKey(m_hwnd, id, mod | MOD_NOREPEAT, vk)) {
        return true;
    }
    return RegisterHotKey(m_hwnd, id, mod, vk);
}

void HotkeyManager::RegisterAll() {
    UnregisterAll();
    if (!m_hwnd) return;

    auto& state = AppState::Instance();

    // 1. Toggle Summary Overlay (Default: Ctrl+Shift+H and F11)
    if (state.hotkey_summary_enabled) {
        if (RegisterSingle(HOTKEY_SUMMARY_ID, MOD_CONTROL | MOD_SHIFT, 'H')) {
            m_action_map[HOTKEY_SUMMARY_ID] = HotkeyAction::ToggleSummary;
        }
        if (RegisterSingle(HOTKEY_SUMMARY_ALT_ID, 0, VK_F11)) {
            m_action_map[HOTKEY_SUMMARY_ALT_ID] = HotkeyAction::ToggleSummary;
        }
    }

    // 2. Previous (Older) Match Summary (Default: Ctrl+Alt+Left and PageDown)
    if (state.hotkey_nav_enabled) {
        if (RegisterSingle(HOTKEY_PREV_ID, MOD_CONTROL | MOD_ALT, VK_LEFT)) {
            m_action_map[HOTKEY_PREV_ID] = HotkeyAction::PrevMatch;
        }
        if (RegisterSingle(HOTKEY_PREV_ALT_ID, 0, VK_NEXT)) { // Page Down
            m_action_map[HOTKEY_PREV_ALT_ID] = HotkeyAction::PrevMatch;
        }

        // 3. Next (Newer) Match Summary (Default: Ctrl+Alt+Right and PageUp)
        if (RegisterSingle(HOTKEY_NEXT_ID, MOD_CONTROL | MOD_ALT, VK_RIGHT)) {
            m_action_map[HOTKEY_NEXT_ID] = HotkeyAction::NextMatch;
        }
        if (RegisterSingle(HOTKEY_NEXT_ALT_ID, 0, VK_PRIOR)) { // Page Up
            m_action_map[HOTKEY_NEXT_ALT_ID] = HotkeyAction::NextMatch;
        }
    }

    // 4. Ally Info & Winrates (Default: Ctrl+Alt+W and F10)
    if (state.hotkey_ally_enabled) {
        if (RegisterSingle(HOTKEY_ALLY_ID, MOD_CONTROL | MOD_ALT, 'W')) {
            m_action_map[HOTKEY_ALLY_ID] = HotkeyAction::AllyInfo;
        }
        if (RegisterSingle(HOTKEY_ALLY_ALT_ID, 0, VK_F10)) {
            m_action_map[HOTKEY_ALLY_ALT_ID] = HotkeyAction::AllyInfo;
        }
    }
}

void HotkeyManager::UnregisterAll() {
    if (!m_hwnd) return;
    for (const auto& pair : m_action_map) {
        UnregisterHotKey(m_hwnd, pair.first);
    }
    m_action_map.clear();
}

void HotkeyManager::OnHotkey(int id) {
    auto it = m_action_map.find(id);
    if (it != m_action_map.end()) {
        auto cb_it = m_callbacks.find(it->second);
        if (cb_it != m_callbacks.end() && cb_it->second) {
            cb_it->second();
        }
    }
}

void HotkeyManager::SetCallback(HotkeyAction action, std::function<void()> callback) {
    m_callbacks[action] = callback;
}

} // namespace sc2
