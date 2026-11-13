#pragma once

#include <windows.h>
#include <string>

namespace sc2 {

class AllyToastWindow {
public:
    static AllyToastWindow& Instance();

    void Init(HINSTANCE hInstance);
    void Shutdown();

    void ShowAllyInfo(const std::string& ally_name, float duration = 10.0f);
    void ShowTestToast();
    void Hide();
    void Toggle(const std::string& ally_name = "");
    void Update(float delta_time);

    bool IsActive() const { return m_active; }

private:
    AllyToastWindow() = default;
    ~AllyToastWindow() = default;

    void Render();
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HINSTANCE m_hInstance = nullptr;
    HWND m_hwnd = nullptr;

    bool m_active = false;
    float m_remaining_time = 0.0f;

    std::string m_ally_name;
    int m_total_games = 0;
    int m_wins = 0;
    double m_winrate = 0.0;
    int m_apm = 0;
    std::string m_note;
};

} // namespace sc2
