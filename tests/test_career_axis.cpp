#include "../src/ui/tabs/career_chart.h"
#include <implot_internal.h>
#include <cassert>
#include <string>

int main() {
    ImGui::CreateContext();
    ImPlot::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1000, 540);
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    sc2::CareerChart chart;
    const auto day = [](int year, unsigned month, unsigned date) {
        return std::chrono::sys_seconds{std::chrono::sys_days{std::chrono::year{year}/month/date}}.time_since_epoch().count();
    };
    auto check = [&](const char* first_label, double last) {
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Career");
        chart.Render();
        auto* plot = ImPlot::GetPlot("##career_daily_activity");
        assert(plot);
        auto& axis = plot->Axes[ImAxis_X1];
        assert(axis.Range.Min == 0 && axis.Range.Max == last);
        bool first_tick = false;
        for (const auto& tick : axis.Ticker.Ticks) {
            if (tick.ShowLabel && tick.PlotPos == 0)
                first_tick |= std::string(axis.Ticker.GetText(tick)) == first_label;
        }
        assert(first_tick);
        ImGui::End();
        ImGui::Render();
    };
    chart.SetActivity({{day(2025, 2, 6), 600, 1}, {day(2026, 2, 6), 900, 2}});
    check("2025-02-06", 365);
    check("2025-02-06", 365);
    chart.SetActivity({{day(2026, 3, 7), 900, 2}});
    check("2026-03-07", 1);
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
}
