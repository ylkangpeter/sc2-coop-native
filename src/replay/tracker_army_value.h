#pragma once
#include "tracker_reference_data.h"
#include "replay_types.h"
#include <cmath>

namespace sc2 {
class TrackerArmyValue {
    using Cost = tracker_reference::Cost;
    const PlayerInfo& player_;
    std::string commander_;
    float offset_ = 0;
    int free_banes_ = 0;
    bool first_outlaw_ = false;
    struct Counts { int made = 0, lost = 0, salvaged = 0; };
    std::unordered_map<std::string, Counts> counts_;
    static bool in(const std::string& s, std::initializer_list<const char*> list) {
        for (auto x : list) if (s == x) return true;
        return false;
    }
    Cost cost(const std::string& unit) const {
        using namespace tracker_reference;
        Cost c{};
        auto ci = costs.find(commander_);
        if (ci == costs.end()) return c;
        auto ui = ci->second.find(unit);
        if (ui == ci->second.end()) {
            std::string base = unit;
            for (auto suffix : {"Burrowed", "Phasing", "Uprooted", "Sieged", "SiegeMode"}) {
                std::string s = suffix;
                if (base.size() >= s.size() && base.compare(base.size()-s.size(), s.size(), s)==0) {
                    base.erase(base.size()-s.size()); break;
                }
            }
            if (base == unit && base.size() >= 7 && base.compare(base.size()-7,7,"Fighter")==0) base.replace(base.size()-7,7,"Assault");
            ui = ci->second.find(base);
        }
        if (ui != ci->second.end()) c = ui->second;
        const auto& p = player_.prestige_name;
        if (commander_ == "Abathur" && p == "Essence Hoarder") { c[1]*=1.2f; c[3]*=1.2f; }
        if (commander_ == "Alarak" && p == "Shadow of Death") {
            if (unit == "SOAMothershipv4") c = {400,400,0,0};
            if (unit == "VoidRayTaldarim") c = {125,75,0,0};
        }
        float m=1, g=1;
        if (commander_=="Artanis" && p=="Valorous Inspirator" && !in(unit,{"PhotonCannon","Observer","ObserverSiegeMode"})) m=g=1.3f;
        else if (commander_=="Fenix" && p=="Network Administrator" && !in(unit,{"PhotonCannon","Observer","ObserverSiegeMode"})) m=g=.5f;
        else if (commander_=="Horner") {
            if (p=="Chaotic Power Couple" && horners_units.count(unit)) m=g=1.3f;
            else if (p=="Wing Commanders" && horners_units.count(unit)) g=.8f;
            else if (p=="Galactic Gunrunners" && unit=="HHBomberPlatform") m=g=2;
        } else if (commander_=="Karax" && p=="Templar Apparent" && !in(unit,{"ShieldBattery","KhaydarinMonolith","PhotonCannon","Observer","ObserverSiegeMode"})) m=g=.6f;
        else if (commander_=="Kerrigan" && player_.masteries.size()>2) g=1-player_.masteries[2]/100.f;
        else if (commander_=="Mengsk") {
            if (royal_guards.count(unit)) {
                if (player_.masteries.size()>3) m=g=1-player_.masteries[3]/150.f;
                if (p=="Principal Proletariat") {m*=2;g*=.75f;}
            }
            if (p=="Merchant of Death" && in(unit,{"TrooperMengskAA","TrooperMengskFlamethrower","TrooperMengskImproved"})) c={40,20,80,20};
        } else if (commander_=="Raynor") {
            if (p=="Rough Rider" && in(unit,{"Banshee","Battlecruiser","VikingAssault","VikingFighter","SiegeTank","SiegeTankSieged"})) g=1.25f;
            else if (p=="Rebel Raider") { if (in(unit,{"Banshee","Battlecruiser","VikingAssault","VikingFighter"})) {m=1.5f;g=.7f;} else if (!in(unit,{"Bunker","MissileTurret","SpiderMine"})) m=1.5f; }
        } else if (commander_=="Stetmann" && p=="Oil Baron" && !in(unit,{"SpineCrawlerStetmann","SpineCrawlerUprootedStetmann","SporeCrawlerStetmann","SporeCrawlerUprootedStetmann","OverseerStetmann","OverseerStetmannSiegeMode"})) m=1.4f;
        else if (commander_=="Stukov" && p=="Frightful Fleshwelder" && in(unit,{"SILiberator","StukovInfestedBanshee","StukovInfestedBansheeBurrowed","StukovInfestedDiamondBack","StukovInfestedSiegeTank","StukovInfestedSiegeTankUprooted"})) m=g=.7f;
        else if (commander_=="Swann" && p=="Grease Monkey" && !in(unit,{"KelMorianGrenadeTurret","KelMorianMissileTurret","PerditionTurret","PerditionTurretUnderground"})) g=1.5f;
        else if (commander_=="Tychus" && p=="Technical Recruiter" && unit!="TychusSCVAutoTurret") m=g=1.5f;
        else if (commander_=="Zagara") {
            if (p=="Mother of Constructs" && in(unit,{"ZagaraCorruptor","InfestedAbomination"})) m=g=.75f;
            else if (p=="Apex Predator" && !in(unit,{"BileLauncherZagara","QueenCoop","QueenCoopBurrowed","Overseer","OverseerSiegeMode","SpineCrawler","SpineCrawlerUprooted","SporeCrawler","SporeCrawlerUprooted"})) m=g=1.25f;
        } else if (commander_=="Zeratul" && p=="Knowledge Seeker" && !in(unit,{"ZeratulObserver","ZeratulObserverSiegeMode","ZeratulPhotonCannon","ZeratulWarpPrism","ZeratulWarpPrismPhasing"})) m=g=1.25f;
        c[0]*=m; c[1]*=g; c[2]*=m; c[3]*=g; return c;
    }
public:
    explicit TrackerArmyValue(const PlayerInfo& p):player_(p),commander_(p.commander=="Han & Horner"?"Horner":p.commander) {}
    void Born(const std::string& unit, const std::string& ability) {
        counts_[unit].made++;
        if (tracker_reference::outlaws.count(unit)) first_outlaw_=true;
        if (commander_=="Zagara" && in(unit,{"Baneling","HotSSplitterlingBig"}) && ability!="MorphZerglingToSplitterling") ++free_banes_;
    }
    void Died(const std::string& unit, bool salvage) { if (salvage) ++counts_[unit].salvaged; else ++counts_[unit].lost; }
    void MindControlledUnitDies(const std::string& unit) {
        auto c = cost(unit);
        offset_ += c[0] + c[1] + c[2] + c[3];
    }
    void RemoveCreated(const std::string& unit) { --counts_[unit].made; }
    void Upgrade(const std::string& upgrade) {
        if (tracker_reference::tychus_base_upgrades.count(upgrade)) offset_ += player_.prestige_name=="Lone Wolf" ? 937.5f : 750;
        else if (tracker_reference::tychus_ultimate_upgrades.count(upgrade) && player_.prestige_name!="Lone Wolf") offset_ += 1600;
    }
    void Morph(const std::string& old, const std::string& unit, bool count_new) {
        if (count_new) ++counts_[unit].made;
        float weapon = player_.prestige_name=="Merchant of Death" ? 60 : 160;
        auto improved=[](const std::string& s){return in(s,{"TrooperMengskAA","TrooperMengskFlamethrower","TrooperMengskImproved"});};
        if (old=="TrooperMengsk" && improved(unit)) offset_+=weapon;
        else if (old=="GaryStetmann" && unit=="SuperGaryStetmann") offset_+=750;
        else if (old=="TrooperMengsk" && unit=="SCVMengsk") offset_-=40;
        else if (old=="SCVMengsk" && unit=="TrooperMengsk") offset_+=40;
        else if (improved(old) && unit=="SCVMengsk") offset_-=40+weapon;
        else if ((old=="Thor" && unit=="ThorWreckageSwann") || (old=="SiegeTank" && unit=="SiegeTankWreckage")) offset_-=cost(old)[1];
        else if ((unit=="Thor" && old=="ThorWreckageSwann") || (unit=="SiegeTank" && old=="SiegeTankWreckage")) offset_+=cost(unit)[1];
        auto sum=[](Cost c){return c[0]+c[1]+c[2]+c[3];};
        auto full=[](Cost c){return c[2]+c[3];};
        if ((old=="GuardianMP" || old=="Devourer" || old=="Viper") && unit=="LeviathanCocoon") {
            auto c=cost(old); offset_ -= (old=="Viper" ? sum(c) : full(c)) - sum(cost("Mutalisk"));
        } else if ((unit=="GuardianMP" || unit=="Devourer" || unit=="Viper") && old=="LeviathanCocoon") {
            auto c=cost(unit); offset_ += (unit=="Viper" ? sum(c) : full(c)) - sum(cost("Mutalisk"));
        } else {
            auto adjustment=[&](const std::string& type) {
                auto c=cost(type);
                return (type=="RavagerAbathur" || type=="RavagerAbathurBurrowed" ? full(c) : sum(c))-sum(cost("RoachVile"));
            };
            if ((in(old,{"SwarmHost","SwarmHostBurrowed"}) && unit=="BrutaliskCocoonSwarmhost") ||
                (in(old,{"RavagerAbathur","RavagerAbathurBurrowed"}) && unit=="BrutaliskCocoonRavager") ||
                (in(old,{"Queen","QueenBurrowed"}) && unit=="BrutaliskCocoonQueen")) offset_-=adjustment(old);
            else if ((in(unit,{"SwarmHost","SwarmHostBurrowed"}) && old=="BrutaliskCocoonSwarmhost") ||
                     (in(unit,{"RavagerAbathur","RavagerAbathurBurrowed"}) && old=="BrutaliskCocoonRavager") ||
                     (in(unit,{"Queen","QueenBurrowed"}) && old=="BrutaliskCocoonQueen")) offset_+=adjustment(unit);
        }
    }
    float Value() const {
        float total=offset_;
        for (const auto& [unit,n] : counts_) {
            auto c=cost(unit); int made=n.made-n.salvaged, lost=tracker_reference::outlaws.count(unit)?0:n.lost;
            if (commander_=="Zagara" && in(unit,{"Baneling","HotSSplitterlingBig"})) total+=(made-free_banes_)*(c[0]+c[1])+(free_banes_-lost)*(c[2]+c[3]);
            else total+=made*(c[0]+c[1])-lost*((c[2]!=0 || c[3]!=0)?c[2]+c[3]:c[0]+c[1]);
        }
        if (first_outlaw_ && total>600) total-=600;
        return std::max(0.f, std::round(total));
    }
};
}
