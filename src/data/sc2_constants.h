// Generated SC2 Co-op constants
#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <unordered_map>
#include <unordered_set>

namespace sc2 {

inline const std::vector<std::string> kCommanders = {
    "Abathur",
    "Alarak",
    "Artanis",
    "Dehaka",
    "Fenix",
    "Horner",
    "Karax",
    "Kerrigan",
    "Mengsk",
    "Nova",
    "Raynor",
    "Stetmann",
    "Stukov",
    "Swann",
    "Tychus",
    "Vorazun",
    "Zagara",
    "Zeratul",
};

struct PrestigeInfo {
    const char* p0;
    const char* p1;
    const char* p2;
    const char* p3;
};

inline const std::unordered_map<std::string, PrestigeInfo> kPrestiges = {
    {"Abathur", {"Evolution Master", "Essence Hoarder", "Tunneling Horror", "The Limitless"}},
    {"Alarak", {"Highlord of the Tal'darim", "Artificer of Souls", "Tyrant Ascendant", "Shadow of Death"}},
    {"Artanis", {"Hierarch of the Daelaam", "Valorous Inspirator", "Nexus Legate", "Arkship Commandant"}},
    {"Dehaka", {"Primal Pack Leader", "Devouring One", "Primal Contender", "Broodbrother"}},
    {"Fenix", {"Purifier Executor", "Akhundelar", "Network Administrator", "Unconquered Spirit"}},
    {"Horner", {"Mercenary Leader and Dominion Admiral", "Chaotic Power Couple", "Wing Commanders", "Galactic Gunrunners"}},
    {"Karax", {"Khalai Phase-Smith", "Architect of War", "Templar Apparent", "Solarite Celestial"}},
    {"Kerrigan", {"Queen of Blades", "Malevolent Matriarch", "Folly of Man", "Desolate Queen"}},
    {"Mengsk", {"Emperor of the Dominion", "Toxic Tyrant", "Principal Proletariat", "Merchant of Death"}},
    {"Nova", {"Dominion Ghost", "Soldier of Fortune", "Tactical Dispatcher", "Infiltration Specialist"}},
    {"Raynor", {"Renegade Commander", "Backwater Marshal", "Rough Rider", "Rebel Raider"}},
    {"Stetmann", {"Hero Genius (Henius)", "Signal Savant", "Best Buddy", "Oil Baron"}},
    {"Stukov", {"Infested Admiral", "Frightful Fleshwelder", "Plague Warden", "Lord of the Horde"}},
    {"Swann", {"Chief Engineer", "Heavy Weapons Specialist", "Grease Monkey", "Payload Director"}},
    {"Tychus", {"Legendary Outlaw", "Technical Recruiter", "Lone Wolf", "Dutiful Dogwalker"}},
    {"Vorazun", {"Matriarch of the Nerazim", "Spirit of Respite", "Withering Siphon", "Keeper of Shadows"}},
    {"Zagara", {"Swarm Broodmother", "Scourge Queen", "Mother of Constructs", "Apex Predator"}},
    {"Zeratul", {"Dark Prelate", "Anakh Su'n", "Knowledge Seeker", "Herald of the Void"}},
};

inline const std::unordered_map<std::string, std::array<const char*, 6>> kCommanderMasteries = {
    {"Abathur", {"Toxic Nest Damage", "Mend Healing Duration", "Symbiote Ability Improvement", "Double Biomass Chance", "Toxic Nest Maximum Charges and Cooldown", "Structure Morph and Evolution Rate"}},
    {"Alarak", {"Alarak Attack Damage", "Combat Unit Attack Speed", "Empower Me Duration", "Death Fleet Cooldown", "Structure Overcharge Shield and Attack Speed", "Chrono Boost Efficiency"}},
    {"Artanis", {"Shield Overcharge Duration and Damage Absorption", "Guardian Shell Life and Shield Restoration", "Energy Regeneration and Cooldown Reduction", "Speed Increases for Warped In Units", "Chrono Boost Efficiency", "Initial and Maximum Spear of Adun Energy"}},
    {"Dehaka", {"Devour Healing Increase", "Devour Buff Duration", "Greater Primal Wurm Cooldown", "Pack Leaders Active Duration", "Gene Mutation Chance", "Dehaka Attack Speed"}},
    {"Fenix", {"Fenix Suit Attack Speed", "Fenix Suit Offline Energy Regeneration", "Champion A.I. Attack Speed", "Champion A.I. Life and Shields", "Chrono Boost Efficiency", "Extra Starting Supply"}},
    {"Horner", {"Strike Fighter Area of Effect", "Stronger Death Chance", "Significant Other Bonuses", "Double Salvage Chance", "Air Fleet Travel Distance", "Mag Mine Charges, Cooldown, and Arming Time"}},
    {"Karax", {"Combat Unit Life and Shield", "Structure Life and Shields", "Repair Beam Healing Rate", "Chrono Wave Energy Generation", "Chrono Boost Efficiency", "Initial and Maximum Spear of Adun Energy"}},
    {"Kerrigan", {"Kerrigan Energy Regeneration", "Kerrigan Attack Damage", "Combat Unit Vespene Gas Cost", "Augmented Immobilization Wave", "Expeditious Evolutions", "Primary Ability Damage and Attack Speed"}},
    {"Mengsk", {"Laborer and Trooper Imperial Support", "Royal Guard Support", "Terrible Damage", "Royal Guard Cost", "Starting Imperial Mandate", "Royal Guard Experience Gain Rate"}},
    {"Nova", {"Nuke and Holo Decoy Cooldown", "Griffin Airstrike Cost", "Nova Primary Ability Improvement", "Combat Unit Attack Speed", "Nova Energy Regeneration", "Unit Life Regeneration"}},
    {"Raynor", {"Research Resource Cost", "Speed Increases for Drop Pod Units", "Hyperion Cooldown", "Banshee Airstrike Cooldown", "Medics Heal Additional Target", "Mech Attack Speed"}},
    {"Stetmann", {"Upgrade Resource Cost", "Gary Ability Cooldown", "Stetzone Bonuses", "Maximum Egonergy Pool", "Deploy Stetellite Cooldown", "Structure Morph Rate"}},
    {"Stukov", {"Volatile Infested Spawn Chance", "Infest Structure Cooldown", "Aleksander Cooldown", "Apocalisk Cooldown", "Infested Infantry Duration", "Mech Attack Speed"}},
    {"Swann", {"Concentrated Beam Width and Damage", "Combat Drop Duration and Life", "Immortality Protocol Cost and Build Time", "Structure Health", "Vespene Drone Cost", "Laser Drill Build Time, Upgrade Time, and Upgrade Cost"}},
    {"Tychus", {"Tychus Attack Speed", "Tychus Shredder Grenade Cooldown", "Tri-Outlaw Research Improvement", "Outlaw Availability", "Medivac Pickup Cooldown", "Odin Cooldown"}},
    {"Vorazun", {"Dark Pylon Range", "Black Hole Duration", "Shadow Guard Duration", "Time Stop Unit Speed Increase", "Chrono Boost Efficiency", "Initial and Maximum Spear of Adun Energy"}},
    {"Zagara", {"Zagara and Queen Regen", "Zagara Attack Damage", "Intensified Frenzy", "Zergling Evasion", "Roach Damage and Life", "Baneling Attack Damage"}},
    {"Zeratul", {"Zeratul Attack Speed", "Combat Unit Attack Speed", "Artifact Fragment Spawn Rate", "Support Calldown Cooldown Reduction", "Legendary Legion Cost", "Avatar Cooldown"}}
};

inline const std::unordered_map<std::string, int> kMapBonusCounts = {
    {"Chain of Ascension", 2},
    {"Cradle of Death", 2},
    {"Dead of Night", 1},
    {"Lock & Load", 1},
    {"Malwarfare", 2},
    {"Miner Evacuation", 2},
    {"Mist Opportunities", 2},
    {"Oblivion Express", 2},
    {"Part and Parcel", 2},
    {"Rifts to Korhal", 2},
    {"Scythe of Amon", 3},
    {"Temple of the Past", 3},
    {"The Vermillion Problem", 1},
    {"Void Launch", 3},
    {"Void Thrashing", 1}
};

inline const std::unordered_set<std::string> kUnitsInWaves = {
    "Medic", "Marauder", "Stalker", "ArbiterMP", "Ultralisk", "Viper", "Ravager", "Disruptor",
    "Banshee", "WidowMine", "Goliath", "Vulture", "Liberator", "Hellion", "Corruptor", "QueenClassic",
    "Marine", "Thor", "Archon", "VikingFighter", "Firebat", "Tempest", "ScienceVessel", "InfestedAbomination",
    "CorsairMP", "VoidRay", "HellionTank", "DarkTemplar", "Oracle", "BroodLord", "Raven", "Reaper",
    "Immortal", "Cyclone", "Colossus", "SiegeTank", "Devourer", "Roach", "Reaver", "Ghost", "Scout",
    "Dragoon", "Carrier", "LurkerMP", "Phoenix", "Sentry", "Zergling", "Hydralisk", "Wraith",
    "Battlecruiser", "Mutalisk", "Guardian", "Zealot", "HighTemplar", "Infestor", "Scourge",
    "SwarmHostMP", "Adept", "Baneling", "WarHound"
};

struct EnemyCompDef {
    const char* name;
    std::vector<std::unordered_set<std::string>> waves;
};

inline const std::vector<EnemyCompDef> kEnemyCompositions = {
    {"Brooding Corruption", {
        {"Zergling"}, {"Mutalisk", "Zergling"}, {"Baneling", "Mutalisk", "Zergling"}, {"Mutalisk", "Infestor", "Zergling"},
        {"Mutalisk", "Corruptor", "Infestor", "Zergling"}, {"Mutalisk", "Infestor", "Zergling", "BroodLord"},
        {"Mutalisk", "Corruptor", "Infestor", "BroodLord"}
    }},
    {"Classic Infantry", {
        {"Marine"}, {"Marine", "Medic"}, {"Marine", "Firebat", "Medic"}, {"Marine", "SiegeTank", "Medic"},
        {"Marine", "SiegeTank", "Ghost", "Medic"}, {"Firebat", "Marine", "Medic", "ScienceVessel", "SiegeTank"},
        {"Ghost", "Marine", "Medic", "ScienceVessel", "SiegeTank"}
    }},
    {"Classic Mech", {
        {"Vulture"}, {"Vulture", "Goliath"}, {"Wraith", "Vulture", "Goliath"}, {"SiegeTank", "Wraith", "Vulture", "Goliath"},
        {"SiegeTank", "Vulture", "Battlecruiser", "Goliath"}, {"ScienceVessel", "Battlecruiser", "Goliath"},
        {"SiegeTank", "Battlecruiser", "ScienceVessel", "Goliath"}
    }},
    {"Devouring Scourge", {
        {"Zergling"}, {"Mutalisk", "Zergling"}, {"Scourge", "QueenClassic", "Mutalisk", "Zergling"},
        {"Scourge", "Mutalisk", "Zergling", "Guardian"}, {"Devourer", "Mutalisk", "Guardian"},
        {"Devourer", "QueenClassic", "Mutalisk", "Guardian"}, {"Devourer", "QueenClassic", "Guardian", "Scourge"}
    }},
    {"Disruptive Artillery", {
        {"Adept"}, {"Adept", "Sentry"}, {"Adept", "Immortal"}, {"Adept", "Scout", "Immortal"},
        {"Adept", "Sentry", "Disruptor", "Reaver"}, {"Adept", "Disruptor", "Scout", "Reaver"},
        {"Sentry", "Disruptor", "Immortal", "Reaver"}
    }},
    {"Dominion Battlegroup", {
        {"VikingFighter"}, {"VikingFighter", "Banshee"}, {"VikingFighter", "Marine", "Banshee"},
        {"VikingFighter", "Marine", "Banshee", "Liberator"}, {"VikingFighter", "Raven", "Banshee", "Liberator"},
        {"Banshee", "Battlecruiser", "Liberator", "Marine", "Raven", "VikingFighter"},
        {"Banshee", "Battlecruiser", "Liberator", "Raven", "VikingFighter"}
    }},
    {"Explosive Threats", {
        {"Zergling"}, {"Baneling", "Zergling"}, {"InfestedAbomination", "Mutalisk", "Scourge", "Viper", "Zergling"},
        {"InfestedAbomination", "Mutalisk", "Scourge", "SwarmHostMP", "Zergling"},
        {"Baneling", "InfestedAbomination", "Mutalisk", "Scourge", "Viper", "Zergling"},
        {"Baneling", "Mutalisk", "Scourge", "SwarmHostMP", "Viper", "Zergling"},
        {"Baneling", "InfestedAbomination", "Mutalisk", "Scourge", "SwarmHostMP", "Viper"}
    }},
    {"Fleet of the Matriarch", {
        {"Zealot"}, {"Scout", "Zealot"}, {"CorsairMP", "Scout", "Zealot"}, {"CorsairMP", "Scout"}, {"Scout", "Carrier"},
        {"ArbiterMP", "Scout", "Carrier"}, {"ArbiterMP", "CorsairMP", "Scout", "Carrier"}
    }},
    {"Hope of the Khalai", {
        {"Zealot"}, {"Scout", "Zealot"}, {"Stalker", "Scout"}, {"Scout", "Oracle", "Zealot"}, {"VoidRay", "Zealot"},
        {"Oracle", "Carrier", "Zealot"}, {"VoidRay", "Oracle", "Carrier", "Zealot"}
    }},
    {"Invasionary Swarm", {
        {"Zergling"}, {"Zergling", "Hydralisk"}, {"LurkerMP", "Zergling", "Hydralisk"}, {"LurkerMP", "Hydralisk"},
        {"LurkerMP", "QueenClassic", "Hydralisk"}, {"Ultralisk", "Zergling", "Hydralisk"},
        {"Hydralisk", "LurkerMP", "QueenClassic", "Ultralisk", "Zergling"}
    }},
    {"Machines of War", {
        {"Hellion"}, {"WarHound", "Hellion", "Goliath"}, {"HellionTank", "SiegeTank", "WarHound", "Goliath"},
        {"Goliath", "HellionTank", "SiegeTank", "WarHound", "WidowMine"},
        {"Goliath", "ScienceVessel", "SiegeTank", "WarHound", "WidowMine"},
        {"Goliath", "ScienceVessel", "Thor", "WarHound", "WidowMine"}, {"Thor", "SiegeTank", "ScienceVessel"}
    }},
    {"Masters and Machines", {
        {"Zealot"}, {"Stalker", "Zealot"}, {"Stalker", "HighTemplar", "Zealot"}, {"Stalker", "HighTemplar", "Immortal"},
        {"Stalker", "Zealot", "HighTemplar", "Archon"}, {"Stalker", "Zealot", "Archon", "Colossus"},
        {"Colossus", "HighTemplar", "Immortal", "Stalker", "Zealot"}
    }},
    {"Raiding Party", {
        {"Marine", "Medic"}, {"Marine", "Marauder", "Medic"}, {"Firebat", "Marauder", "Medic"},
        {"Marine", "Medivac", "SiegeTank", "Ghost"}, {"Marauder", "Marine", "Medivac", "SiegeTank"},
        {"Ghost", "Marauder", "Marine", "ScienceVessel", "SiegeTank"},
        {"Battlecruiser", "Ghost", "Marine", "ScienceVessel", "SiegeTank"}
    }},
    {"Ravaging Infestation", {
        {"Roach"}, {"Roach", "Zergling"}, {"Roach", "Hydralisk"}, {"Ravager", "LurkerMP", "Roach", "Hydralisk"},
        {"LurkerMP", "Roach", "Infestor", "Hydralisk"}, {"Hydralisk", "Infestor", "Ravager", "Roach", "Ultralisk"},
        {"Hydralisk", "Infestor", "Ravager", "Roach", "Ultralisk"}
    }},
    {"Shadow Disruption", {
        {"Adept"}, {"Adept", "Stalker"}, {"Adept", "Sentry", "Stalker"}, {"DarkTemplar", "Stalker", "Adept", "Sentry"},
        {"DarkTemplar", "Adept", "Phoenix", "Sentry"}, {"Adept", "DarkTemplar", "Disruptor", "Sentry", "Stalker"},
        {"Adept", "DarkTemplar", "Disruptor", "Phoenix", "Sentry"}
    }},
    {"Shadow Tech", {
        {"Reaper"}, {"Marauder", "Reaper"}, {"Liberator", "Marauder", "Reaper"}, {"Cyclone", "Liberator", "Marauder", "Reaper"},
        {"Marauder", "Raven", "Liberator", "Cyclone"}, {"Marauder", "Raven", "Cyclone", "Battlecruiser"},
        {"Battlecruiser", "Cyclone", "Liberator", "Marauder", "Raven"}
    }},
    {"Siege of Storms", {
        {"Adept"}, {"Adept", "Phoenix"}, {"Adept", "Phoenix"}, {"Stalker", "Phoenix", "Oracle"}, {"Adept", "VoidRay"},
        {"Adept", "Oracle", "Tempest"}, {"Adept", "VoidRay", "Oracle", "Tempest"}
    }},
    {"Towering Walkers", {
        {"Zealot"}, {"Sentry", "Zealot"}, {"Sentry", "Immortal"}, {"Scout", "Immortal", "Zealot"},
        {"Sentry", "Scout", "Zealot", "Colossus"}, {"Scout", "Zealot", "Colossus"}, {"Sentry", "Zealot", "Immortal", "Colossus"}
    }},
    {"Vanguard of Aiur", {
        {"Zealot"}, {"Dragoon", "Zealot"}, {"Dragoon", "HighTemplar", "Zealot"}, {"Dragoon", "Reaver"},
        {"Reaver", "Archon", "HighTemplar", "Zealot"}, {"ArbiterMP", "Dragoon", "HighTemplar", "Zealot"},
        {"ArbiterMP", "Archon", "Dragoon", "HighTemplar", "Reaver", "Zealot"}
    }}
};

inline std::string IdentifyEnemyComposition(const std::vector<std::vector<std::string>>& identified_waves) {
    if (identified_waves.empty()) return "Standard Force";

    std::string best_comp = "Standard Force";
    double max_score = -1.0;

    for (const auto& comp : kEnemyCompositions) {
        double score = 0.0;
        for (const auto& wave : identified_waves) {
            std::unordered_set<std::string> wave_set(wave.begin(), wave.end());
            wave_set.erase("Medivac");
            if (wave_set.empty()) continue;

            for (const auto& target_stage : comp.waves) {
                if (wave_set == target_stage) {
                    score += 1.0 * target_stage.size();
                } else {
                    bool is_subset = true;
                    for (const auto& u : wave_set) {
                        if (target_stage.find(u) == target_stage.end()) {
                            is_subset = false;
                            break;
                        }
                    }
                    if (is_subset && target_stage.size() >= wave_set.size() && target_stage.size() - wave_set.size() == 1) {
                        score += 0.25 * target_stage.size();
                    }
                }
            }
        }
        if (score > max_score) {
            max_score = score;
            best_comp = comp.name;
        }
    }
    return best_comp;
}

inline const std::unordered_map<std::string, const char*> kFriendlyUnitNames = {
    {"DehakaCoop", "Dehaka"},
    {"KaraxCoop", "Karax"},
    {"K5Kerrigan", "Kerrigan"},
    {"AlarakCoop", "Alarak"},
    {"ZagaraVoidCoop", "Zagara"},
    {"NovaCoop", "Nova"},
    {"ZeratulCoop", "Zeratul"},
    {"FenixCoop", "Fenix"},
    {"TychusCoop", "Tychus"},
    {"GaryStetmann", "Gary"},
    {"SuperGaryStetmann", "Super Gary"},
    {"PhotonCannon", "Photon Cannon"},
    {"KhaydarinMonolith", "Khaydarin Monolith"},
    {"ShieldBattery", "Shield Battery"},
    {"SoACasterKarax", "Karax's Top Bar"},
    {"SoACasterArtanis", "Artanis' Top Bar"},
    {"CoopCasterDehaka", "Dehaka's Top Bar"},
    {"CoopCasterRaynor", "Raynor's Top Bar"},
    {"CoopCasterKerrigan", "Kerrigan's Top Bar"},
    {"CoopCasterSwann", "Swann's Top Bar"},
    {"CoopCasterZagara", "Zagara's Top Bar"},
    {"CoopCasterVorazun", "Vorazun's Top Bar"},
    {"CoopCasterAbathur", "Abathur's Top Bar"},
    {"CoopCasterAlarak", "Photon Overcharge"},
    {"CoopCasterNova", "Nova's Top Bar"},
    {"CoopCasterStukov", "Stukov's Top Bar"},
    {"CoopCasterFenix", "Fenix's Top Bar"},
    {"CoopCasterHorner", "Han & Horner's Top Bar"},
    {"CoopCasterTychus", "Tychus' Top Bar"},
    {"CoopCasterZeratul", "Zeratul's Top Bar"},
    {"CoopCasterStetmann", "Stetmann's Top Bar"},
    {"CoopCasterMengsk", "Mengsk's Top Bar"},
    {"DehakaZerglingLevel2", "Primal Zergling"},
    {"DehakaRoachLevel2", "Primal Roach"},
    {"DehakaRoachLevel3", "Primal Igniter"},
    {"DehakaHydraliskLevel2", "Primal Hydralisk"},
    {"DehakaMutaliskLevel3FightMorph", "Primal Mutalisk"},
    {"DehakaUltraliskLevel2", "Primal Ultralisk"},
    {"DehakaUltraliskLevel3", "Tyrannozor"},
    {"DehakaSwarmHost", "Primal Swarm Host"},
    {"DehakaRavasaur", "Ravasaur"},
    {"ImpalerDehaka", "Primal Impaler"},
    {"DehakaPackLeaderMurvar", "Murvar"},
    {"DehakaPackLeaderGlevig", "Glevig"},
    {"DehakaPackLeaderDakrun", "Dakrun"},
    {"DehakaPackLeaderZoruka", "Zoruka"},
    {"Murvar", "Murvar"},
    {"Glevig", "Glevig"},
    {"Dakrun", "Dakrun"},
    {"InfestedAbomination", "Aberration"},
    {"InfestedCivilian", "Infested Civilian"},
    {"InfestedTerranCampaign", "Infested Marine"},
    {"InfestedTrooper", "Infested Trooper"},
    {"VolatileInfested", "Volatile Infested"},
    {"Stank", "Stank"},
    {"Nyon", "Nyon"},
    {"Hunterling", "Hunterling"},
    {"Choker", "Choker"},
    {"Spotter", "Spotter"},
    {"SOAPurifierBeamUnit", "Purifier Beam"},
    {"PurifierBeam", "Purifier Beam"},
    {"PurifierBeamUnit", "Purifier Beam"},
    {"SOAThermalLanceTargeter", "Solar Lance"},
    {"ColossusPurifier", "Colossus"},
    {"ImmortalAiur", "Immortal"},
    {"ZealotPurifier", "Sentinel"},
    {"DehakaCoopClone", "Zweihaka"},
    {"CoopClone", "Zweihaka"},
    {"DehakaCreeperFlying", "Creeper Host"},
    {"CreeperFlying", "Creeper Host"},
    {"HybridNemesis", "Hybrid Nemesis"},
    {"HybridBehemoth", "Hybrid Behemoth"},
    {"VoidThrasher", "Void Thrasher"},
    {"Carrier", "Carrier"},
    {"Interceptor", "Interceptor"},
    {"Probe", "Probe"},
    {"SCV", "SCV"},
    {"Drone", "Drone"},
    {"Marine", "Marine"},
    {"Marauder", "Marauder"},
    {"SiegeTank", "Siege Tank"},
    {"SiegeTankSieged", "Siege Tank"},
    {"Goliath", "Goliath"},
    {"VikingFighter", "Viking"},
    {"VikingAssault", "Viking"},
    {"Banshee", "Banshee"},
    {"Battlecruiser", "Battlecruiser"},
    {"Raven", "Raven"},
    {"Liberator", "Liberator"},
    {"Thor", "Thor"},
    {"Cyclone", "Cyclone"},
    {"Hellion", "Hellion"},
    {"HellionTank", "Hellbat"},
    {"WidowMine", "Widow Mine"},
    {"WidowMineBurrowed", "Widow Mine"},
    {"SpiderMine", "Spider Mine"},
    {"Zealot", "Zealot"},
    {"Stalker", "Stalker"},
    {"Immortal", "Immortal"},
    {"Colossus", "Colossus"},
    {"Disruptor", "Disruptor"},
    {"HighTemplar", "High Templar"},
    {"DarkTemplar", "Dark Templar"},
    {"Archon", "Archon"},
    {"VoidRay", "Void Ray"},
    {"Tempest", "Tempest"},
    {"Phoenix", "Phoenix"},
    {"Adept", "Adept"},
    {"Oracle", "Oracle"},
    {"Scout", "Scout"},
    {"Reaver", "Reaver"},
    {"CorsairMP", "Corsair"},
    {"ArbiterMP", "Arbiter"},
    {"Zergling", "Zergling"},
    {"Baneling", "Baneling"},
    {"Roach", "Roach"},
    {"Ravager", "Ravager"},
    {"Hydralisk", "Hydralisk"},
    {"LurkerMP", "Lurker"},
    {"LurkerMPBurrowed", "Lurker"},
    {"Mutalisk", "Mutalisk"},
    {"Corruptor", "Corruptor"},
    {"BroodLord", "Brood Lord"},
    {"Ultralisk", "Ultralisk"},
    {"Viper", "Viper"},
    {"Infestor", "Infestor"},
    {"SwarmHostMP", "Swarm Host"},
    {"QueenClassic", "Queen"},
    {"Scourge", "Scourge"},
    {"Devourer", "Devourer"},
    {"Guardian", "Guardian"},
    {"TrooperMengsk", "Dominion Trooper"},
    {"SCVMengsk", "Dominion Laborer"},
    {"TrooperMengskAA", "Trooper (Rocket)"},
    {"TrooperMengskFlamethrower", "Trooper (Flamer)"},
    {"TrooperMengskImproved", "Trooper (Rifle)"},
    {"AegisGuard", "Aegis Guard"},
    {"EmperorsShadow", "Shadow"},
    {"ShockDivision", "Shock Division"},
    {"Blackhammer", "Blackhammer"},
    {"PrideOfAugustgrad", "Pride of Augustgrad"},
    {"TychusReaper", "Crooked Sam"},
    {"TychusWarhound", "Sirius"},
    {"TychusWarhoundAutoTurret", "Sirius Turret"},
    {"TychusMarauder", "Rattlesnake"},
    {"TychusHERC", "Cannonball"},
    {"TychusFirebat", "Blaze"},
    {"TychusGhost", "Vega"},
    {"TychusSpectre", "Nux"},
    {"TychusMedic", "Lt. Layna Nikara"},
    {"TychusOdin", "Odin"}
};

inline std::string FormatFriendlyUnitName(const std::string& raw) {
    if (raw.find("Top Bar") != std::string::npos) {
        return raw;
    }
    auto it = kFriendlyUnitNames.find(raw);
    if (it != kFriendlyUnitNames.end()) {
        return it->second;
    }
    // Clean common prefixes/suffixes
    std::string s = raw;
    if (s.rfind("Dehaka", 0) == 0 && s.size() > 6) s = s.substr(6);
    if (s.rfind("Karax", 0) == 0 && s.size() > 5) s = s.substr(5);
    if (s.rfind("Mengsk", 0) == 0 && s.size() > 6) s = s.substr(6);
    if (s.rfind("Stetmann", 0) == 0 && s.size() > 8) s = s.substr(8);
    if (s.rfind("Zagara", 0) == 0 && s.size() > 6) s = s.substr(6);
    if (s.rfind("MutatorAmon", 0) == 0 && s.size() > 11) s = s.substr(11);
    if (s.ends_with("Coop")) s = s.substr(0, s.size() - 4);
    if (s.ends_with("MP")) s = s.substr(0, s.size() - 2);
    return s.empty() ? raw : s;
}

inline const std::vector<std::string> kCanonicalMaps = {
    "Chain of Ascension",
    "Cradle of Death",
    "Dead of Night",
    "Lock & Load",
    "Malwarfare",
    "Miner Evacuation",
    "Mist Opportunities",
    "Oblivion Express",
    "Part and Parcel",
    "Rifts to Korhal",
    "Scythe of Amon",
    "Temple of the Past",
    "The Vermillion Problem",
    "Void Launch",
    "Void Thrashing",
};

inline const std::unordered_map<std::string, std::string> kMapAliases = {
    {"Apunta y carga", "Lock & Load"},
    {"Archange mécanique", "Part and Parcel"},
    {"Bariery na Ulnarze", "Lock & Load"},
    {"Bedrohung auf Korhal", "Rifts to Korhal"},
    {"Berceau de mort", "Cradle of Death"},
    {"Berço da Morte", "Cradle of Death"},
    {"Cadena de ascensión", "Chain of Ascension"},
    {"Catena dell'Ascensione", "Chain of Ascension"},
    {"Chain of Ascension", "Chain of Ascension"},
    {"Chaîne de l’Ascension", "Chain of Ascension"},
    {"La chaîne de l’Ascension", "Chain of Ascension"},
    {"Ciberguerra", "Malwarfare"},
    {"Cible verrouillée", "Lock & Load"},
    {"Cierre de filas", "Lock & Load"},
    {"Comboio da Aniquilação", "Oblivion Express"},
    {"Congegni contesi", "Lock & Load"},
    {"Cradle of Death", "Cradle of Death"},
    {"Culla mortale", "Cradle of Death"},
    {"Cuna de la muerte", "Cradle of Death"},
    {"Cuna de muerte", "Cradle of Death"},
    {"Cyfrowa wojna", "Malwarfare"},
    {"Dead of Night", "Dead of Night"},
    {"Demoliendo vacíos", "Void Thrashing"},
    {"Der Himmel fällt uns auf den Kopf", "The Sky is Falling"},
    {"Devastação do Vazio", "Void Thrashing"},
    {"Die Wiege des Todes", "Cradle of Death"},
    {"Ekspres do Otchłani", "Oblivion Express"},
    {"El problema Bermellón", "The Vermillion Problem"},
    {"El problema de Vermillion", "The Vermillion Problem"},
    {"Elektronische Kriegsführung", "Malwarfare"},
    {"Envíos letales", "Part and Parcel"},
    {"Evacuación de mineros", "Miner Evacuation"},
    {"Evacuación minera", "Miner Evacuation"},
    {"Evacuazione dei minatori", "Miner Evacuation"},
    {"Evacuação dos Mineiros", "Miner Evacuation"},
    {"Ewakuacja górników", "Miner Evacuation"},
    {"Expreso del olvido", "Oblivion Express"},
    {"Expreso hacia la extinción", "Oblivion Express"},
    {"Express pour l’Oubli", "Oblivion Express"},
    {"Falce di Amon", "Scythe of Amon"},
    {"Fallas a Korhal", "Rifts to Korhal"},
    {"Faux d’Amon", "Scythe of Amon"},
    {"Faz Parte", "Part and Parcel"},
    {"Fissuras de Korhal", "Rifts to Korhal"},
    {"Fisuras a Korhal", "Rifts to Korhal"},
    {"Foice de Amon", "Scythe of Amon"},
    {"Geißel Amons", "Scythe of Amon"},
    {"Guadaña de Amon", "Scythe of Amon"},
    {"Guadaña de Amón", "Scythe of Amon"},
    {"Guerra de malware", "Malwarfare"},
    {"Guerra informatica", "Malwarfare"},
    {"Guerre cybernétique", "Malwarfare"},
    {"Guerrilha Tecnológica", "Malwarfare"},
    {"Hierarquia da Ascensão", "Chain of Ascension"},
    {"Im Dienste der Wissenschaft", "Mist Opportunities"},
    {"Kette des Aufstiegs", "Chain of Ascension"},
    {"Kolebka Śmierci", "Cradle of Death"},
    {"La caduta del cielo", "The Sky is Falling"},
    {"La caída del cielo", "The Sky is Falling"},
    {"La route de Korhal", "Rifts to Korhal"},
    {"Lacération du Vide", "Void Thrashing"},
    {"Lancio del Vuoto", "Void Launch"},
    {"Lanzamiento al vacío", "Void Launch"},
    {"Lanzamiento del Vacío", "Void Launch"},
    {"Lançamento do Vazio", "Void Launch"},
    {"Le ciel nous tombe sur la tête", "The Sky is Falling"},
    {"Le problème Vermillion", "The Vermillion Problem"},
    {"Leerentumult", "Void Thrashing"},
    {"Lock & Load", "Lock & Load"},
    {"Los cielos se derrumban", "The Sky is Falling"},
    {"Malwarfare", "Malwarfare"},
    {"Mandando Bala", "Lock & Load"},
    {"Materialschlacht", "Part and Parcel"},
    {"Miner Evacuation", "Miner Evacuation"},
    {"Mist Opportunities", "Mist Opportunities"},
    {"Molochy na Char", "Void Thrashing"},
    {"Nacht des Grauens", "Dead of Night"},
    {"Navettes pour le Vide", "Void Launch"},
    {"Nicht ohne meinen Kumpel", "Miner Evacuation"},
    {"Noc zainfekowanych", "Dead of Night"},
    {"Noche de los muertos", "Dead of Night"},
    {"Noche mortal", "Dead of Night"},
    {"Noite Morta", "Dead of Night"},
    {"Notte oscura", "Dead of Night"},
    {"O Céu está Caindo", "The Sky is Falling"},
    {"O problema de Vermillion", "The Vermillion Problem"},
    {"Oblio Express", "Oblivion Express"},
    {"Oblivion Express", "Oblivion Express"},
    {"Oportunidades etéreas", "Mist Opportunities"},
    {"Oportunidades na Névoa", "Mist Opportunities"},
    {"Oportunidades nebulosas", "Mist Opportunities"},
    {"Opportunità sfumate", "Mist Opportunities"},
    {"Opresión del vacío", "Void Thrashing"},
    {"Part and Parcel", "Part and Parcel"},
    {"Parte por parte", "Part and Parcel"},
    {"Problem Vermilliona", "The Vermillion Problem"},
    {"Prosto w Otchłań", "Void Launch"},
    {"Questione scottante", "The Vermillion Problem"},
    {"Rak'Shir na Slayn", "Chain of Ascension"},
    {"Rifts to Korhal", "Rifts to Korhal"},
    {"Robots dans la brume", "Mist Opportunities"},
    {"Rottami pericolosi", "Part and Parcel"},
    {"Schloss und Riegel", "Lock & Load"},
    {"Scythe of Amon", "Scythe of Amon"},
    {"Shuttle zu Schutt", "Void Launch"},
    {"Stetmann w oparach", "Mist Opportunities"},
    {"Szczeliny na Korhalu", "Rifts to Korhal"},
    {"Tempel der Vergangenheit", "Temple of the Past"},
    {"Tempio del passato", "Temple of the Past"},
    {"Temple du passé", "Temple of the Past"},
    {"Temple of the Past", "Temple of the Past"},
    {"Templo del pasado", "Temple of the Past"},
    {"Templo do Passado", "Temple of the Past"},
    {"Terreurs nocturnes", "Dead of Night"},
    {"The Sky is Falling", "The Sky is Falling"},
    {"The Vermillion Problem", "The Vermillion Problem"},
    {"Transtarsonische Eisenbahn", "Oblivion Express"},
    {"Upadek z nieba", "The Sky is Falling"},
    {"Varchi per Korhal", "Rifts to Korhal"},
    {"Vermillion Live", "The Vermillion Problem"},
    {"Void Launch", "Void Launch"},
    {"Void Thrashing", "Void Thrashing"},
    {"Vuoto abominevole", "Void Thrashing"},
    {"Złomuj i rządź", "Part and Parcel"},
    {"Évacuation minière", "Miner Evacuation"},
    {"Świątynia przeszłości", "Temple of the Past"},
    {"Żniwa Amona", "Scythe of Amon"},
    {"Битва гигантов", "Void Thrashing"},
    {"Вермиллион проблем", "The Vermillion Problem"},
    {"Вирусная война", "Malwarfare"},
    {"Древний храм", "Temple of the Past"},
    {"Колыбель смерти", "Cradle of Death"},
    {"Коса Амуна", "Scythe of Amon"},
    {"Мертвые в ночи", "Dead of Night"},
    {"Надежный затвор", "Lock & Load"},
    {"Падающие небеса", "The Sky is Falling"},
    {"Пришествие Пустоты", "Void Launch"},
    {"Разломы на Корхале", "Rifts to Korhal"},
    {"С миру по нитке", "Part and Parcel"},
    {"Туманные перспективы", "Mist Opportunities"},
    {"Цепь вознесения", "Chain of Ascension"},
    {"Эвакуация шахтеров", "Miner Evacuation"},
    {"Экспресс забвения", "Oblivion Express"},
    {"亞蒙之鐮", "Scythe of Amon"},
    {"亡者之夜", "Dead of Night"},
    {"克哈裂痕", "Rifts to Korhal"},
    {"克哈裂隙", "Rifts to Korhal"},
    {"净网行动", "Malwarfare"},
    {"升格之链", "Chain of Ascension"},
    {"古老神殿", "Temple of the Past"},
    {"天界封锁", "Lock & Load"},
    {"天穹坠落", "The Sky is Falling"},
    {"天降危機", "The Sky is Falling"},
    {"往日神庙", "Temple of the Past"},
    {"惡意程式", "Malwarfare"},
    {"控制天星鎖", "Lock & Load"},
    {"机会渺茫", "Mist Opportunities"},
    {"死亡搖籃", "Cradle of Death"},
    {"死亡摇篮", "Cradle of Death"},
    {"求生无路", "Dead of Night"},
    {"湮灭快车", "Oblivion Express"},
    {"滅絕特快車", "Oblivion Express"},
    {"熔火危机", "The Vermillion Problem"},
    {"礦工疏散行動", "Miner Evacuation"},
    {"聚铁成兵", "Part and Parcel"},
    {"营救矿工", "Miner Evacuation"},
    {"虚空撕裂", "Void Thrashing"},
    {"虚空降临", "Void Launch"},
    {"虛空躍傳", "Void Launch"},
    {"護艇大作戰", "Mist Opportunities"},
    {"赤色危機", "The Vermillion Problem"},
    {"迎戰虛空", "Void Thrashing"},
    {"關鍵零件", "Part and Parcel"},
    {"飛升之鍊", "Chain of Ascension"},
    {"黑暗杀星", "Scythe of Amon"},
    {"공허 분쇄", "Void Thrashing"},
    {"공허의 출격", "Void Launch"},
    {"과거의 사원", "Temple of the Past"},
    {"광부 대피", "Miner Evacuation"},
    {"망각행 고속열차", "Oblivion Express"},
    {"버밀리언의 특종", "The Vermillion Problem"},
    {"승천의 사슬", "Chain of Ascension"},
    {"아몬의 낫", "Scythe of Amon"},
    {"안갯속 표류기", "Mist Opportunities"},
    {"잘못된 전쟁", "Malwarfare"},
    {"죽음의 밤", "Dead of Night"},
    {"죽음의 요람", "Cradle of Death"},
    {"천상의 쟁탈전", "Lock & Load"},
    {"코랄의 균열", "Rifts to Korhal"},
    {"하늘이 무너져도", "The Sky is Falling"},
    {"핵심 부품", "Part and Parcel"},
};

inline std::string NormalizeMapName(const std::string& raw) {
    auto it = kMapAliases.find(raw);
    if (it != kMapAliases.end()) return it->second;
    return raw;
}

inline const std::unordered_map<std::string, std::string> kMutators = {
    {"Random", "Applies a random Mutator that has not yet been chosen for this game."},
    {"Walking Infested", "Enemy units spawn Infested Terran upon death in numbers according to the unit's life."},
    {"Outbreak", "Enemy Infested Terrans spawn continuously around the map."},
    {"Darkness", "Previously explored areas remain blacked out on the minimap while outside of player vision."},
    {"Time Warp", "Enemy Time Warps are periodically deployed throughout the map."},
    {"Speed Freaks", "Enemy units have increased movement speed."},
    {"Mag-nificent", "Mag Mines are deployed throughout the map at the start of the mission."},
    {"Mineral Shields", "Mineral clusters at player bases are periodically encased in a shield which must be destroyed for gathering to continue."},
    {"Barrier", "Enemy units and structures gain a temporary shield upon the first time they take damage."},
    {"Avenger", "Enemy units gain increased attack speed, movement speed, armor, life, and life-regeneration when nearby enemy units die."},
    {"Evasive Maneuvers", "Enemy units teleport a short distance away upon taking damage."},
    {"Scorched Earth", "Enemy units set the terrain on fire upon death."},
    {"Lava Burst", "Lava periodically bursts from the ground at random locations and deals damage to player air and ground units."},
    {"Self Destruction", "Enemy units explode and deal damage to nearby player units upon death."},
    {"Aggressive Deployment", "Additional enemy units are periodically deployed onto the battlefield."},
    {"Alien Incubation", "All enemy units spawn Broodlings upon death."},
    {"Laser Drill", "An enemy Laser Drill constantly attacks player units within enemy vision."},
    {"Long Range", "Enemy units and structures have increased weapon and vision range."},
    {"Shortsighted", "Player units and structures have reduced vision range."},
    {"Mutually Assured Destruction", "Enemy Hybrid units detonate a Nuke upon death."},
    {"We Move Unseen", "All enemy units are permanently cloaked."},
    {"Nap Time", "Player worker units periodically fall asleep and must be awoken via the Wake Up ability on their command card."},
    {"Slim Pickings", "Player worker units gather resources at a reduced rate, but resource pickups spawn throughout the map."},
    {"Concussive Attacks", "Player units are slowed by all enemy attacks."},
    {"Stone Zealots", "The enemy has deployed Stone Zealots."},
    {"Just Die!", "Enemy units are automatically revived upon death."},
    {"Temporal Field", "Enemy Temporal Fields are periodically deployed throughout the map."},
    {"Void Rifts", "Void Rifts periodically appear in random locations and spawn enemy units until destroyed."},
    {"Twister", "Tornadoes move across the map, damaging and knocking back player units in their path."},
    {"Orbital Strike", "Enemy Orbital Strikes are periodically fired throughout the map."},
    {"Purifier Beam", "An enemy Purifier Beam moves across the map toward nearby player units."},
    {"Blizzard", "Storm clouds move across the map, damaging and freezing player units in their path."},
    {"Fear", "Player units will occasionally stop attacking and run around in fear upon taking damage."},
    {"Photon Overload", "All enemy structures attack nearby hostile units."},
    {"Minesweeper", "Groups of Widow Mines and Spider Mines are buried throughout the battlefield."},
    {"Chaos Studios", "Mutators are chosen at random and periodically cycle throughout the mission."},
    {"Void Reanimators", "Void Reanimators wander the battlefield, bringing your enemies back to life."},
    {"Going Nuclear", "Nukes are launched at random throughout the map."},
    {"Life Leech", "Enemy units and structures steal life or shields whenever they do damage."},
    {"Power Overwhelming", "All enemy units have energy and use random abilities."},
    {"Micro Transactions", "Giving commands to your units costs resources based on the unit's cost."},
    {"Missile Command", "Endless missile bombardments target your structures and must be shot down throughout the mission."},
    {"Vertigo", "Your camera randomly changes positions."},
    {"Undying Evil", "A monster is deployed against you.  Each time you kill it, it will gather more power and return even stronger."},
    {"Polarity", "Each enemy unit is immune to either your units or your ally's units."},
    {"Transmutation", "Enemy units have a chance to transform into more powerful units whenever they deal damage."},
    {"Afraid of the Dark", "Vision provided by all sources is extremely limited except when in view of your camera."},
    {"Trick or Treat", "Civilians visit your Candy Bowl looking for treats, which are generated by spending minerals. If no treats are available, the civilians transform into random enemy units."},
    {"Turkey Shoot", "Supply can only be generated by killing turkeys that wander throughout the map. Doing so may anger the turkeys that remain."},
    {"Sharing Is Caring", "Supply is shared between you and your partner, and units from both armies contribute to your combined supply cap."},
    {"Diffusion", "Damage dealt to enemies is split evenly across all nearby units, including your own."},
    {"Black Death", "Some enemy units carry a plague that deals damage over time and spreads to other nearby units. The plague spreads to your units when the enemy unit is killed."},
    {"Eminent Domain", "Enemies gain control of your structures after destroying them."},
    {"Gift Exchange", "Gifts are periodically deployed around the map.  If you don't claim them, Amon will!"},
    {"Naughty List", "Player units and structures take increased damage for each enemy they've killed."},
    {"Extreme Caution", "Your units will not obey any command placed in areas they cannot see."},
    {"Insubordination", "Your units don't follow orders exactly."},
    {"Heroes from the Storm", "Attack waves will be joined by heroes of increasing power."},
    {"Inspiration", "Enemy Heroic units increase the attack speed and armor of all enemies within a small range. "},
    {"Hardened Will", "Enemy Heroic units reduce all incoming damage to a maximum of 10 when any non-heroic enemy unit is near them."},
    {"Fireworks", "Enemies launch a dazzling fireworks display upon death, dealing damage to your nearby units."},
    {"Lucky Envelopes", "Festive envelopes containing resource pickups are dropped at random throughout the map."},
    {"Sluggishness", "Your units accelerate and turn slowly."},
    {"Double-Edged", "Your units also receive all the damage they deal, but they are healed over time."},
    {"Fatal Attraction", "When enemy units and structures die, any of your nearby units are pulled to their location."},
    {"Propagators", "Shapeless lifeforms creep toward your base, transforming all of the units and structures they touch into copies of themselves."},
    {"Moment Of Silence", "When a Heroic enemy dies, all player units around it will reflect on their transgressions, leaving them unable to attack or use abilities."},
    {"Kill Bots", "Offensive robots of a mysterious origin have been unleashed on the Koprulu sector, intent on destruction. Through cunning engineering, they are invincible until their pre-programmed kill counter has been filled. After that occurs, they will shut down. But can you survive for that long?"},
    {"Boom Bots", "Uncaring automatons carry a nuclear payload toward your base. One player must discern the disarming sequence and the other player must enter it."},
    {"The Mist", "Mists roll over battlefield while unseen terrors lurk inside. Desperate warriors will fall and rise again."},
    {"The Usual Suspects", "Enemy attacks will be led by dark reflections of Heroes in the service of Amon"},
    {"Supreme Commander", "Massive units gain 25% life and are bigger, the rest of units have 25% less life and are smaller. All units gain +2 weapon range."},
    {"Shapeshifters", "Shapeshifters spawn with enemy attacks and in enemy bases. These creatures can transform into any unit of yours."},
    {"Rip Field Generators", "Rip-Field Generators are deployed throughout the map. They will burn any unit that comes into their range."},
    {"Repulsive Field", "Enemy attacks will push your units away."},
    {"Old Times", "We are traveling back in time. Unit selection is limited to 12 units. There is no worker auto-mine, no smart cast, no multiple building select, etc."},
    {"Nuclear Mines", "Nuclear Mines have been placed around the battlefield."},
    {"Necronomicon", "Killed player units will rise again at enemy bases."},
    {"Mothership", "Enemy Mothership roams the map and attacks player units."},
    {"Matryoshka", "Enemy units will spawn mini-self upon death. This can trigger several times for larger units."},
    {"Level Playing Field", "All weapons and abilities can hit both air and ground targets."},
    {"Infestation Station", "Damaging any structure can cause infestation."},
    {"I Collect, I Change", "When a non-heroic unit kills a hostile unit, it becomes the unit it killed. Units can only evolve into more expensive units."},
    {"Great Wall", "Enemy begins massive effort to construct defensive structures around the battlefield."},
    {"Endurance", "Player and enemy units and structures have 3x more health and shields."},
    {"Dark Mirror", "Enemy attack waves will contain player units."},
    {"Bloodlust", "Enemy units gain increased attack speed, movement speed, acceleration and damage reduction as their health gets lower."},
    {"BlizzCon Challenge", "The BlizzCon Challenge starts with a single Mutator. As time passes, more Mutators will be added, until TEN Mutators are running wild at the same time."},
};

} // namespace sc2
