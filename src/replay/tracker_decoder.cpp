#include "s2_decoder.h"
#include "s2_proto_utils.h"
#include "../data/sc2_constants.h"
#include <algorithm>
#include <unordered_map>
#include <vector>

namespace sc2 {

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
    enum class Objective { Boss, Train };
    std::unordered_map<int64_t, Objective> objective_units;

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
        else if (event_id == 1 || event_id == 6 || event_id == 4) { // Born, init, or type change
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
            if (unit_type == "HybridDominatorCoopBoss") {
                objective_units[unit_id] = Objective::Boss;
            } else if (unit_type == "TarsonisEngine" || unit_type == "TarsonisEngineFast") {
                objective_units[unit_id] = Objective::Train;
            } else if (event_id == 4) {
                objective_units.erase(unit_id);
            }
            if (event_id == 4) continue;
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
            if (const auto objective = objective_units.find(unit_id); objective != objective_units.end()) {
                auto& deaths = objective->second == Objective::Boss ? summary.objectives.bosses : summary.objectives.trains;
                ++deaths.count;
                deaths.last_loop = current_gameloop;
                objective_units.erase(objective);
            }
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

    if (!buf.Done()) summary.objectives = {};
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

} // namespace sc2
