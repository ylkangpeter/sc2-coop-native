#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include "../../replay/replay_types.h"

namespace sc2 {

enum class OverlayAnimState {
    Hidden,
    SlidingIn,
    Visible,
    SlidingOut
};

class SummaryOverlay {
public:
    static SummaryOverlay& Instance();

    void Init(HINSTANCE hInstance);
    void Shutdown();

    void ShowMatch(const ReplayRecord& rec, bool is_best_time = false, float duration = 15.0f);
    void ShowLatest();
    void ShowPrevious();
    void ShowNext();
    void Toggle();
    void Hide();

    bool IsActive() const { return m_anim_state != OverlayAnimState::Hidden; }
    void Update(float delta_time);
    void DumpNextRender(const std::string& path) { m_dump_path = path; }

private:
    SummaryOverlay() = default;
    ~SummaryOverlay() = default;

    void Render();
    void UpdatePosition();
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HINSTANCE m_hInstance = nullptr;
    HWND m_hwnd = nullptr;

    OverlayAnimState m_anim_state = OverlayAnimState::Hidden;
    float m_anim_t = 0.0f;           // 0.0 = completely offscreen right, 1.0 = fully docked to right edge
    float m_remaining_time = 0.0f;
    float m_total_duration = 15.0f;

    ReplayRecord m_current_record;
    bool m_is_best_time = false;
    int m_browse_index = 0;
    std::vector<ReplayRecord> m_cached_recent;

    int m_overlay_w = 1000;
    int m_overlay_h = 1080;

    HFONT m_hFontRotated = nullptr;
    std::string m_dump_path;
};

} // namespace sc2
