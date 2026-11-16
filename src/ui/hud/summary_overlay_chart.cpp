#include "summary_overlay_types.h"
#include <cmath>
#include <algorithm>

namespace sc2 {

std::wstring FormatChartSeconds(float sec) {
    int total_s = static_cast<int>(sec + 0.5f);
    int m = total_s / 60;
    int s = total_s % 60;
    wchar_t buf[16];
    wsprintfW(buf, L"%02d:%02d", m, s);
    return buf;
}

std::wstring FormatChartValue(float val) {
    wchar_t buf[32];
    if (val >= 1000.0f) {
        int thousands = (int)(val / 1000.0f);
        int rem = ((int)val) % 1000;
        wsprintfW(buf, L"%d,%03d", thousands, rem);
    } else {
        wsprintfW(buf, L"%d", (int)(val + 0.5f));
    }
    return buf;
}

void DrawSummaryChart(HDC hdc, const SummaryOverlayFonts& fonts,
                      int cx, int cy, int cw, int ch,
                      const std::wstring& title, float duration_seconds,
                      const std::vector<float>& times1, const std::vector<float>& vals1,
                      const std::vector<float>& times2, const std::vector<float>& vals2,
                      COLORREF color1, COLORREF color2) {
    // Plot drawing region inside chart
    int px = cx + 55;
    int py = cy + 28;
    int pw = cw - 70;
    int ph = ch - 58;

    // Chart Title (Centered above plot, Eurostile bold #DDDDDD)
    SelectObject(hdc, fonts.hFontChartTitle);
    SetTextColor(hdc, RGB(225, 230, 240));
    RECT rcTitle = { px, cy, px + pw, cy + 24 };
    DrawTextW(hdc, title.c_str(), -1, &rcTitle, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    float max_time = duration_seconds > 0 ? duration_seconds : 900.0f;
    if (max_time < 60.0f) max_time = 60.0f;

    float max_val = 10.0f;
    for (float v : vals1) if (v > max_val) max_val = v;
    for (float v : vals2) if (v > max_val) max_val = v;

    // Clean round Y intervals matching reference screenshot
    if (max_val > 10000.0f) max_val = std::ceil(max_val / 4000.0f) * 4000.0f;
    else if (max_val > 4000.0f) max_val = std::ceil(max_val / 2000.0f) * 2000.0f;
    else if (max_val > 1000.0f) max_val = std::ceil(max_val / 1000.0f) * 1000.0f;
    else if (max_val > 400.0f) max_val = std::ceil(max_val / 200.0f) * 200.0f;
    else if (max_val > 100.0f) max_val = std::ceil(max_val / 50.0f) * 50.0f;
    else max_val = std::ceil(max_val / 20.0f) * 20.0f;
    if (max_val < 40.0f) max_val = 40.0f;

    // Draw horizontal grid lines & Y labels (4 intervals, color #333333)
    SelectObject(hdc, fonts.hFontAxis);
    SetTextColor(hdc, RGB(170, 170, 170));
    HPEN hPenGrid = CreatePen(PS_SOLID, 1, RGB(55, 55, 55));
    HGDIOBJ hOldPen = SelectObject(hdc, hPenGrid);

    for (int i = 0; i <= 4; ++i) {
        float frac = (float)i / 4.0f;
        int ly = py + ph - (int)(frac * ph);
        float cur_val = frac * max_val;

        MoveToEx(hdc, px, ly, nullptr);
        LineTo(hdc, px + pw, ly);

        std::wstring lbl = FormatChartValue(cur_val);
        RECT rcLbl = { cx + 2, ly - 8, px - 6, ly + 8 };
        DrawTextW(hdc, lbl.c_str(), -1, &rcLbl, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    // Draw vertical grid lines & X labels (every 100s)
    float time_step = 100.0f;
    if (max_time > 2400.0f) time_step = 200.0f;
    else if (max_time < 600.0f) time_step = 60.0f;

    for (float cur_time = 0.0f; cur_time <= max_time + 0.1f; cur_time += time_step) {
        int lx = px + (int)((cur_time / max_time) * pw);
        if (lx > px + pw) break;

        MoveToEx(hdc, lx, py, nullptr);
        LineTo(hdc, lx, py + ph);

        // Angled or centered X timestamp text
        std::wstring t_lbl = FormatChartSeconds(cur_time);
        if (fonts.hFontRotated) {
            SelectObject(hdc, fonts.hFontRotated);
            SetTextColor(hdc, RGB(170, 170, 170));
            TextOutW(hdc, lx - 8, py + ph + 24, t_lbl.c_str(), (int)t_lbl.length());
        } else {
            SelectObject(hdc, fonts.hFontAxis);
            SetTextColor(hdc, RGB(170, 170, 170));
            RECT rcTLbl = { lx - 20, py + ph + 4, lx + 20, py + ph + 18 };
            DrawTextW(hdc, t_lbl.c_str(), -1, &rcTLbl, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
    SelectObject(hdc, hOldPen);
    DeleteObject(hPenGrid);

    // Draw Data Polylines
    auto PlotSeries = [&](const std::vector<float>& times, const std::vector<float>& vals, COLORREF col) {
        if (times.empty() || vals.empty()) return;
        std::vector<POINT> pts;
        pts.reserve(times.size());
        for (size_t k = 0; k < times.size() && k < vals.size(); ++k) {
            float t = times[k];
            float v = vals[k];
            int sx = px + (int)((t / max_time) * pw);
            int sy = py + ph - (int)((v / max_val) * ph);
            if (sx < px) sx = px;
            if (sx > px + pw) sx = px + pw;
            if (sy < py) sy = py;
            if (sy > py + ph) sy = py + ph;
            pts.push_back({ sx, sy });
        }
        if (pts.size() >= 2) {
            HPEN hPen = CreatePen(PS_SOLID, 2, col);
            HGDIOBJ hOldPen2 = SelectObject(hdc, hPen);
            Polyline(hdc, pts.data(), static_cast<int>(pts.size()));
            SelectObject(hdc, hOldPen2);
            DeleteObject(hPen);
        }
    };

    PlotSeries(times1, vals1, color1);
    PlotSeries(times2, vals2, color2);
}

} // namespace sc2
