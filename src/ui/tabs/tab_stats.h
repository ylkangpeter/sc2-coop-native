#pragma once

namespace sc2 {

class AppState;

class TabStats {
public:
    static void Render(AppState& state);

private:
    static int s_sub_tab;
};

} // namespace sc2
