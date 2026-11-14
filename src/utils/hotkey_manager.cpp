#include "hotkey_manager.h"
#include "../app_state.h"
#include "win32_utils.h"
#include "../ui/hud/summary_overlay.h"
#include "../ui/hud/ally_toast.h"
#include <iostream>

#define HOTKEY_SUMMARY_ID      2001
#define HOTKEY_PREV_ID         2003
#define HOTKEY_NEXT_ID         2005
#define HOTKEY_ALLY_ID         2007

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif

namespace sc2 {

static HHOOK s_global_hook = nullptr;

HotkeyManager& HotkeyManager::Instance() {
    static HotkeyManager instance;
    return instance;
}

LRESULT CALLBACK HotkeyManager::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        bool is_down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        if (is_down) {
            KBDLLHOOKSTRUCT* pKey = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
            if (pKey) {
                if (HotkeyManager::Instance().HandleLowLevelKey(pKey->vkCode, (wParam == WM_SYSKEYDOWN), true)) {
                    return 1; // Handled and consumed
                }
            }
        }
    }
    return CallNextHookEx(s_global_hook, nCode, wParam, lParam);
}

void HotkeyManager::Init(HWND hwnd) {
    m_hwnd = hwnd;
    if (!m_hook) {
        m_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandleW(nullptr), 0);
        s_global_hook = m_hook;
    }
    RegisterAll();
}

void HotkeyManager::Shutdown() {
    if (m_hook) {
        UnhookWindowsHookEx(m_hook);
        m_hook = nullptr;
        s_global_hook = nullptr;
    }
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

    // 1. Toggle Summary Overlay (Default: Ctrl + Shift + H)
    if (state.hotkey_summary_enabled && state.hotkey_summary_vk != 0) {
        if (RegisterSingle(HOTKEY_SUMMARY_ID, state.hotkey_summary_mod, state.hotkey_summary_vk)) {
            m_action_map[HOTKEY_SUMMARY_ID] = HotkeyAction::ToggleSummary;
        }
    }

    // 2. Previous Match Summary (Default: Ctrl + Alt + Left)
    if (state.hotkey_nav_enabled && state.hotkey_prev_vk != 0) {
        if (RegisterSingle(HOTKEY_PREV_ID, state.hotkey_prev_mod, state.hotkey_prev_vk)) {
            m_action_map[HOTKEY_PREV_ID] = HotkeyAction::PrevMatch;
        }
    }

    // 3. Next Match Summary (Default: Ctrl + Alt + Right)
    if (state.hotkey_nav_enabled && state.hotkey_next_vk != 0) {
        if (RegisterSingle(HOTKEY_NEXT_ID, state.hotkey_next_mod, state.hotkey_next_vk)) {
            m_action_map[HOTKEY_NEXT_ID] = HotkeyAction::NextMatch;
        }
    }

    // 4. Ally Info & Winrates (Default: Ctrl + Alt + W)
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

void HotkeyManager::TriggerAction(HotkeyAction action) {
    auto now = std::chrono::steady_clock::now();
    auto it_t = m_last_trigger_time.find(action);
    if (it_t != m_last_trigger_time.end()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - it_t->second).count();
        if (elapsed < 200) {
            return; // 200ms debounce prevents double trigger from Hook + RegisterHotKey
        }
    }
    m_last_trigger_time[action] = now;

    auto it = m_callbacks.find(action);
    if (it != m_callbacks.end() && it->second) {
        it->second();
    }
}

void HotkeyManager::OnHotkey(int id) {
    auto it = m_action_map.find(id);
    if (it != m_action_map.end()) {
        TriggerAction(it->second);
    }
}

bool HotkeyManager::HandleLowLevelKey(DWORD vk, bool is_sys_key, bool is_down) {
    // 1. If currently recording a new hotkey in Settings
    if (m_recording_action > 0) {
        if (vk == VK_ESCAPE) {
            StopRecording();
            return true;
        }

        // Wait for actual key, ignore bare modifiers
        if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
            vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
            vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
            vk == VK_LWIN || vk == VK_RWIN) {
            return false;
        }

        bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
        bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        bool win = ((GetAsyncKeyState(VK_LWIN) & 0x8000) != 0) || ((GetAsyncKeyState(VK_RWIN) & 0x8000) != 0);

        UINT mod = (ctrl ? MOD_CONTROL : 0) | (alt ? MOD_ALT : 0) | (shift ? MOD_SHIFT : 0) | (win ? MOD_WIN : 0);

        auto& state = AppState::Instance();
        if (m_recording_action == 1) {
            state.hotkey_summary_mod = mod;
            state.hotkey_summary_vk = vk;
        } else if (m_recording_action == 2) {
            state.hotkey_ally_mod = mod;
            state.hotkey_ally_vk = vk;
        } else if (m_recording_action == 3) {
            state.hotkey_prev_mod = mod;
            state.hotkey_prev_vk = vk;
        } else if (m_recording_action == 4) {
            state.hotkey_next_mod = mod;
            state.hotkey_next_vk = vk;
        }
        state.SaveSettings();
        RegisterAll();
        StopRecording();
        return true;
    }

    // 2. Escape closes any visible overlay/toast
    if (vk == VK_ESCAPE) {
        if (SummaryOverlay::Instance().IsActive()) {
            SummaryOverlay::Instance().Hide();
            return true;
        }
        if (AllyToastWindow::Instance().IsActive()) {
            AllyToastWindow::Instance().Hide();
            return true;
        }
        return false;
    }

    // 3. Evaluate modifier keys
    bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    bool win = ((GetAsyncKeyState(VK_LWIN) & 0x8000) != 0) || ((GetAsyncKeyState(VK_RWIN) & 0x8000) != 0);

    UINT current_mod = (ctrl ? MOD_CONTROL : 0) | (alt ? MOD_ALT : 0) | (shift ? MOD_SHIFT : 0) | (win ? MOD_WIN : 0);

    auto& state = AppState::Instance();

    // Hotkey 1: Toggle Match Summary Overlay
    if (state.hotkey_summary_enabled && state.hotkey_summary_vk != 0) {
        if (vk == state.hotkey_summary_vk && current_mod == state.hotkey_summary_mod) {
            TriggerAction(HotkeyAction::ToggleSummary);
            return true;
        }
    }

    // Hotkey 2: Toggle Ally Info & Winrates
    if (state.hotkey_ally_enabled && state.hotkey_ally_vk != 0) {
        if (vk == state.hotkey_ally_vk && current_mod == state.hotkey_ally_mod) {
            TriggerAction(HotkeyAction::AllyInfo);
            return true;
        }
    }

    // Hotkeys 3 & 4: Browse Previous / Next Match (only consume if Summary Overlay is visible)
    if (state.hotkey_nav_enabled && SummaryOverlay::Instance().IsActive()) {
        if (state.hotkey_prev_vk != 0 && vk == state.hotkey_prev_vk && current_mod == state.hotkey_prev_mod) {
            TriggerAction(HotkeyAction::PrevMatch);
            return true;
        }
        if (vk == VK_NEXT && current_mod == 0) { // Page Down
            TriggerAction(HotkeyAction::PrevMatch);
            return true;
        }
        if (state.hotkey_next_vk != 0 && vk == state.hotkey_next_vk && current_mod == state.hotkey_next_mod) {
            TriggerAction(HotkeyAction::NextMatch);
            return true;
        }
        if (vk == VK_PRIOR && current_mod == 0) { // Page Up
            TriggerAction(HotkeyAction::NextMatch);
            return true;
        }
    }

    return false;
}

void HotkeyManager::StartRecording(int action_id) {
    m_recording_action = action_id;
}

void HotkeyManager::StopRecording() {
    m_recording_action = -1;
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
