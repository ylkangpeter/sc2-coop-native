#pragma once

#include <string>

namespace sc2 {

class AppState;

class TabPlayers {
public:
    static void Render(AppState& state);

private:
    static int s_current_page;
    static std::string s_sort_column;
    static bool s_sort_ascending;
    static const int kPageSize = 25;
};

} // namespace sc2
