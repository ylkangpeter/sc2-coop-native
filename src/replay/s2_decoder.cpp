#include "s2_decoder.h"
#include "s2_initdata.h"
#include "../data/sc2_constants.h"
#include <string_view>
#include <cstring>
#include <algorithm>

namespace sc2 {

std::vector<DecodedSlot> S2Decoder::DecodeInitDataSlots(const std::vector<uint8_t>& initdata_bytes,
                                                        std::vector<std::string>& out_cache_handles,
                                                        std::vector<std::string>& out_mutators,
                                                        bool& out_is_weekly) {
    std::vector<DecodedSlot> slots;
    if (initdata_bytes.empty()) return slots;

    // 1. Try bit-packed initData decoder
    std::vector<DecodedInitSlot> raw_slots;
    if (S2InitDataDecoder::Decode(initdata_bytes, raw_slots, out_mutators, out_is_weekly) && !raw_slots.empty()) {
        for (const auto& rs : raw_slots) {
            DecodedSlot s;
            s.control = rs.control;
            s.commander = rs.commander;
            s.commander_level = rs.commander_level;
            s.commander_mastery_level = rs.commander_mastery_level;
            s.prestige = rs.prestige;
            s.brutal_plus = rs.brutal_plus;
            s.difficulty = rs.difficulty;
            s.masteries = rs.masteries;
            if (s.masteries.size() < 6) s.masteries.resize(6, 0);

            auto p_it = kPrestiges.find(s.commander);
            if (p_it != kPrestiges.end()) {
                if (s.prestige == 0) s.prestige_name = p_it->second.p0;
                else if (s.prestige == 1) s.prestige_name = p_it->second.p1;
                else if (s.prestige == 2) s.prestige_name = p_it->second.p2;
                else if (s.prestige == 3) s.prestige_name = p_it->second.p3;
            }
            slots.push_back(s);
        }
        return slots;
    }

    // 2. Fallback heuristic scanner
    const char* kKnownCommanders[] = {
        "Abathur", "Alarak", "Artanis", "Dehaka", "Fenix", "Horner", "HanHorner", "Karax",
        "Kerrigan", "Mengsk", "Nova", "Raynor", "Stetmann", "Stukov", "Swann",
        "Tychus", "Vorazun", "Zagara", "Zeratul"
    };

    // Extract all strings
    std::string cur;
    std::vector<std::string> tokens;
    for (uint8_t b : initdata_bytes) {
        if (b >= 32 && b <= 126) {
            cur.push_back(static_cast<char>(b));
        } else {
            if (cur.size() >= 3) {
                tokens.push_back(cur);
            }
            cur.clear();
        }
    }

    for (const auto& tok : tokens) {
        for (const char* cn : kKnownCommanders) {
            if (tok.find(cn) != std::string::npos) {
                DecodedSlot s;
                s.commander = cn;
                s.prestige = 0;

                if (tok.find("CommanderPrestige") == 0) {
                    std::string pres_part = tok.substr(17 + strlen(cn));
                    s.prestige = 1;
                    if (pres_part.find("2") != std::string::npos ||
                        pres_part.find("UltEvo") != std::string::npos ||
                        pres_part.find("EmpowerMe") != std::string::npos ||
                        pres_part.find("PowerField") != std::string::npos ||
                        pres_part.find("PackLeaders") != std::string::npos ||
                        pres_part.find("DataWeb") != std::string::npos ||
                        pres_part.find("RoughRider") != std::string::npos ||
                        pres_part.find("Clone") != std::string::npos) {
                        s.prestige = 2;
                    }
                    if (pres_part.find("3") != std::string::npos ||
                        pres_part.find("DeathFleet") != std::string::npos ||
                        pres_part.find("OrbitalStrikes") != std::string::npos ||
                        pres_part.find("RebelRaider") != std::string::npos ||
                        pres_part.find("Avenger") != std::string::npos) {
                        s.prestige = 3;
                    }
                }
                // Look up prestige name
                auto p_it = kPrestiges.find(s.commander);
                if (p_it != kPrestiges.end()) {
                    if (s.prestige == 0) s.prestige_name = p_it->second.p0;
                    else if (s.prestige == 1) s.prestige_name = p_it->second.p1;
                    else if (s.prestige == 2) s.prestige_name = p_it->second.p2;
                    else if (s.prestige == 3) s.prestige_name = p_it->second.p3;
                }

                // Extract masteries for commander
                s.masteries = ExtractMasteries(initdata_bytes, s.commander);

                slots.push_back(s);
                break;
            }
        }
        if (slots.size() >= 2) break; // Found both players
    }

    return slots;
}

std::vector<int> S2Decoder::ExtractMasteries(const std::vector<uint8_t>& initdata_bytes,
                                            const std::string& commander) {
    std::vector<int> out = {0, 0, 0, 0, 0, 0};
    if (initdata_bytes.size() < 25 || commander.empty()) return out;

    std::string_view data_sv(reinterpret_cast<const char*>(initdata_bytes.data()), initdata_bytes.size());
    size_t c_idx = data_sv.find(commander);
    if (c_idx == std::string_view::npos && commander == "HanHorner") {
        c_idx = data_sv.find("Horner");
    }
    if (c_idx == std::string_view::npos) return out;

    size_t max_scan = initdata_bytes.size() - 25;
    size_t scan_end = std::min(c_idx + 120, max_scan);
    for (size_t byte_start = c_idx; byte_start < scan_end; ++byte_start) {
        for (int bit_start = 0; bit_start < 8; ++bit_start) {
            BitBuffer buf(initdata_bytes.data() + byte_start, initdata_bytes.size() - byte_start, true);
            if (bit_start > 0) buf.ReadBits(bit_start);
            uint32_t len = buf.ReadBits(3);
            if (len == 6) {
                std::vector<int> candidate;
                int sum = 0;
                bool valid = true;
                for (int i = 0; i < 6; ++i) {
                    uint32_t val = buf.ReadBits(32);
                    if (val > 30) { valid = false; break; }
                    candidate.push_back(static_cast<int>(val));
                    sum += static_cast<int>(val);
                }
                if (valid && sum > 0 && sum <= 90) {
                    if (candidate[0] + candidate[1] <= 30 &&
                        candidate[2] + candidate[3] <= 30 &&
                        candidate[4] + candidate[5] <= 30) {
                        return candidate;
                    }
                }
            }
        }
    }
    return out;
}

} // namespace sc2
