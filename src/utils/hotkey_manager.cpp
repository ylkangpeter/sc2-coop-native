#include "hotkey_manager.h"
#include "../app_state.h"
#include "win32_utils.h"
#include "../ui/hud/summary_overlay.h"
#include "../ui/hud/ally_toast.h"
#include "../ui/ui_manager.h"
#include <iostream>
#include "diagnostics.h"

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
    m_recording_action = -1;
    RegisterAll();
}

void HotkeyManager::Shutdown() {
    UnregisterAll();
    m_hwnd = nullptr;
    m_recording_action = -1;
    m_unavailable_actions.clear();
    UpdateHook();
}

void HotkeyManager::UpdateHook() {
    const bool needed = m_hwnd && (IsRecording() || SummaryOverlay::Instance().IsActive() ||
                                                   AllyToastWindow::Instance().IsActive());
    if (needed && !m_hook) {
        m_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandleW(nullptr), 0);
        s_global_hook = m_hook;
        if (!m_hook) LogDiagnostic("hotkeys", "temporary keyboard hook could not be installed");
    } else if (!needed && m_hook) {
        UnhookWindowsHookEx(m_hook);
        m_hook = nullptr;
        s_global_hook = nullptr;
    }
}

void HotkeyManager::UpdateContext() {
    UpdateHook();
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
    m_unavailable_actions.clear();
    if (m_hwnd && !IsRecording()) {
        const auto& state = AppState::Instance();
        auto add = [&](bool enabled, int id, int action_id, HotkeyAction action, UINT mod, UINT vk) {
            if (!enabled || !vk) return;
            if (RegisterSingle(id, mod, vk)) {
                m_action_map[id] = action;
            } else {
                m_unavailable_actions.insert(action_id);
                LogDiagnostic("hotkeys", "shortcut unavailable: " + FormatHotkey(mod, vk));
            }
        };
        add(state.hotkey_summary_enabled, HOTKEY_SUMMARY_ID, 1, HotkeyAction::ToggleSummary,
            state.hotkey_summary_mod, state.hotkey_summary_vk);
        add(state.hotkey_ally_enabled, HOTKEY_ALLY_ID, 2, HotkeyAction::AllyInfo,
            state.hotkey_ally_mod, state.hotkey_ally_vk);
        add(state.hotkey_nav_enabled, HOTKEY_PREV_ID, 3, HotkeyAction::PrevMatch,
            state.hotkey_prev_mod, state.hotkey_prev_vk);
        add(state.hotkey_nav_enabled, HOTKEY_NEXT_ID, 4, HotkeyAction::NextMatch,
            state.hotkey_next_mod, state.hotkey_next_vk);
    }
    UpdateHook();
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
    if (IsRecording()) return;
    auto it = m_action_map.find(id);
    if (it != m_action_map.end()) {
        TriggerAction(it->second);
    }
}

bool HotkeyManager::HandleLowLevelKey(DWORD vk, bool is_sys_key, bool is_down) {
    if (!is_down) return false;
    if (IsRecording()) {
        if (vk == VK_ESCAPE) {
            m_recording_action = -1;
            PostMessageW(m_hwnd, WM_USER_HOTKEY_RECORD_CANCELLED, 0, 0);
            return true;
        }
        if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
            vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
            vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
            vk == VK_LWIN || vk == VK_RWIN) return false;

        const UINT mod = ((GetAsyncKeyState(VK_CONTROL) & 0x8000) ? MOD_CONTROL : 0) |
                         ((GetAsyncKeyState(VK_MENU) & 0x8000) ? MOD_ALT : 0) |
                         ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? MOD_SHIFT : 0) |
                         (((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) ? MOD_WIN : 0);
        const int action = m_recording_action;
        m_recording_action = -1;
        // Re-register shortcuts, remove the hook and save settings outside the
        // synchronous input callback, through the normal UI message queue.
        PostMessageW(m_hwnd, WM_USER_HOTKEY_RECORDED, action, MAKELPARAM(mod, vk));
        return true;
    }

    if (vk == VK_ESCAPE) {
        PostMessageW(m_hwnd, WM_USER_DISMISS_OVERLAYS, 0, 0);
        return false; // The game must still receive its Escape key.
    }
    // Normal hotkeys are handled exclusively by RegisterHotKey/WM_HOTKEY.
    // All ordinary typing and game keys take this branch with no key-state queries.
    if ((vk != VK_PRIOR && vk != VK_NEXT) || !AppState::Instance().hotkey_nav_enabled ||
        !SummaryOverlay::Instance().IsActive()) return false;
    if (((GetAsyncKeyState(VK_CONTROL) | GetAsyncKeyState(VK_MENU) | GetAsyncKeyState(VK_SHIFT) |
          GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) != 0) return false;
    const auto& state = AppState::Instance();
    const auto registered_key = [&](int id, UINT mod, UINT key) {
        return mod == 0 && key == vk && m_action_map.count(id) != 0;
    };
    // Explicit assignments take priority over the overlay's bare PageUp/Down
    // convenience controls. Let Windows deliver the registered action once.
    if (registered_key(HOTKEY_SUMMARY_ID, state.hotkey_summary_mod, state.hotkey_summary_vk) ||
        registered_key(HOTKEY_ALLY_ID, state.hotkey_ally_mod, state.hotkey_ally_vk) ||
        registered_key(HOTKEY_PREV_ID, state.hotkey_prev_mod, state.hotkey_prev_vk) ||
        registered_key(HOTKEY_NEXT_ID, state.hotkey_next_mod, state.hotkey_next_vk)) return false;
    const auto action = vk == VK_NEXT ? HotkeyAction::PrevMatch : HotkeyAction::NextMatch;
    PostMessageW(m_hwnd, WM_USER_HOTKEY_ACTION, static_cast<WPARAM>(action), 0);
    return true;
}

void HotkeyManager::ApplyRecordedHotkey(int action_id, UINT mod, UINT vk) {
    auto& state = AppState::Instance();
    if (action_id == 1) {
        state.hotkey_summary_mod = mod;
        state.hotkey_summary_vk = vk;
    } else if (action_id == 2) {
        state.hotkey_ally_mod = mod;
        state.hotkey_ally_vk = vk;
    } else if (action_id == 3) {
        state.hotkey_prev_mod = mod;
        state.hotkey_prev_vk = vk;
    } else if (action_id == 4) {
        state.hotkey_next_mod = mod;
        state.hotkey_next_vk = vk;
    }
    state.SaveSettings();
    RegisterAll();
}

void HotkeyManager::DispatchAction(HotkeyAction action) {
    TriggerAction(action);
}

void HotkeyManager::StartRecording(int action_id) {
    if (!m_hwnd || action_id < 1 || action_id > 4) return;
    m_recording_action = action_id;
    RegisterAll(); // Suspend global shortcuts while capturing a replacement.
    if (!m_hook) StopRecording();
}

void HotkeyManager::StopRecording() {
    m_recording_action = -1;
    RegisterAll();
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
