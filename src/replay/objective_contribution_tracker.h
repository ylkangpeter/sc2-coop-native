#pragma once

#include "replay_types.h"
#include "tracker_enemy.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sc2 {

// Consumes tracker events in replay order. Times are raw game loops.
class ObjectiveContributionTracker {
    struct Unit { std::string type; int owner; int landed = -1; bool bonus_recorded = false; };
    std::string map_;
    std::unordered_map<int64_t, Unit> units_;
    std::array<std::map<std::string, int>, 3> counts_;
    std::array<std::vector<std::string>, 3> outlaws_;
    std::set<int64_t> used_mines_;
    std::unordered_map<int64_t, std::string> dead_killers_;
    bool saw_mutator_mine_ = false;
    std::vector<int> bonus_;
    int mw_first_ = -1, mw_second_ = -1, eradicator_player_kills_ = 0;
    bool eradicator_failed_ = false;
    bool Is(std::string_view s) const { return map_.find(s) != std::string::npos; }
    static bool Player(int p) { return p == 1 || p == 2; }
    static bool In(std::string_view s, std::initializer_list<std::string_view> values) {
        return std::find(values.begin(), values.end(), s) != values.end();
    }
    bool Enemy(int p) const { return IsTrackerEnemy(map_, p); }

    bool BonusDeath(const std::string& type, int owner, int killer, int x, int y, int loop) {
        if (Is("Void Thrashing")) return owner == 5 && In(type, {"ArchAngelCoopFighter", "ArchAngelCoopAssault"});
        if (Is("Dead of Night")) return owner == 7 && type == "ACVirophage" && Player(killer);
        if (Is("Lock & Load") || Is("[MM] LnL")) return owner == 3 && type == "XelNagaConstruct";
        if (Is("Chain of Ascension")) return owner == 10 && type == "SlaynElemental" && Player(killer);
        if (Is("Rifts to Korhal")) return owner == 8 && type == "ACPirateCapitalShip" && Player(killer);
        if (Is("Cradle of Death")) return owner == 3 && type == "LogisticsHeadquarters";
        if (Is("Part and Parcel")) {
            if (owner != 8 || !In(type, {"Caboose", "TarsonisEngine"}) || bonus_.size() >= 2) return false;
            if ((x == 169 && y == 99) || (x == 38 && y == 178)) return false;
            return bonus_.empty() || (loop + 8) / 16 != (bonus_.back() + 8) / 16;
        }
        if (Is("Oblivion Express")) return owner == 7 && type == "TarsonisEngineFast" && x < 196;
        if (Is("Mist Opportunities")) return owner == 3 && type == "COOPTerrazineTank" && Player(killer);
        if (Is("The Vermillion Problem")) return owner == 9 && In(type, {"RedstoneSalamander", "RedstoneSalamanderBurrowed"}) && Player(killer);
        if (Is("Miner Evacuation")) {
            if (owner == 5 && type == "Blightbringer") return Player(killer);
            if (owner == 9 && type == "NovaEradicator") {
                if (Player(killer)) ++eradicator_player_kills_;
                else eradicator_failed_ = true;
                return !eradicator_failed_ && eradicator_player_kills_ == 2;
            }
            return false;
        }
        if (Is("Temple of the Past")) return owner == 8 && type == "ZenithStone";
        return false;
    }

public:
    explicit ObjectiveContributionTracker(std::string map) : map_(std::move(map)) {}
    bool BonusKnown() const {
        return Is("Void Thrashing") || Is("Dead of Night") || Is("Lock & Load") || Is("[MM] LnL") ||
            Is("Chain of Ascension") || Is("Rifts to Korhal") || Is("Cradle of Death") || Is("Part and Parcel") ||
            Is("Oblivion Express") || Is("Mist Opportunities") || Is("The Vermillion Problem") || Is("Miner Evacuation") ||
            Is("Temple of the Past") || Is("Void Launch") || Is("Scythe of Amon") || Is("Malwarfare");
    }
    const std::vector<int>& BonusLoops() const { return bonus_; }
    std::vector<Contribution> Contributions(int player) const {
        std::vector<Contribution> result;
        if (!Player(player)) return result;
        for (const auto& name : outlaws_[player]) result.push_back({name, 0});
        for (const auto& [name, count] : counts_[player]) {
            if (count <= 0 || ((name == "locust" || name == "broodling") && count < 200)) continue;
            if (name == "minesweeper" && !saw_mutator_mine_) continue;
            if (name == "deadofnight" && !Is("Dead of Night")) continue;
            if (name == "hfts" && counts_[player].count("tus")) continue;
            if (name == "tus") {
                auto h = counts_[player].find("hfts");
                result.push_back({name, count + (h == counts_[player].end() ? 0 : h->second)});
            } else result.push_back({name, count});
        }
        return result;
    }
    void Born(int64_t id, const std::string& type, int owner, int /*loop*/, const std::string& creator_ability = {}) {
        units_[id] = {type, owner};
        dead_killers_.erase(id);
        if (type == "MutatorSpiderMine" && Enemy(owner)) saw_mutator_mine_ = true;
        if (!Player(owner)) return;
        if (creator_ability != "CoopMurvarSpawnCreepers" && In(type, {"AbathurLocust", "Locust", "LocustMP", "DehakaCreeperFlying", "DehakaLocust"})) ++counts_[owner]["locust"];
        if (In(type, {"BroodlingEscortStetmann", "BroodlingEscort", "Broodling", "KerriganInfestBroodling", "StukovInfestBroodling", "BroodlingStetmann"})) ++counts_[owner]["broodling"];
        if (In(type, {"TychusCoop", "TychusReaper", "TychusWarhound", "TychusMarauder", "TychusHERC", "TychusFirebat", "TychusGhost", "TychusSpectre", "TychusMedic"})) {
            auto& order = outlaws_[owner];
            if (std::find(order.begin(), order.end(), type) == order.end()) order.push_back(type);
        }
        if (In(type, {"ZeratulKhaydarinMonolithProjection", "ZeratulPhotonCannonProjection"})) ++counts_[owner]["ShadeProjection"];
        if (type == "MULE") ++counts_[owner]["MULE"];
        if (type == "GreaterNydusWorm") ++counts_[owner]["Omega Worm"];
        if (type == "InfestedBunker" || type == "SIInfestedBunker") ++counts_[owner]["Infested Bunker"];
        if (type == "InfestorStetmann") ++counts_[owner]["Mecha Infestor"];
        if (type == "CommanderPrestigeArtanisOrbitalStrikesArchon") ++counts_[owner]["Unbound Fanatic"];
    }
    void TypeChange(int64_t id, const std::string& type, int loop) {
        auto it = units_.find(id); if (it == units_.end()) return;
        const int owner = it->second.owner;
        if (Is("Void Launch") && owner == 7 && type == "ResearchVesselLanded") it->second.landed = loop;
        if (Is("Void Launch") && owner == 7 && type == "ResearchVessel" && it->second.landed >= 0 &&
            loop < it->second.landed + 1500 && !it->second.bonus_recorded) {
            bonus_.push_back(loop); it->second.bonus_recorded = true;
        }
        if (Is("Scythe of Amon") && owner == 11 && type == "WarpPrismPhasing" && it->second.type != "WarpPrismPhasing") {
            bonus_.push_back(loop);
        }
        it->second.type = type;
    }
    void OwnerChange(int64_t id, int owner, int loop) {
        auto it = units_.find(id); if (it == units_.end()) return;
        if (Player(owner) && Enemy(it->second.owner)) ++counts_[owner]["mc"];
        it->second.owner = owner;
        if (Is("Malwarfare")) {
            if (owner == 9) mw_first_ = loop;
            else if (owner == 10) mw_second_ = loop;
            else if (owner == 6 && !it->second.bonus_recorded) {
                const bool completed_second = mw_second_ >= 0 && loop - mw_second_ != 3935;
                const bool completed_first = mw_first_ >= 0 && mw_second_ < 0 && loop - mw_first_ != 3935;
                if (completed_first || completed_second) {
                    bonus_.push_back(loop);
                    it->second.bonus_recorded = true;
                }
            }
        }
    }
    void Died(int64_t id, int killer, int64_t killer_id, int x, int y, int loop) {
        auto it = units_.find(id); if (it == units_.end()) return;
        const auto type = it->second.type; const int owner = it->second.owner;
        auto kit = units_.find(killer_id);
        std::string killer_type = kit == units_.end() ? "" : kit->second.type;
        if (killer_type.empty()) {
            auto dead = dead_killers_.find(killer_id);
            if (dead != dead_killers_.end()) killer_type = dead->second;
        }
        if (Player(owner) && In(type, {"ZeratulArtifactPickup1", "ZeratulArtifactPickup2", "ZeratulArtifactPickup3", "ZeratulArtifactPickupUnlimited"})) ++counts_[owner]["Artifact"];
        if (Player(owner) && In(killer_type, {"MutatorKillBot", "MutatorDeathBot", "MutatorMurderBot"})) ++counts_[owner]["killbots"];
        if (Player(owner) && Enemy(killer) && killer_type == "MutatorSpiderMine" && used_mines_.insert(killer_id).second) ++counts_[owner]["minesweeper"];
        if (Player(killer) && Enemy(owner)) {
            std::string key;
            if (In(type, {"MutatorAmonArtanis", "MutatorAmonDehaka", "MutatorAmonKarax", "MutatorAmonKerrigan", "MutatorAmonNova", "MutatorAmonRaynor", "MutatorAmonTychus", "MutatorAmonZagara", "MutatorAmonZeratul"})) ++counts_[killer]["hfts"];
            if (In(type, {"SuperGaryStetmann", "SOAMothershipv4", "TychusHERC", "AvatarofForm", "HyperionVoidCoop", "StukovAleksander", "DehakaMurvar", "DehakaDakrun", "TychusOdin", "StukovApocalisk", "AlarakCoop", "DehakaGlevig", "TychusReaper", "TychusFirebat", "TychusSpectre", "TychusWarhound", "FenixCoop", "TychusMedic"})) ++counts_[killer]["tus"];
            if (In(type, {"MutatorSpiderMine", "MutatorSpiderMineBurrowed", "WidowMineBurrowed", "WidowMine"})) key = "minesweeper";
            else if (type == "MutatorVoidRift") key = "voidrifts";
            else if (type == "MutatorPropagator") key = "propagators";
            else if (type == "ProtossFrigate") key = "shuttles";
            else if (In(type, {"MutatorTurkey", "MutatorTurking", "MutatorInfestedTurkey"})) key = "turkey";
            else if (type == "MutatorVoidReanimator") key = "voidreanimators";
            else if (In(type, {"InfestableBiodome", "JarbanInfestibleColonistHut", "InfestedMercHaven", "InfestableHut"})) key = "deadofnight";
            else if (In(type, {"MutatorMissileSplitterChild", "MutatorMissileNuke", "MutatorMissileSplitter", "MutatorMissileStandard", "MutatorMissilePointDefense"})) key = "missilecommand";
            if (!key.empty()) ++counts_[killer][key];
        }
        if (BonusDeath(type, owner, killer, x, y, loop)) bonus_.push_back(Is("Cradle of Death") ? loop - 128 : loop);
        // A mine's explosion can kill after its own death. Retain only such killer types.
        if (In(type, {"MutatorSpiderMine", "MutatorKillBot", "MutatorDeathBot", "MutatorMurderBot"})) dead_killers_[id] = type;
        units_.erase(it);
    }
};
}
