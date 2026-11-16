#pragma once

#include <windows.h>
#include <string>
#include <vector>

namespace sc2 {

struct SummaryOverlayFonts {
    HFONT hFontTitle = nullptr;
    HFONT hFontPlayerName = nullptr;
    HFONT hFontPrestige = nullptr;
    HFONT hFontMap = nullptr;
    HFONT hFontMastery = nullptr;
    HFONT hFontUnit = nullptr;
    HFONT hFontUnitBold = nullptr;
    HFONT hFontChartTitle = nullptr;
    HFONT hFontAxis = nullptr;
    HFONT hFontRotated = nullptr;
};

std::wstring FormatChartSeconds(float sec);
std::wstring FormatChartValue(float val);

void DrawSummaryChart(HDC hdc, const SummaryOverlayFonts& fonts,
                      int cx, int cy, int cw, int ch,
                      const std::wstring& title, float duration_seconds,
                      const std::vector<float>& times1, const std::vector<float>& vals1,
                      const std::vector<float>& times2, const std::vector<float>& vals2,
                      COLORREF color1, COLORREF color2);

} // namespace sc2
