#include "s2_decoder.h"
#include "s2_proto_utils.h"
#include "tracker_army_value.h"
#include "tracker_enemy.h"
#include "mutator_ids.h"
#include "objective_contribution_tracker.h"
#include <cmath>
#include <unordered_set>
#include "../data/sc2_constants.h"
#include <algorithm>
#include <unordered_map>
#include <vector>

namespace sc2 {

TrackerSummary S2Decoder::DecodeTrackerEvents(const std::vector<uint8_t>& tracker_bytes,
                                              const PlayerInfo& p1, const PlayerInfo& p2,
                                              const std::string& map_name, const std::vector<DroneOrder>& drones, int end_loop) {
    TrackerSummary summary;
    if (tracker_bytes.empty()) return summary;

    BitBuffer buf(tracker_bytes.data(), tracker_bytes.size(), true);
    int current_gameloop = 0;
    TrackerArmyValue army1(p1), army2(p2);
    ObjectiveContributionTracker contribution(map_name);
    size_t drone_index = 0;
    std::unordered_set<int64_t> drone_targets;
    int amon_kills = 0;
    std::unordered_set<int64_t> biomass_locusts, broodlord_broodlings, glevig_spawns, murvar_spawns;
    std::unordered_set<int64_t> mind_controlled, dummy_zerglings, dragged_units;
    int last_biomass_x = -1, last_biomass_y = -1, last_biomass_loop = -1;
    int archon_merges[17] = {};
    auto is_enemy = [&](int owner) { return IsTrackerEnemy(map_name, owner); };

    struct UnitInfo {
        std::string type;
        int owner = 0;
        bool completed = true;
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

        while (drone_index < drones.size() && drones[drone_index].loop <= current_gameloop) {
            auto target = live_units.find(drones[drone_index].target);
            if (target != live_units.end() && target->second.completed &&
                (target->second.type.find("Refinery") != std::string::npos || target->second.type.find("Assimilator") != std::string::npos || target->second.type.find("Extractor") != std::string::npos))
                drone_targets.insert(target->first);
            ++drone_index;
        }
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
        if (end_loop >= 0 && current_gameloop > end_loop) {
            for (int64_t f = 0; f < field_count && !buf.Done(); ++f) {
                buf.ReadVInt();
                SkipValue(buf, buf.ReadBits(8));
            }
            continue;
        }

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

            if (summary.start_loop < 0 && (player_id == 1 || player_id == 2) && collection_rate > 0) summary.start_loop = current_gameloop;
            collection_rate += static_cast<float>(drone_targets.size()) * 19.055f;
            float time_sec = current_gameloop / 16.0f;
            if (player_id == 1) {
                summary.p1_series.time_stamps.push_back(time_sec);
                summary.p1_series.army_value.push_back(army1.Value());
                summary.p1_series.supply_used.push_back(food_used);
                summary.p1_series.kill_count.push_back(static_cast<float>(p1_kills));
                summary.p1_series.collection_rate.push_back(collection_rate);
            } else if (player_id == 2) {
                summary.p2_series.time_stamps.push_back(time_sec);
                summary.p2_series.army_value.push_back(army2.Value());
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
            std::string creator_ability;
            int position_x = 0, position_y = 0;
            int64_t creator_index = 0, creator_recycle = 0;
            bool has_creator = false;

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
                } else if ((tag == 5 || tag == 6) && val_type == 9) {
                    int value = static_cast<int>(buf.ReadVInt());
                    if (tag == 5) position_x = value; else position_y = value;
                } else if ((tag == 7 || tag == 8) && val_type == 4) {
                    if (buf.ReadBool()) {
                        auto t = buf.ReadBits(8);
                        if (t == 7 || t == 9) {
                            int64_t value = t == 7 ? buf.ReadU32() : buf.ReadVInt();
                            if (tag == 7) { creator_index = value; has_creator = true; }
                            else creator_recycle = value;
                        } else SkipValue(buf, t);
                    }
                } else if (tag == 9 && val_type == 4) {
                    if (buf.ReadBool()) { auto t=buf.ReadBits(8); if (t==2) { auto n=buf.ReadVInt(); creator_ability=buf.ReadString(static_cast<size_t>(n)); } else SkipValue(buf,t); }
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
            if (event_id == 4) {
                contribution.TypeChange(unit_id, unit_type, current_gameloop);
                auto old = live_units.find(unit_id);
                if (old != live_units.end()) {
                    const auto previous = old->second.type;
                    static const std::unordered_set<std::string> ignored = {"HydraliskLurker","MutaliskBroodlord","RoachVile","Mutalisk","Devourer","GuardianMP","Viper","SwarmHost","Queen"};
                    if (!ignored.count(unit_type)) {
                        if (previous == "BanelingCocoon" && unit_type == "HotSSwarmling") {
                            dummy_zerglings.insert(unit_id);
                            old->second.type = unit_type;
                            continue;
                        }
                        auto a=tracker_reference::unit_names.find(previous), b=tracker_reference::unit_names.find(unit_type);
                        bool created=a!=tracker_reference::unit_names.end() && b!=tracker_reference::unit_names.end() && a->second!=b->second && unit_type!="SCVMengsk" && unit_type!="TrooperMengsk" && unit_type!="Hellion" && unit_type!="HellionTank";
                        auto& stats=old->second.owner==1?p1_unit_stats:old->second.owner==2?p2_unit_stats:amon_unit_stats;
                        if (created) ++stats[unit_type].created;
                        if (old->second.owner==1) army1.Morph(previous,unit_type,created);
                        if (old->second.owner==2) army2.Morph(previous,unit_type,created);
                        old->second.type=unit_type;
                    }
                }
                continue;
            }
            contribution.Born(unit_id, unit_type, control_pid, current_gameloop, creator_ability);
            if (unit_type == "BiomassPickup") {
                last_biomass_x = position_x;
                last_biomass_y = position_y;
                last_biomass_loop = current_gameloop;
            } else if (unit_type == "Locust" && current_gameloop == last_biomass_loop &&
                       position_x == last_biomass_x && position_y == last_biomass_y) {
                biomass_locusts.insert(unit_id);
            }
            if (unit_type == "CoopDehakaGlevigEggZergling" || unit_type == "CoopDehakaGlevigEggRoach" ||
                unit_type == "CoopDehakaGlevigEggHydralisk") glevig_spawns.insert(unit_id);
            if ((unit_type == "DehakaLocust" || unit_type == "DehakaCreeperFlying" ||
                 unit_type == "DehakaLocustFlying" || unit_type == "DehakaCreeper") &&
                creator_ability == "CoopMurvarSpawnCreepers") murvar_spawns.insert(unit_id);
            if ((unit_type == "Broodling" || unit_type == "BroodlingStetmann") && has_creator) {
                auto creator = live_units.find((creator_index << 18) | creator_recycle);
                if (creator != live_units.end() && (creator->second.type == "BroodlingEscort" ||
                    creator->second.type == "BroodlingEscortStetmann")) broodlord_broodlings.insert(unit_id);
            }
            if (creator_ability == "MutatorAmonDehakaDrag") dragged_units.insert(unit_id);
            if (event_id == 6 && unit_type == "Archon" && control_pid >= 0 && control_pid < 17)
                archon_merges[control_pid] += 2;
            static const std::unordered_map<std::string, std::string> revival_types = {
                {"KerriganReviveCocoon", "K5Kerrigan"}, {"AlarakReviveBeacon", "AlarakCoop"},
                {"ZagaraReviveCocoon", "ZagaraVoidCoop"}, {"DehakaCoopReviveCocoonFootPrint", "DehakaCoop"},
                {"NovaReviveBeacon", "NovaCoop"}, {"ZeratulCoopReviveBeacon", "ZeratulCoop"}};
            auto revival = revival_types.find(unit_type);
            if (revival != revival_types.end() && current_gameloop > 16 && (control_pid == 1 || control_pid == 2)) {
                auto& stats = control_pid == 1 ? p1_unit_stats : p2_unit_stats;
                ++stats[revival->second].created;
                ++stats[revival->second].lost;
            }
            live_units[unit_id] = { unit_type, control_pid, event_id == 1 };
            bool excluded_spawn = glevig_spawns.count(unit_id) || murvar_spawns.count(unit_id) || broodlord_broodlings.count(unit_id);
            if (!excluded_spawn && control_pid == 1) army1.Born(unit_type,creator_ability);
            if (!excluded_spawn && control_pid == 2) army2.Born(unit_type,creator_ability);

            if (excluded_spawn || dragged_units.count(unit_id)) {
                // These are temporary spawns, already credited to their parent.
            } else if (control_pid == 1) {
                p1_unit_stats[unit_type].created++;
            } else if (control_pid == 2) {
                p2_unit_stats[unit_type].created++;
            } else if (is_enemy(control_pid)) {
                amon_unit_stats[unit_type].created++;

                float time_sec = current_gameloop / 16.0f;
                int second = current_gameloop;
                if (time_sec > 60 && control_pid <= 6 && kUnitsInWaves.find(unit_type) != kUnitsInWaves.end()) {
                    if (second == current_wave_second) {
                        current_wave_units.push_back(unit_type);
                    } else {
                        if (current_wave_units.size() >= 6) {
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
            bool has_killer_unit = false;
            int x = 0, y = 0;

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
                } else if ((tag == 3 || tag == 4) && val_type == 9) {
                    int v=static_cast<int>(buf.ReadVInt()); if (tag==3) x=v; else y=v;
                } else if (tag == 5) { // m_killerUnitTagIndex
                    if (val_type == 4) {
                        bool exists = buf.ReadBool();
                        if (exists) {
                            uint32_t t2 = buf.ReadBits(8);
                            if (t2 == 7) { killer_idx = buf.ReadU32(); has_killer_unit=true; }
                            else if (t2 == 9) { killer_idx = buf.ReadVInt(); has_killer_unit=true; }
                            else SkipValue(buf, t2);
                        }
                    } else if (val_type == 7) {
                        killer_idx = buf.ReadU32(); has_killer_unit=true;
                    } else if (val_type == 9) {
                        killer_idx = buf.ReadVInt(); has_killer_unit=true;
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
            const int64_t killer_uid = has_killer_unit ? ((killer_idx << 18) | killer_recycle) : -1;
            contribution.Died(unit_id, killer_pid, killer_uid, x, y, current_gameloop);
            drone_targets.erase(unit_id);
            if (const auto objective = objective_units.find(unit_id); objective != objective_units.end()) {
                auto& deaths = objective->second == Objective::Boss ? summary.objectives.bosses : summary.objectives.trains;
                ++deaths.count;
                deaths.last_loop = current_gameloop;
                objective_units.erase(objective);
            }
            auto it = live_units.find(unit_id);
            if (it != live_units.end()) {
                const auto& uinfo = it->second;
                bool excluded = uinfo.type=="FuelCellPickupUnit" || uinfo.type=="ForceField" || uinfo.type=="Scarab";
                bool salvage = killer_pid == uinfo.owner && (uinfo.type=="Bunker" || uinfo.type=="ArtilleryMengsk" || uinfo.type=="PerditionTurret" || uinfo.type=="PerditionTurretUnderground" || uinfo.type=="FlamingBetty" || uinfo.type=="KelMorianGrenadeTurret" || uinfo.type=="KelMorianMissileTurret" || uinfo.type=="NovaACLaserTurret" || uinfo.type=="TychusSCVAutoTurret" || uinfo.type=="BunkerDepotMengsk");
                const auto& victim_commander = uinfo.owner == 1 ? p1.commander : p2.commander;
                bool ignore_loss = salvage || glevig_spawns.count(unit_id) || murvar_spawns.count(unit_id) ||
                    broodlord_broodlings.count(unit_id) || dragged_units.count(unit_id) ||
                    (dummy_zerglings.count(unit_id) && !killer_pid) ||
                    (uinfo.type == "Drone" && !killer_pid) ||
                    (victim_commander == "Abathur" && !killer_pid &&
                     (uinfo.type == "Roach" || uinfo.type == "RavagerAbathur" || uinfo.type == "RoachVileBurrowed" ||
                      uinfo.type == "RoachBurrowed" || uinfo.type == "SwarmHostBurrowed" || uinfo.type == "QueenBurrowed"));
                bool self_killing = !killer_pid && (uinfo.type == "FenixCoop" || uinfo.type == "FenixDragoon" || uinfo.type == "FenixArbiter");
                bool duplicate = current_gameloop > 0 && killer_pid == uinfo.owner &&
                    (uinfo.type == "HotSRaptor" || uinfo.type == "MutatorAmonArtanis" || uinfo.type == "HellbatBlackOps" || uinfo.type == "LurkerStetmannBurrowed") &&
                    live_units.count(killer_uid) && live_units.at(killer_uid).type == uinfo.type;
                if (self_killing || duplicate) {
                    auto& stats = uinfo.owner == 1 ? p1_unit_stats : uinfo.owner == 2 ? p2_unit_stats : amon_unit_stats;
                    --stats[uinfo.type].created;
                    if (uinfo.owner == 1) army1.RemoveCreated(uinfo.type);
                    if (uinfo.owner == 2) army2.RemoveCreated(uinfo.type);
                    ignore_loss = true;
                }
                if ((uinfo.type == "HighTemplar" || uinfo.type == "DarkTemplar") && uinfo.owner >= 0 &&
                    uinfo.owner < 17 && archon_merges[uinfo.owner] > 0) {
                    --archon_merges[uinfo.owner];
                    ignore_loss = true;
                }
                if (current_gameloop <= 16) ignore_loss = true;
                if (uinfo.owner==1 && !ignore_loss) army1.Died(uinfo.type,false); else if (uinfo.owner==1 && salvage) army1.Died(uinfo.type,true);
                if (uinfo.owner==2 && !ignore_loss) army2.Died(uinfo.type,false); else if (uinfo.owner==2 && salvage) army2.Died(uinfo.type,true);
                if (!ignore_loss && uinfo.owner == 1) {
                    p1_unit_stats[uinfo.type].lost++;
                } else if (!ignore_loss && uinfo.owner == 2) {
                    p2_unit_stats[uinfo.type].lost++;
                } else if (!ignore_loss && is_enemy(uinfo.owner)) {
                    amon_unit_stats[uinfo.type].lost++;
                }

                auto kit = live_units.find(killer_uid);
                std::string killer_type;
                if (kit != live_units.end()) {
                    killer_type = kit->second.type;
                } else if (killer_pid == 1 || killer_pid == 2) {
                    const auto& commander = killer_pid == 1 ? p1.commander : p2.commander;
                    const auto& stats = killer_pid == 1 ? p1_unit_stats : p2_unit_stats;
                    std::vector<std::string> fallback;
                    if (commander == "Nova") fallback = {"CoopCasterNova"};
                    else if (commander == "Han & Horner") fallback = {"HHMagneticMine"};
                    else if (commander == "Karax") fallback = {"SoACasterKarax"};
                    else if (commander == "Artanis") fallback = {"SoACasterArtanis"};
                    else if (commander == "Mengsk") fallback = {"ArtilleryMengsk", "GhostMengsk"};
                    for (const auto& candidate : fallback) {
                        if (stats.count(candidate)) { killer_type = candidate; break; }
                    }
                }

                if (killer_pid == 1 || killer_pid == 2) {
                    const auto& commander = killer_pid == 1 ? p1.commander : p2.commander;
                    const auto& stats = killer_pid == 1 ? p1_unit_stats : p2_unit_stats;
                    if (killer_type == "Locust" && commander == "Abathur" && !biomass_locusts.count(killer_uid) && stats.count("SwarmHost"))
                        killer_type = "SwarmHost";
                    else if (glevig_spawns.count(killer_uid)) killer_type = "Glevig";
                    else if (murvar_spawns.count(killer_uid)) killer_type = "Murvar";
                    else if (broodlord_broodlings.count(killer_uid))
                        killer_type = killer_type == "Broodling" ? "BroodLord" : "BroodLordStetmann";
                }
                if (!ignore_loss && mind_controlled.count(unit_id)) {
                    if (uinfo.owner == 1) army1.MindControlledUnitDies(uinfo.type);
                    if (uinfo.owner == 2) army2.MindControlledUnitDies(uinfo.type);
                }
                if (!excluded && killer_pid == 1 && is_enemy(uinfo.owner)) {
                    p1_kills++;
                    if (!killer_type.empty() && killer_uid != unit_id) {
                        p1_unit_stats[killer_type].kills++;
                    }
                } else if (!excluded && killer_pid == 2 && is_enemy(uinfo.owner)) {
                    p2_kills++;
                    if (!killer_type.empty() && killer_uid != unit_id) {
                        p2_unit_stats[killer_type].kills++;
                    }
                } else if (!excluded && is_enemy(killer_pid) && (uinfo.owner == 1 || uinfo.owner == 2)) {
                    ++amon_kills;
                    if (!killer_type.empty() && killer_uid != unit_id) {
                        amon_unit_stats[killer_type].kills++;
                    }
                }
                it->second.completed = false;
            }
        }
        else if (event_id == 3 || event_id == 5 || event_id == 7) {
            int64_t idx=0, recycle=0; int owner=0; std::string upgrade;
            for (int64_t f=0; f<field_count && !buf.Done(); ++f) {
                auto tag=buf.ReadVInt(); auto type=buf.ReadBits(8);
                if (event_id==5 && tag==0 && type==9) owner=static_cast<int>(buf.ReadVInt());
                else if (event_id==5 && tag==1 && type==2) { auto n=buf.ReadVInt(); upgrade=buf.ReadString(static_cast<size_t>(n)); }
                else if (event_id!=5 && (tag==0 || tag==1) && (type==7 || type==9)) { auto v=type==7?buf.ReadU32():buf.ReadVInt(); if(tag==0) idx=v; else recycle=v; }
                else if (event_id==3 && tag==2 && type==9) owner=static_cast<int>(buf.ReadVInt());
                else SkipValue(buf,type);
            }
            if (event_id==5) {
                if (owner == 0) {
                    auto mutator = MutatorFromUpgrade(upgrade);
                    if (!mutator.empty() && std::find(summary.tracker_mutators.begin(), summary.tracker_mutators.end(), mutator) == summary.tracker_mutators.end())
                        summary.tracker_mutators.push_back(std::move(mutator));
                }
                if (owner==1) army1.Upgrade(upgrade); else if (owner==2) army2.Upgrade(upgrade);
                if (summary.start_loop<0 && (owner==1 || owner==2) && upgrade.find("Spray")!=std::string::npos) summary.start_loop=current_gameloop;
            } else {
                auto id=(idx<<18)|recycle; auto it=live_units.find(id);
                if (event_id==7 && it!=live_units.end()) it->second.completed=true;
                if (event_id==3) {
                    contribution.OwnerChange(id,owner,current_gameloop);
                    if (it != live_units.end()) {
                        if ((owner == 1 || owner == 2) && is_enemy(it->second.owner)) mind_controlled.insert(id);
                        it->second.owner = owner;
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

    summary.complete = buf.Done();
    if (!summary.complete) summary.objectives = {};
    if (current_wave_units.size() >= 6) {
        identified_waves.push_back(current_wave_units);
    }
    summary.enemy_comp = IdentifyEnemyComposition(identified_waves);

    auto ProcessUnits = [](const std::unordered_map<std::string, StatCounter>& stats_map, int total_kills) {
        std::unordered_map<std::string, StatCounter> merged;
        static const std::unordered_map<std::string,std::string> losses = {{"TorrasqueChrysalis","Ultralisk"},{"SiegeTankWreckage","Siege Tank"},{"ThorWreckageSwann","Thor"},{"ThorWreckage","Thor"},{"DehakaMutaliskReviveEgg","Primal Mutalisk"}};
        static const std::unordered_set<std::string> excluded = {
            "SuperWarpGate", "VoidRiftUnselectable", "UnbuildableRocksUnit", "TrooperMengskWeaponAAPickup",
            "TrooperMengskWeaponFlamethrowerPickup", "TrooperMengskWeaponImprovedPickup", "PsiDisintegratorPowerLink",
            "ProtossDockingBayUnit", "PnPHybridVoidRift", "PlatformConnector", "MutatorAmonKaraxInvisiblePylon",
            "KorhalGateControl", "HybridStasisChamberA", "HybridHoldingCellSmallUnit", "HybridHoldingCellUnit",
            "GateControlUnit", "Food1000", "COOPTerrazineTank", "ExpeditionJumpGate", "EnemyPathingBlocker4x4",
            "InvisibleEscortFlying", "DestructibleUmojanLabTestTube", "AmonHostDeathBeamUnit", "DamagedMutatorLaserDrill"};
        for (const auto& [raw, st] : stats_map) {
            if (excluded.count(raw)) continue;
            auto kill = tracker_reference::kill_names.find(raw); auto loss=losses.find(raw);
            if (kill!=tracker_reference::kill_names.end()) merged[kill->second].kills += st.kills;
            if (loss!=losses.end()) merged[loss->second].lost += st.lost;
            if (kill!=tracker_reference::kill_names.end() || loss!=losses.end()) continue;
            auto name=tracker_reference::unit_names.find(raw);
            auto& dst=merged[name==tracker_reference::unit_names.end()?raw:name->second];
            dst.created+=st.created; dst.lost+=st.lost; dst.kills+=st.kills;
        }
        std::vector<UnitStat> out;
        for (const auto& [name, st] : merged) {
            UnitStat us; us.name=name; us.created=st.created; us.lost=st.lost; us.kills=st.kills;
            us.kill_fraction=total_kills>0 ? std::round(100.f*st.kills/total_kills)/100.f : 0.f;
            out.push_back(us);
        }
        std::sort(out.begin(),out.end(),[](const UnitStat& a,const UnitStat& b) {
            if(a.kills!=b.kills) return a.kills>b.kills;
            if(a.created!=b.created) return a.created>b.created;
            return a.name<b.name;
        });
        return out;
    };
    summary.p1_units = ProcessUnits(p1_unit_stats, p1_kills);
    summary.p2_units = ProcessUnits(p2_unit_stats, p2_kills);
    summary.amon_units = ProcessUnits(amon_unit_stats, amon_kills);
    summary.bonus_loops = contribution.BonusLoops();
    summary.bonus_known = buf.Done() && contribution.BonusKnown();
    summary.p1_contributions = contribution.Contributions(1);
    summary.p2_contributions = contribution.Contributions(2);

    auto SmoothSeries = [](PlayerTimeSeries& s) {
        if (s.collection_rate.size() <= 1) return;
        for (size_t i = s.collection_rate.size() - 1; i > 0; --i) {
            s.collection_rate[i] = 0.5f * s.collection_rate[i] + 0.5f * s.collection_rate[i - 1];
        }
    };
    SmoothSeries(summary.p1_series);
    SmoothSeries(summary.p2_series);
    auto remove_spikes=[](std::vector<float>& data) { for(size_t i=1;i+1<data.size();++i) if(data[i]>data[i-1] && data[i]>data[i+1]) data[i]=(data[i-1]+data[i+1])/2; };
    if(p1.commander=="Dehaka") remove_spikes(summary.p1_series.army_value);
    if(p2.commander=="Dehaka") remove_spikes(summary.p2_series.army_value);

    summary.p1_kills = p1_kills;
    summary.p2_kills = p2_kills;

    return summary;
}

TrackerSummary S2Decoder::DecodeTrackerEvents(const std::vector<uint8_t>& bytes, int p1_kills, int p2_kills,
                                               const std::string& p1_cmdr, const std::string& p2_cmdr) {
    PlayerInfo p1, p2; p1.kills=p1_kills; p2.kills=p2_kills; p1.commander=p1_cmdr; p2.commander=p2_cmdr;
    return DecodeTrackerEvents(bytes,p1,p2,{},{});
}
} // namespace sc2
