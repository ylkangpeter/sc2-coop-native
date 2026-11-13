#include "hotkey_manager.h"
#include "../app_state.h"
#include "win32_utils.h"
#include <iostream>

#define HOTKEY_SUMMARY_ID      2001
#define HOTKEY_PREV_ID         2003
#define HOTKEY_PREV_ALT_ID     2004
#define HOTKEY_NEXT_ID         2005
#define HOTKEY_NEXT_ALT_ID     2006
#define HOTKEY_ALLY_ID         2007

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
    if (!m_hwnd || vk == 0) return false;
    if (RegisterHotKey(m_hwnd, id, mod | MOD_NOREPEAT, vk)) {
        return true;
    }
    return RegisterHotKey(m_hwnd, id, mod, vk);
}

void HotkeyManager::RegisterAll() {
    UnregisterAll();
    if (!m_hwnd) return;

    auto& state = AppState::Instance();

    // 1. Toggle Summary Overlay (Default: Ctrl + Shift + H, user-customizable, NO F11)
    if (state.hotkey_summary_enabled && state.hotkey_summary_vk != 0) {
        if (RegisterSingle(HOTKEY_SUMMARY_ID, state.hotkey_summary_mod, state.hotkey_summary_vk)) {
            m_action_map[HOTKEY_SUMMARY_ID] = HotkeyAction::ToggleSummary;
        }
    }

    // 2. Previous Match Summary (Default: Ctrl + Alt + Left, and PageDown)
    if (state.hotkey_nav_enabled) {
        if (state.hotkey_prev_vk != 0 && RegisterSingle(HOTKEY_PREV_ID, state.hotkey_prev_mod, state.hotkey_prev_vk)) {
            m_action_map[HOTKEY_PREV_ID] = HotkeyAction::PrevMatch;
        }
        if (RegisterSingle(HOTKEY_PREV_ALT_ID, 0, VK_NEXT)) { // Page Down
            m_action_map[HOTKEY_PREV_ALT_ID] = HotkeyAction::PrevMatch;
        }

        // 3. Next Match Summary (Default: Ctrl + Alt + Right, and PageUp)
        if (state.hotkey_next_vk != 0 && RegisterSingle(HOTKEY_NEXT_ID, state.hotkey_next_mod, state.hotkey_next_vk)) {
            m_action_map[HOTKEY_NEXT_ID] = HotkeyAction::NextMatch;
        }
        if (RegisterSingle(HOTKEY_NEXT_ALT_ID, 0, VK_PRIOR)) { // Page Up
            m_action_map[HOTKEY_NEXT_ALT_ID] = HotkeyAction::NextMatch;
        }
    }

    // 4. Ally Info & Winrates (Default: Ctrl + Alt + W, user-customizable, NO F10)
    if (state.hotkey_ally_enabled && state.hotkey_ally_vk != 0) {
        if (RegisterSingle(HOTKEY_ALLY_ID, state.hotkey_ally_mod, state.hotkey_ally_vk)) {
            m_action_map[HOTKEY_ALLY_ID] = HotkeyAction::AllyInfo;
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

std::string HotkeyManager::FormatHotkey(UINT mod, UINT vk) {
    if (vk == 0) return "None";
    std::string s;
    if (mod & MOD_CONTROL) s += "Ctrl + ";
    if (mod & MOD_ALT)     s += "Alt + ";
    if (mod & MOD_SHIFT)   s += "Shift + ";
    if (mod & MOD_WIN)     s += "Win + ";

    if (vk >= 'A' && vk <= 'Z') {
        s += static_cast<char>(vk);
    } else if (vk >= '0' && vk <= '9') {
        s += static_cast<char>(vk);
    } else if (vk >= VK_F1 && vk <= VK_F24) {
        s += "F" + std::to_string(vk - VK_F1 + 1);
    } else {
        switch (vk) {
            case VK_LEFT:   s += "Left"; break;
            case VK_RIGHT:  s += "Right"; break;
            case VK_UP:     s += "Up"; break;
            case VK_DOWN:   s += "Down"; break;
            case VK_PRIOR:  s += "PageUp"; break;
            case VK_NEXT:   s += "PageDown"; break;
            case VK_HOME:   s += "Home"; break;
            case VK_END:    s += "End"; break;
            case VK_INSERT: s += "Insert"; break;
            case VK_DELETE: s += "Delete"; break;
            case VK_SPACE:  s += "Space"; break;
            case VK_TAB:    s += "Tab"; break;
            case VK_OEM_3:  s += "~"; break;
            case VK_OEM_MINUS: s += "-"; break;
            case VK_OEM_PLUS:  s += "+"; break;
            case VK_OEM_4:  s += "["; break;
            case VK_OEM_6:  s += "]"; break;
            case VK_OEM_1:  s += ";"; break;
            case VK_OEM_7:  s += "'"; break;
            case VK_OEM_COMMA:  s += ","; break;
            case VK_OEM_PERIOD: s += "."; break;
            case VK_OEM_2:  s += "/"; break;
            case VK_OEM_5:  s += "\\"; break;
            default: {
                UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
                if (sc > 0) {
                    wchar_t buf[64] = {0};
                    if (GetKeyNameTextW(sc << 16, buf, 64) > 0) {
                        s += Win32Utils::WideToUtf8(buf);
                        break;
                    }
                }
                char hex[16];
                snprintf(hex, sizeof(hex), "0x%02X", vk);
                s += hex;
                break;
            }
        }
    }
    return s;
}

} // namespace sc2
