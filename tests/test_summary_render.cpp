#include <windows.h>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>
#include "../src/i18n/i18n.h"
#include "../src/ui/hud/summary_overlay_render.h"

// Render directly into a memory DIB: no window, replay files or user database.
int main() {
    sc2::I18n::Init("zh_CN");
    for (int height : {720, 1080, 2160}) {
        int width = height;
        HDC dc = CreateCompatibleDC(nullptr);
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* data = nullptr;
        HBITMAP bitmap = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &data, nullptr, 0);
        assert(dc && bitmap && data);
        HGDIOBJ old = SelectObject(dc, bitmap);
        SetBkMode(dc, TRANSPARENT);
        auto* pixels = static_cast<uint32_t*>(data);
        std::fill(pixels, pixels + width * height, 0);
        HFONT font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
            DEFAULT_PITCH, L"Microsoft YaHei");
        sc2::SummaryOverlayFonts fonts;
        fonts.hFontTitle = fonts.hFontPlayerName = fonts.hFontTimestamp = font;
        fonts.hFontPrestige = fonts.hFontMap = fonts.hFontMastery = font;
        fonts.hFontUnit = fonts.hFontUnitBold = fonts.hFontChartTitle = fonts.hFontAxis = font;
        // The MULE image has red pixels in its 15x15 icon; the missing-image
        // fallback draws only blue text on the badge background. This catches a
        // broken asset path or failed image decode, not merely a nonempty DIB.
        auto icon_red_pixels = [&](const std::string& icon) {
            std::fill(pixels, pixels + width * height, 0);
            sc2::DrawSummarySection(dc, fonts, 30, 300, height, 0, 100,
                "Raynor", "P1", 0, "", {}, {}, {{icon, 1}}, 0,
                RGB(0, 128, 248), false, "");
            GdiFlush();
            int count = 0;
            for (int y = 153; y < 168; ++y) for (int x = 32; x < 47; ++x) {
                uint32_t c = pixels[y * width + x];
                int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
                if (r > g + 8 && r > b + 8) ++count;
            }
            return count;
        };
        assert(icon_red_pixels("MULE") >= 3);
        assert(icon_red_pixels("missing icon") == 0);
        std::fill(pixels, pixels + width * height, 0);
        sc2::ReplayRecord record;
        record.result = "Victory";
        record.map_name = "The Vermillion Problem";
        record.form_length = "25:34";
        record.bonus_earned = 2;
        record.bonus_total = 2;
        record.difficulty = "Brutal";
        record.is_weekly = true;
        record.mutators = {"Avenger", "Black Death", "Void Rifts"};
        record.p1.name = "Player 1";
        record.p2.name = "Player 2";
        record.p1.commander = "Raynor";
        record.p2.commander = "Tychus";
        record.p1.kills = 100;
        record.p2.kills = 60;
        record.p1.contributions = {{"MULE", 20}, {"voidrifts", 3}, {"killbots", 4}};
        record.p2.contributions = {{"TychusCoop", 0}, {"TychusReaper", 0}, {"TychusMedic", 0}, {"missing icon", 5}};
        record.p1.units = {{"Marine", 30, 10, 100, 1.0f}, {"Medic", 3, 0, 0, 0.0f}};
        int rx = width / 2, rw = width / 2 - 10;
        // Compare the colored glyphs with an unclipped reference at the same
        // font size. A banner can render successfully while dropping a label.
        auto render_banner = [&](int left, int banner_width) {
            std::fill(pixels, pixels + width * height, 0);
            sc2::DrawSummaryBanner(dc, pixels, width, height, fonts, record, left, banner_width,
                                   true, true, 3, 4, "Tychus", "Lone Wolf");
            GdiFlush();
            int top = int(height * 0.076f) + int(height * 0.040f) + int(height * 0.008f);
            int line = sc2::SummaryHeaderLineHeight(height);
            auto gold_pixels = [&](int y, uint32_t color) {
                return std::count_if(pixels + y * width, pixels + (y + line) * width,
                    [color](uint32_t pixel) { return (pixel & 0xffffff) == color; });
            };
            return std::make_pair(gold_pixels(top, 0xffe670), gold_pixels(top + line, 0xffc000));
        };
        auto reference = render_banner(0, width);
        assert(reference.first > 0 && reference.second > 0);
        auto actual = render_banner(rx, rw);
        assert(actual == reference);
        sc2::DrawSummarySection(dc, fonts, rx, rw, height, 160, height / 4,
            record.p1.commander, record.p1.name, 1, "", {30, 0, 30, 0, 30, 0},
            record.p1.units, record.p1.contributions, 100, RGB(0, 128, 248), false, "");
        sc2::DrawSummarySection(dc, fonts, rx, rw, height, 160, height / 2,
            record.p2.commander, record.p2.name, 0, "", {}, {}, record.p2.contributions,
            60, RGB(0, 213, 50), false, "");
        sc2::DrawSummarySection(dc, fonts, rx, rw, height, 160, height * 3 / 4,
            "", "", 0, "", {}, {{"Zergling", 1, 1, 25, 0.25f}}, {},
            0, RGB(255, 51, 51), true, "");
        sc2::DrawSummaryChart(dc, fonts, 0, 0, width / 2 - 10, height / 4,
            L"Empty", 0, {}, {}, {}, {}, RGB(0, 128, 248), RGB(0, 213, 50));
        GdiFlush();
        auto changed = std::count_if(pixels, pixels + width * height, [](uint32_t p) { return (p & 0xffffff) != 0; });
        assert(changed > 100);
        // An empty graph must still draw its centered message.
        int center = height / 8;
        bool empty_message = false;
        for (int y = center - 15; y <= center + 15; ++y)
            for (int x = 55; x < width / 2 - 25; ++x)
                empty_message |= (pixels[y * width + x] & 0xffffff) != 0;
        assert(empty_message);
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
        DeleteObject(font);
        std::cout << "Rendered summary at " << height << "p\n";
    }
}
