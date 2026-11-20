#pragma once

#include "../../db/database.h"
#include "../../i18n/i18n.h"
#include "../../replay/replay_parser.h"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace sc2 {

class CareerChart {
public:
    void SetActivity(const std::vector<DailyCareerStat>& activity) {
        const auto first_day = activity.empty() ? 0 : activity.front().day_epoch_seconds;
        const auto last_day = activity.empty() ? 0 : activity.back().day_epoch_seconds;
        reset_view_ |= first_day != first_day_ || last_day != last_day_;
        first_day_ = first_day;
        last_day_ = last_day;
        dates_.clear();
        hours_.clear();
        games_.clear();
        dates_.reserve(activity.size() * 3);
        hours_.reserve(activity.size() * 3);
        games_.reserve(activity.size() * 3);
        for (size_t i = 0; i < activity.size(); ++i) {
            const auto& day = activity[i];
            if (i > 0) {
                const auto previous = activity[i - 1].day_epoch_seconds;
                const auto gap = day.day_epoch_seconds - previous;
                // Two zero endpoints represent every inactive day in a long gap.
                if (gap > kDaySeconds) AddPoint(previous + kDaySeconds, 0, 0);
                if (gap > 2 * kDaySeconds) AddPoint(day.day_epoch_seconds - kDaySeconds, 0, 0);
            }
            const double hours = day.total_duration_seconds / 3600.0;
            AddPoint(day.day_epoch_seconds, hours, day.total_games);
        }
    }

    void Render() {
        if (dates_.empty()) {
            ImGui::TextDisabled("%s", TR("Career chart: no activity"));
            return;
        }

        if (ImGui::Button(TR("Reset chart zoom"))) reset_view_ = true;
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("Chart zoom hint"));

        const double x_min = 0.0;
        const double x_max = std::max(1.0, dates_.back());
        ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0.0f, 0.2f));
        if (ImPlot::BeginPlot("##career_daily_activity", ImVec2(-1, -1), ImPlotFlags_NoMouseText)) {
            const ImPlotAxisFlags visible_fit = ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_NoMenus;
            ImPlot::SetupAxis(ImAxis_X1, TR("Date"), ImPlotAxisFlags_NoGridLines);
            ImPlot::SetupAxis(ImAxis_Y1, TR("Daily play time (hours)"), visible_fit);
            ImPlot::SetupAxis(ImAxis_Y2, TR("Daily games"), visible_fit | ImPlotAxisFlags_AuxDefault);
            // Plot days since the first match and format ticks as calendar dates.
            ImPlot::SetupAxisFormat(ImAxis_X1, FormatDate, &first_day_);
            ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, x_min, x_max);
            ImPlot::SetupAxisZoomConstraints(ImAxis_X1, 1.0, x_max - x_min);
            const double upper_bound = (std::numeric_limits<double>::max)();
            ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0.0, upper_bound);
            ImPlot::SetupAxisLimitsConstraints(ImAxis_Y2, 0.0, upper_bound);
            if (reset_view_) {
                ImPlot::SetupAxisLimits(ImAxis_X1, x_min, x_max, ImPlotCond_Always);
                reset_view_ = false;
            }

            DrawCalendar();
            ImPlotSpec style;
            if (dates_.size() == 1) style.Marker = ImPlotMarker_Circle;
            const int count = static_cast<int>(dates_.size());
            const ImVec4 time_color(0.35f, 0.79f, 1.0f, 1.0f);
            const ImVec4 games_color(1.0f, 0.68f, 0.31f, 1.0f);
            ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
            style.LineColor = time_color;
            ImPlot::PlotLine(TR("Daily play time"), dates_.data(), hours_.data(), count, style);
            ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
            style.LineColor = games_color;
            ImPlot::PlotLine(TR("Daily games"), dates_.data(), games_.data(), count, style);
            const auto visible = ImPlot::GetPlotLimits(ImAxis_X1, ImAxis_Y1).X;
            FitVisibleEdges(ImAxis_Y1, "##time_fit_bounds", hours_, visible, 0.25);
            FitVisibleEdges(ImAxis_Y2, "##games_fit_bounds", games_, visible, 1.0);
            if (ImPlot::IsPlotHovered()) DrawDayTooltip(time_color, games_color);
            ImPlot::EndPlot();
        }
        ImPlot::PopStyleVar();
    }

private:
    static constexpr int64_t kDaySeconds = 86400;

    void AddPoint(int64_t seconds, double hours, int games) {
        dates_.push_back(static_cast<double>(seconds - first_day_) / kDaySeconds);
        hours_.push_back(hours);
        games_.push_back(games);
    }

    void FitVisibleEdges(ImAxis axis, const char* id, const std::vector<double>& values,
                         ImPlotRange visible, double minimum_scale) const {
        auto value_at = [&](double date) {
            if (date < dates_.front() || date > dates_.back()) return 0.0;
            const auto next = std::lower_bound(dates_.begin(), dates_.end(), date);
            const auto index = static_cast<size_t>(next - dates_.begin());
            if (*next == date) return values[index];
            const double previous = dates_[index - 1];
            return std::lerp(values[index - 1], values[index], (date - previous) / (*next - previous));
        };
        // RangeFit sees in-range vertices. Add both clipped line endpoints and
        // a zero baseline so even a quiet range keeps a usable vertical scale.
        const double x[] = {visible.Min, visible.Min, visible.Min, visible.Max};
        const double y[] = {0.0, minimum_scale, value_at(visible.Min), value_at(visible.Max)};
        ImPlotSpec fit;
        fit.LineWeight = 0.0f;
        fit.Flags = ImPlotItemFlags_NoLegend;
        ImPlot::SetAxes(ImAxis_X1, axis);
        ImPlot::PlotLine(id, x, y, 4, fit);
    }

    static std::chrono::year_month_day CalendarDate(double epoch_seconds) {
        using namespace std::chrono;
        const sys_seconds time{seconds{static_cast<int64_t>(std::floor(epoch_seconds))}};
        return year_month_day{floor<days>(time)};
    }

    static int FormatDate(double value, char* buffer, int size, void* origin) {
        if (std::abs(value - std::round(value)) > 1e-6) {
            if (size > 0) buffer[0] = 0;
            return 0;
        }
        const auto date = CalendarDate(*static_cast<const int64_t*>(origin) + std::round(value) * kDaySeconds);
        return std::snprintf(buffer, size, "%04d-%02u-%02u",
                             static_cast<int>(date.year()),
                             static_cast<unsigned>(date.month()),
                             static_cast<unsigned>(date.day()));
    }

    void DrawDayTooltip(const ImVec4& time_color, const ImVec4& games_color) {
        const double mouse_x = ImPlot::GetPlotMousePos(ImAxis_X1, ImAxis_Y1).x;
        const double day = std::round(mouse_x);
        if (day < dates_.front() || day > dates_.back()) return;

        const auto point = std::lower_bound(dates_.begin(), dates_.end(), day);
        const auto index = static_cast<size_t>(point - dates_.begin());
        const bool has_point = point != dates_.end() && *point == day;
        const int games = has_point ? static_cast<int>(games_[index]) : 0;
        const int seconds = has_point ? static_cast<int>(std::round(hours_[index] * 3600.0)) : 0;
        const std::string play_time = ReplayParser::FormatDuration(seconds);
        char date[24];
        FormatDate(day, date, sizeof(date), &first_day_);

        if (ImGui::BeginTooltip()) {
            ImGui::TextUnformatted(date);
            ImGui::Separator();
            ImGui::TextColored(games_color, I18n::GetFormat("Career day games", "Games: %d"), games);
            ImGui::TextColored(time_color, "%s: %s", TR("Daily play time"), play_time.c_str());
            ImGui::EndTooltip();
        }
    }

    void DrawCalendar() const {
        using namespace std::chrono;
        const auto range = ImPlot::GetPlotLimits(ImAxis_X1, ImAxis_Y1).X;
        const ImVec2 top_left = ImPlot::GetPlotPos();
        const ImVec2 size = ImPlot::GetPlotSize();
        const float bottom = top_left.y + size.y;
        const double pixels_per_day = size.x / (range.Max - range.Min);
        const bool show_months = pixels_per_day * 28 >= 4;
        const bool show_years = pixels_per_day * 365 >= 2;
        if (!show_years) return;

        const auto pixel_at = [&](sys_days date) {
            const double time = date.time_since_epoch().count() - first_day_ / kDaySeconds;
            const double fraction = std::clamp((time - range.Min) / (range.Max - range.Min), 0.0, 1.0);
            return top_left.x + size.x * static_cast<float>(fraction);
        };
        const auto first_year = CalendarDate(first_day_ + range.Min * kDaySeconds).year();
        const auto last_year = CalendarDate(first_day_ + range.Max * kDaySeconds).year();
        ImDrawList* draw = ImPlot::GetPlotDrawList();
        ImPlot::PushPlotClipRect();
        for (auto year = first_year; year <= last_year; ++year) {
            const float left = pixel_at(sys_days{year / January / 1});
            const float right = pixel_at(sys_days{(year + years{1}) / January / 1});
            if (static_cast<int>(year) % 2 == 0) {
                draw->AddRectFilled(ImVec2(left, top_left.y), ImVec2(right, bottom),
                                    IM_COL32(120, 150, 180, 28));
            }
            if (left > top_left.x) {
                draw->AddLine(ImVec2(left, top_left.y), ImVec2(left, bottom), IM_COL32(160, 175, 190, 105));
            }
            if (show_months) {
                for (unsigned month = 2; month <= 12; ++month) {
                    const float x = pixel_at(sys_days{year / month / 1});
                    if (x > top_left.x && x < top_left.x + size.x) {
                        draw->AddLine(ImVec2(x, top_left.y), ImVec2(x, bottom), IM_COL32(160, 175, 190, 55));
                    }
                }
            }
        }
        ImPlot::PopPlotClipRect();
    }

    std::vector<double> dates_;
    std::vector<double> hours_;
    std::vector<double> games_;
    int64_t first_day_ = 0;
    int64_t last_day_ = 0;
    bool reset_view_ = true;
};

} // namespace sc2
