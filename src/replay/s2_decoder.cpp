#include "s2_decoder.h"
#include "s2_initdata.h"
#include "../data/sc2_constants.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <cmath>

namespace sc2 {

static void SkipValue(BitBuffer& buf, uint32_t val_type) {
    switch (val_type) {
        case 0: { // array
            int64_t count = buf.ReadVInt();
            for (int64_t i = 0; i < count && !buf.Done(); ++i) {
                if (buf.Done()) break;
                uint32_t elem_type = buf.ReadBits(8);
                SkipValue(buf, elem_type);
            }
            break;
        }
        case 1: { // bitblob
            int64_t bitlen = buf.ReadVInt();
            if (bitlen > 0) {
                buf.ReadAlignedBytes(static_cast<size_t>((bitlen + 7) / 8));
            }
            break;
        }
        case 2: { // blob
            int64_t len = buf.ReadVInt();
            if (len > 0) {
                buf.ReadAlignedBytes(static_cast<size_t>(len));
            }
            break;
        }
        case 3: { // choice
            buf.ReadVInt(); // tag
            uint32_t elem_type = buf.ReadBits(8);
            SkipValue(buf, elem_type);
            break;
        }
        case 4: { // optional
            bool exists = buf.ReadBool();
            if (exists) {
                uint32_t elem_type = buf.ReadBits(8);
                SkipValue(buf, elem_type);
            }
            break;
        }
        case 5: { // struct
            int64_t field_count = buf.ReadVInt();
            for (int64_t i = 0; i < field_count && !buf.Done(); ++i) {
                buf.ReadVInt(); // tag
                uint32_t elem_type = buf.ReadBits(8);
                SkipValue(buf, elem_type);
            }
            break;
        }
        case 6: // u8
            buf.ReadU8();
            break;
        case 7: // u32 / fourcc / real32
            buf.ReadU32();
            break;
        case 8: // u64 / real64
            buf.ReadU64();
            break;
        case 9: // vint
            buf.ReadVInt();
            break;
        default:
            break;
    }
}

void S2Decoder::SkipType(BitBuffer& buf) {
    if (buf.Done()) return;
    uint32_t type = buf.ReadBits(8);
    SkipValue(buf, type);
}

std::vector<DecodedPlayer> S2Decoder::DecodeDetails(const std::vector<uint8_t>& details_bytes) {
    std::vector<DecodedPlayer> players;
    if (details_bytes.empty()) return players;

    BitBuffer buf(details_bytes);
    uint32_t top_type = buf.ReadBits(8);
    if (top_type != 5) return players; // Must be struct

    int64_t root_fields = buf.ReadVInt();
    for (int64_t f = 0; f < root_fields && !buf.Done(); ++f) {
        int64_t root_tag = buf.ReadVInt();
        uint32_t list_type = buf.ReadBits(8);

        if (root_tag == 0) { // m_playerList
            if (list_type == 4) { // optional!
                bool exists = buf.ReadBool();
                if (!exists) continue;
                list_type = buf.ReadBits(8); // array type
            }

            if (list_type == 0) { // array
                int64_t player_count = buf.ReadVInt();
                for (int64_t p = 0; p < player_count && !buf.Done(); ++p) {
                    uint32_t p_type = buf.ReadBits(8);
                    if (p_type != 5) {
                        SkipValue(buf, p_type);
                        continue;
                    }

                    DecodedPlayer player;
                    player.player_id = static_cast<int>(p + 1);

                    int64_t p_fields = buf.ReadVInt();
                    for (int64_t pf = 0; pf < p_fields && !buf.Done(); ++pf) {
                        int64_t pf_tag = buf.ReadVInt();
                        uint32_t val_type = buf.ReadBits(8);

                        if (pf_tag == 0 && val_type == 2) { // m_name
                            int64_t len = buf.ReadVInt();
                            player.name = buf.ReadString(static_cast<size_t>(len));
                        } else if (pf_tag == 1 && val_type == 5) { // m_toon
                            int64_t toon_fields = buf.ReadVInt();
                            int region_id = 0;
                            uint64_t toon_id = 0;
                            uint32_t realm_id = 0;

                            for (int64_t tf = 0; tf < toon_fields && !buf.Done(); ++tf) {
                                int64_t t_tag = buf.ReadVInt();
                                uint32_t t_type = buf.ReadBits(8);
                                if (t_tag == 0 && t_type == 9) { // m_region
                                    region_id = static_cast<int>(buf.ReadVInt());
                                } else if (t_tag == 2 && t_type == 9) { // m_realm
                                    realm_id = static_cast<uint32_t>(buf.ReadVInt());
                                } else if (t_tag == 4 && (t_type == 7 || t_type == 9)) { // m_id
                                    toon_id = (t_type == 7) ? buf.ReadU32() : static_cast<uint64_t>(buf.ReadVInt());
                                } else {
                                    SkipValue(buf, t_type);
                                }
                            }
                            if (region_id == 1) player.region = "NA";
                            else if (region_id == 2) player.region = "EU";
                            else if (region_id == 3) player.region = "KR";
                            else if (region_id == 5) player.region = "CN";
                            else player.region = "Global";

                            std::ostringstream oss;
                            oss << region_id << "-S2-" << realm_id << "-" << toon_id;
                            player.handle = oss.str();
                        } else if (pf_tag == 2 && val_type == 2) { // m_race
                            int64_t len = buf.ReadVInt();
                            player.race = buf.ReadString(static_cast<size_t>(len));
                        } else if (pf_tag == 7 && val_type == 9) { // m_observe
                            player.is_observer = (buf.ReadVInt() != 0);
                        } else if (pf_tag == 8 && val_type == 9) { // m_result
                            player.result = static_cast<int>(buf.ReadVInt());
                        } else {
                            SkipValue(buf, val_type);
                        }
                    }
                    players.push_back(player);
                }
            } else {
                SkipValue(buf, list_type);
            }
        } else {
            SkipValue(buf, list_type);
        }
    }

    return players;
}

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
            s.commander = rs.commander;
            s.commander_level = rs.commander_level;
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
        "Abathur", "Alarak", "Artanis", "Dehaka", "Fenix", "Horner", "Karax",
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
    if (initdata_bytes.empty() || commander.empty()) return out;

    std::string_view data_sv(reinterpret_cast<const char*>(initdata_bytes.data()), initdata_bytes.size());
    size_t c_idx = data_sv.find(commander);
    if (c_idx == std::string_view::npos) return out;

    size_t scan_end = std::min(c_idx + 120, initdata_bytes.size() - 25);
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

TrackerSummary S2Decoder::DecodeTrackerEvents(const std::vector<uint8_t>& tracker_bytes,
                                              int p1_total_kills, int p2_total_kills,
                                              const std::string& p1_cmdr, const std::string& p2_cmdr) {
    TrackerSummary summary;
    if (tracker_bytes.empty()) return summary;

    BitBuffer buf(tracker_bytes.data(), tracker_bytes.size(), true);
    int current_gameloop = 0;

    struct UnitInfo {
        std::string type;
        int owner = 0;
    };
    std::unordered_map<int64_t, UnitInfo> live_units;

    struct StatCounter {
        int created = 0;
        int lost = 0;
        int kills = 0;
    };
    std::unordered_map<std::string, StatCounter> p1_unit_stats;
    std::unordered_map<std::string, StatCounter> p2_unit_stats;
    std::unordered_map<std::string, StatCounter> amon_unit_stats;

    int current_wave_second = -1;
    std::vector<std::string> current_wave_units;
    std::vector<std::vector<std::string>> identified_waves;

    int p1_kills = 0;
    int p2_kills = 0;

    while (!buf.Done() && buf.RemainingBytes() > 0) {
        // Gameloop delta (choice type 3)
        uint32_t c_type = buf.ReadBits(8);
        if (c_type != 3) {
            break;
        }
        int64_t choice_tag = buf.ReadVInt();
        (void)choice_tag;
        uint32_t vint_type = buf.ReadBits(8);
        (void)vint_type;
        int64_t delta = buf.ReadVInt();
        current_gameloop += static_cast<int>(delta);

        // Event ID (vint type 9)
        uint32_t eid_type = buf.ReadBits(8);
        if (eid_type != 9) break;
        int64_t event_id = buf.ReadVInt();

        // Event Struct (type 5)
        uint32_t struct_type = buf.ReadBits(8);
        if (struct_type != 5) {
            SkipValue(buf, struct_type);
            continue;
        }
        int64_t field_count = buf.ReadVInt();

        if (event_id == 0) { // SPlayerStatsEvent
            int player_id = 0;
            float food_used = 0.0f;
            float collection_rate = 0.0f;
            float army_value = 0.0f;

            for (int64_t f = 0; f < field_count && !buf.Done(); ++f) {
                int64_t tag = buf.ReadVInt();
                uint32_t val_type = buf.ReadBits(8);
                if (tag == 0 && val_type == 9) { // m_playerId
                    player_id = static_cast<int>(buf.ReadVInt());
                } else if (tag == 1 && val_type == 5) { // m_stats
                    int64_t stats_fc = buf.ReadVInt();
                    for (int64_t sf = 0; sf < stats_fc && !buf.Done(); ++sf) {
                        int64_t stag = buf.ReadVInt();
                        uint32_t stype = buf.ReadBits(8);
                        if (stag == 2 && stype == 9) {
                            collection_rate += static_cast<float>(buf.ReadVInt());
                        } else if (stag == 3 && stype == 9) {
                            collection_rate += static_cast<float>(buf.ReadVInt());
                        } else if (stag == 11 && stype == 9) {
                            army_value += static_cast<float>(buf.ReadVInt());
                        } else if (stag == 14 && stype == 9) {
                            army_value += static_cast<float>(buf.ReadVInt());
                        } else if (stag == 29 && stype == 9) {
                            food_used = static_cast<float>(buf.ReadVInt()) / 4096.0f;
                        } else {
                            SkipValue(buf, stype);
                        }
                    }
                } else {
                    SkipValue(buf, val_type);
                }
            }

            float time_sec = current_gameloop / 16.0f;
            if (player_id == 1) {
                summary.p1_series.time_stamps.push_back(time_sec);
                summary.p1_series.army_value.push_back(army_value);
                summary.p1_series.supply_used.push_back(food_used);
                summary.p1_series.kill_count.push_back(static_cast<float>(p1_kills));
                summary.p1_series.collection_rate.push_back(collection_rate);
            } else if (player_id == 2) {
                summary.p2_series.time_stamps.push_back(time_sec);
                summary.p2_series.army_value.push_back(army_value);
                summary.p2_series.supply_used.push_back(food_used);
                summary.p2_series.kill_count.push_back(static_cast<float>(p2_kills));
                summary.p2_series.collection_rate.push_back(collection_rate);
            }
        }
        else if (event_id == 1 || event_id == 6) { // SUnitBornEvent or SUnitInitEvent
            int64_t tag_idx = 0;
            int64_t tag_recycle = 0;
            std::string unit_type;
            int control_pid = 0;

            for (int64_t f = 0; f < field_count && !buf.Done(); ++f) {
                int64_t tag = buf.ReadVInt();
                uint32_t val_type = buf.ReadBits(8);
                if (tag == 0 && (val_type == 7 || val_type == 9)) {
                    tag_idx = (val_type == 7) ? buf.ReadU32() : buf.ReadVInt();
                } else if (tag == 1 && (val_type == 7 || val_type == 9)) {
                    tag_recycle = (val_type == 7) ? buf.ReadU32() : buf.ReadVInt();
                } else if (tag == 2 && val_type == 2) {
                    int64_t len = buf.ReadVInt();
                    unit_type = buf.ReadString(static_cast<size_t>(len));
                } else if (tag == 3 && val_type == 9) {
                    control_pid = static_cast<int>(buf.ReadVInt());
                } else {
                    SkipValue(buf, val_type);
                }
            }

            int64_t unit_id = (tag_idx << 18) | tag_recycle;
            live_units[unit_id] = { unit_type, control_pid };

            if (control_pid == 1) {
                p1_unit_stats[unit_type].created++;
            } else if (control_pid == 2) {
                p2_unit_stats[unit_type].created++;
            } else if (control_pid >= 3) {
                amon_unit_stats[unit_type].created++;

                float time_sec = current_gameloop / 16.0f;
                int second = static_cast<int>(time_sec);
                if (second > 60 && kUnitsInWaves.find(unit_type) != kUnitsInWaves.end()) {
                    if (second == current_wave_second) {
                        current_wave_units.push_back(unit_type);
                    } else {
                        if (current_wave_units.size() >= 4) {
                            identified_waves.push_back(current_wave_units);
                        }
                        current_wave_second = second;
                        current_wave_units = { unit_type };
                    }
                }
            }
        }
        else if (event_id == 2) { // SUnitDiedEvent
            int64_t tag_idx = 0;
            int64_t tag_recycle = 0;
            int killer_pid = 0;
            int64_t killer_idx = 0;
            int64_t killer_recycle = 0;

            for (int64_t f = 0; f < field_count && !buf.Done(); ++f) {
                int64_t tag = buf.ReadVInt();
                uint32_t val_type = buf.ReadBits(8);
                if (tag == 0 && (val_type == 7 || val_type == 9)) {
                    tag_idx = (val_type == 7) ? buf.ReadU32() : buf.ReadVInt();
                } else if (tag == 1 && (val_type == 7 || val_type == 9)) {
                    tag_recycle = (val_type == 7) ? buf.ReadU32() : buf.ReadVInt();
                } else if (tag == 2) { // m_killerPlayerId
                    if (val_type == 4) {
                        bool exists = buf.ReadBool();
                        if (exists) {
                            uint32_t t2 = buf.ReadBits(8);
                            if (t2 == 9) killer_pid = static_cast<int>(buf.ReadVInt());
                            else SkipValue(buf, t2);
                        }
                    } else if (val_type == 9) {
                        killer_pid = static_cast<int>(buf.ReadVInt());
                    } else {
                        SkipValue(buf, val_type);
                    }
                } else if (tag == 5) { // m_killerUnitTagIndex
                    if (val_type == 4) {
                        bool exists = buf.ReadBool();
                        if (exists) {
                            uint32_t t2 = buf.ReadBits(8);
                            if (t2 == 7) killer_idx = buf.ReadU32();
                            else if (t2 == 9) killer_idx = buf.ReadVInt();
                            else SkipValue(buf, t2);
                        }
                    } else if (val_type == 7) {
                        killer_idx = buf.ReadU32();
                    } else if (val_type == 9) {
                        killer_idx = buf.ReadVInt();
                    } else {
                        SkipValue(buf, val_type);
                    }
                } else if (tag == 6) { // m_killerUnitTagRecycle
                    if (val_type == 4) {
                        bool exists = buf.ReadBool();
                        if (exists) {
                            uint32_t t2 = buf.ReadBits(8);
                            if (t2 == 7) killer_recycle = buf.ReadU32();
                            else if (t2 == 9) killer_recycle = buf.ReadVInt();
                            else SkipValue(buf, t2);
                        }
                    } else if (val_type == 7) {
                        killer_recycle = buf.ReadU32();
                    } else if (val_type == 9) {
                        killer_recycle = buf.ReadVInt();
                    } else {
                        SkipValue(buf, val_type);
                    }
                } else {
                    SkipValue(buf, val_type);
                }
            }

            int64_t unit_id = (tag_idx << 18) | tag_recycle;
            auto it = live_units.find(unit_id);
            if (it != live_units.end()) {
                const auto& uinfo = it->second;
                if (uinfo.owner == 1) {
                    p1_unit_stats[uinfo.type].lost++;
                } else if (uinfo.owner == 2) {
                    p2_unit_stats[uinfo.type].lost++;
                } else if (uinfo.owner >= 3) {
                    amon_unit_stats[uinfo.type].lost++;
                }

                int64_t killer_uid = (killer_idx << 18) | killer_recycle;
                auto kit = live_units.find(killer_uid);
                std::string killer_type;
                if (kit != live_units.end()) {
                    killer_type = kit->second.type;
                }

                if (killer_pid == 1 && uinfo.owner >= 3) {
                    p1_kills++;
                    if (!killer_type.empty()) {
                        p1_unit_stats[killer_type].kills++;
                    }
                } else if (killer_pid == 2 && uinfo.owner >= 3) {
                    p2_kills++;
                    if (!killer_type.empty()) {
                        p2_unit_stats[killer_type].kills++;
                    }
                } else if (killer_pid >= 3 && (uinfo.owner == 1 || uinfo.owner == 2)) {
                    if (!killer_type.empty()) {
                        amon_unit_stats[killer_type].kills++;
                    }
                }
            }
        }
        else {
            // Other events
            for (int64_t f = 0; f < field_count && !buf.Done(); ++f) {
                buf.ReadVInt();
                uint32_t val_type = buf.ReadBits(8);
                SkipValue(buf, val_type);
            }
        }
    }

    if (current_wave_units.size() >= 4) {
        identified_waves.push_back(current_wave_units);
    }
    summary.enemy_comp = IdentifyEnemyComposition(identified_waves);

    // Filter and sort units
    auto ProcessUnits = [](const std::unordered_map<std::string, StatCounter>& stats_map, int total_kills) {
        std::vector<UnitStat> out;
        for (const auto& [raw_name, st] : stats_map) {
            if (raw_name.find("Placement") != std::string::npos ||
                raw_name.find("Placeholder") != std::string::npos ||
                raw_name.find("Dummy") != std::string::npos ||
                raw_name.find("Beacon") != std::string::npos ||
                raw_name.find("Cocoon") != std::string::npos ||
                raw_name.find("DropPod") != std::string::npos ||
                raw_name.find("Pickup") != std::string::npos ||
                raw_name.find("Weapon") != std::string::npos ||
                raw_name.find("Missile") != std::string::npos ||
                raw_name.find("Targeter") != std::string::npos) {
                continue;
            }
            if (st.created == 0 && st.lost == 0 && st.kills == 0) continue;

            UnitStat us;
            us.name = FormatFriendlyUnitName(raw_name);
            us.created = st.created;
            us.lost = st.lost;
            us.kills = st.kills;
            us.kill_fraction = (total_kills > 0) ? (static_cast<float>(st.kills) / total_kills) : 0.0f;
            out.push_back(us);
        }

        std::sort(out.begin(), out.end(), [](const UnitStat& a, const UnitStat& b) {
            if (a.kills != b.kills) return a.kills > b.kills;
            if (a.created != b.created) return a.created > b.created;
            return a.lost > b.lost;
        });

        if (out.size() > 5) out.resize(5);
        return out;
    };

    summary.p1_units = ProcessUnits(p1_unit_stats, p1_total_kills > 0 ? p1_total_kills : p1_kills);
    summary.p2_units = ProcessUnits(p2_unit_stats, p2_total_kills > 0 ? p2_total_kills : p2_kills);

    int amon_total_kills = 0;
    for (const auto& [name, st] : amon_unit_stats) amon_total_kills += st.kills;
    summary.amon_units = ProcessUnits(amon_unit_stats, amon_total_kills > 0 ? amon_total_kills : 1);

    auto SmoothSeries = [](PlayerTimeSeries& s) {
        if (s.collection_rate.size() <= 1) return;
        for (size_t i = s.collection_rate.size() - 1; i > 0; --i) {
            s.collection_rate[i] = 0.5f * s.collection_rate[i] + 0.5f * s.collection_rate[i - 1];
        }
    };
    SmoothSeries(summary.p1_series);
    SmoothSeries(summary.p2_series);

    summary.p1_kills = p1_kills;
    summary.p2_kills = p2_kills;

    return summary;
}

std::vector<DecodedChatMessage> S2Decoder::DecodeMessageEvents(const std::vector<uint8_t>& msg_bytes) {
    std::vector<DecodedChatMessage> messages;
    if (msg_bytes.empty()) return messages;

    BitBuffer buf(msg_bytes);
    int current_gameloop = 0;

    while (!buf.Done() && buf.RemainingBytes() > 0) {
        // 1. svaruint32 gameloop delta (choice of 2 bits tag, followed by 6, 14, 22, or 32 bits)
        uint32_t tag = buf.ReadBits(2);
        uint32_t delta = 0;
        if (tag == 0) {
            delta = buf.ReadBits(6);
        } else if (tag == 1) {
            delta = buf.ReadBits(14);
        } else if (tag == 2) {
            delta = buf.ReadBits(22);
        } else if (tag == 3) {
            delta = buf.ReadBits(32);
        }
        current_gameloop += static_cast<int>(delta);

        // 2. replay_userid (5 bits)
        uint32_t user_id = buf.ReadBits(5);

        // 3. message_eventid (4 bits)
        uint32_t event_id = buf.ReadBits(4);

        if (event_id == 0) { // NNet.Game.SChatMessage
            uint32_t recipient = buf.ReadBits(3);
            uint32_t text_len = buf.ReadBits(11);
            buf.ByteAlign();
            std::string text = buf.ReadString(text_len);
            buf.ByteAlign();

            if (!text.empty()) {
                DecodedChatMessage msg;
                msg.gameloop = current_gameloop;
                msg.time_seconds = current_gameloop / 16.0;
                msg.player_id = static_cast<int>(user_id + 1);
                msg.text = text;
                messages.push_back(msg);
            }
        } else if (event_id == 1) { // NNet.Game.SPingMessage
            uint32_t recipient = buf.ReadBits(3);
            buf.ReadBits(32); // point.x
            buf.ReadBits(32); // point.y
            buf.ByteAlign();
        } else if (event_id == 2) { // NNet.Game.SLoadingProgressMessage
            buf.ReadBits(32); // progress
            buf.ByteAlign();
        } else if (event_id == 3) { // NNet.Game.SServerPingMessage
            // empty struct
            buf.ByteAlign();
        } else if (event_id == 4) { // NNet.Game.SReconnectNotifyMessage
            buf.ReadBits(2); // status
            buf.ByteAlign();
        } else {
            // Unknown event id, terminate loop safely
            break;
        }
    }

    return messages;
}

} // namespace sc2
