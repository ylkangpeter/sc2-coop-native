#pragma once

namespace sc2 {

class AppState;

class TabGames {
public:
    static void Render(AppState& state);

private:
    static int s_current_page;
    static const int kPageSize = 25;
};

} // namespace sc2
