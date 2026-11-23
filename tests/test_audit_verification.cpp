#include "../src/replay/replay_parser.h"
#include "../src/utils/win32_utils.h"
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <cstdlib>
#ifdef NDEBUG
#undef NDEBUG // This executable is an audit; its assertions must run in release builds too.
#endif
#include <cassert>

static int GetCount(const std::vector<sc2::Contribution>& items, const std::string& key) {
    for (const auto& item : items) if (item.icon == key) return item.count;
    return -1;
}

struct AuditCase {
    const wchar_t* filename;
    const char* map;
    void (*verify)(const sc2::ReplayRecord&);
};

int main() {
    const AuditCase cases[] = {
        {L"克哈裂痕 (222).SC2Replay", "Rifts to Korhal", [](const auto& rec) {
            assert(rec.is_weekly);
            assert(rec.mutators.size() == 2);
            assert(GetCount(rec.p1.contributions, "minesweeper") == 88);
            assert(GetCount(rec.p1.contributions, "voidrifts") == 27);
            assert(GetCount(rec.p2.contributions, "minesweeper") == 82);
            assert(GetCount(rec.p2.contributions, "voidrifts") == 22);
            assert(rec.p2.contributions.size() >= 7); // 5 outlaws + 2 mutator counters
        }},
        {L"黑暗杀星 (181).SC2Replay", "Scythe of Amon", [](const auto& rec) {
            // The same WarpPrism entered phasing mode twice.
            assert(rec.bonus_earned == 2);
            assert(rec.bonus_total == 3);
        }},
        {L"黑暗杀星 (182).SC2Replay", "Scythe of Amon", [](const auto& rec) {
            // The WarpPrism entered phasing mode once.
            assert(rec.bonus_earned == 1);
            assert(rec.bonus_total == 3);
        }},
        {L"虚空撕裂 (141).SC2Replay", "Void Thrashing", [](const auto& rec) {
            assert(rec.p1.kills == 163);
            assert(GetCount(rec.p1.contributions, "Infested Bunker") == 8);
            assert(GetCount(rec.p1.contributions, "broodling") == 800);
            assert(GetCount(rec.p1.contributions, "mc") == 14);
        }},
        {L"熔火危机 (138).SC2Replay", "The Vermillion Problem", [](const auto& rec) {
            assert(rec.bonus_earned == 1);
            assert(rec.bonus_total == 1);
            assert(rec.p1.kills == 508);
            assert(rec.p2.kills == 101);
        }},
        {L"天界封锁 (129).SC2Replay", "Lock & Load", [](const auto& rec) {
            assert(rec.bonus_earned == 1);
            assert(rec.bonus_total == 1);
            assert(rec.p1.kills == 232);
            assert(rec.p2.kills == 281);
        }},
        {L"湮灭快车 (188).SC2Replay", "Oblivion Express", [](const auto& rec) {
            assert(rec.bonus_earned == 2);
            assert(rec.bonus_total == 2);
            assert(rec.p1.kills == 908);
            assert(rec.p2.kills == 576);
        }}
    };

    const wchar_t* directory = _wgetenv(L"SC2_AUDIT_REPLAY_DIR");
    if (!directory || !*directory) {
        std::cout << "[INFO] Private replay audit skipped (SC2_AUDIT_REPLAY_DIR is not set).\n";
        return 0;
    }
    std::error_code directory_error;
    if (!std::filesystem::is_directory(directory, directory_error)) {
        std::cerr << "[ERROR] SC2_AUDIT_REPLAY_DIR does not name an accessible directory.\n";
        return 1;
    }
    int verified_count = 0;
    for (const auto& audit : cases) {
        const auto path = std::filesystem::path(directory) / audit.filename;
        std::error_code ec;
        const bool exists = std::filesystem::exists(path, ec);
        if (ec) {
            std::cerr << "[ERROR] Cannot check " << sc2::Win32Utils::WideToUtf8(audit.filename)
                      << ": " << ec.message() << "\n";
            return 1;
        }
        if (!exists) {
            std::cout << "[SKIP] Missing " << sc2::Win32Utils::WideToUtf8(audit.filename) << "\n";
            continue;
        }

        auto rec = sc2::ReplayParser::ParseFile(path.wstring(), sc2::ReplayParseDepth::Full);
        if (!rec) {
            std::cerr << "[ERROR] Failed to parse " << sc2::Win32Utils::WideToUtf8(audit.filename) << "\n";
            return 1;
        }
        if (rec->map_name != audit.map) {
            std::cerr << "[ERROR] Unexpected map for " << sc2::Win32Utils::WideToUtf8(audit.filename)
                      << ": expected " << audit.map << ", got " << rec->map_name << "\n";
            return 1;
        }
        std::cout << "[AUDIT] Map: " << rec->map_name
                  << " | Date: " << rec->game_date
                  << " | Bonus: " << rec->bonus_earned << "/" << rec->bonus_total
                  << " | Kills: " << rec->p1.kills << "/" << rec->p2.kills << "\n";
        audit.verify(*rec);
        ++verified_count;
    }

    if (verified_count > 0) {
        std::cout << "Audit verification passed: verified " << verified_count << " real matches.\n";
    } else {
        std::cout << "[INFO] Private replay audit skipped (no private test replays present in local path).\n";
    }
    return 0;
}
