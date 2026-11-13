#pragma once

namespace sc2 {

class AppState;

class TabWeeklies {
public:
    static void Render(AppState& state);

private:
    static char s_search_buf[128];
};

} // namespace sc2
