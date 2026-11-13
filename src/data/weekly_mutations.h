// Generated Weekly Mutations catalog (Fixed 184-week perpetual rotation)
#pragma once

#include <string>
#include <vector>
#include <ctime>
#include <cstdio>
#include <cstdint>

namespace sc2 {

struct WeeklyMutation {
    int week_number;                   // 1-based week number (1..184)
    const char* title;                 // Mutation name
    const char* map_name;              // Canonical map name
    std::vector<const char*> mutators; // Mutator list
    int score;                         // Total mutator difficulty points (-1 if none/unranked)
    const char* tier;                  // Brutal+ tier ("Brutal+1".."Brutal+6", or "-")
};

inline const std::vector<WeeklyMutation> kWeeklyMutations = {
    {1, "Train of the Dead", "Oblivion Express", {"Darkness", "Outbreak", "Walking Infested"}, 7, "Brutal+2"},
    {2, "First Strike", "Chain of Ascension", {"Laser Drill", "Long Range", "Shortsighted"}, 5, "Brutal+1"},
    {3, "Time Lock", "Lock & Load", {"Mag-nificent", "Speed Freaks", "Time Warp"}, 7, "Brutal+2"},
    {4, "Bad Weather", "Rifts to Korhal", {"Twister", "Void Rifts"}, 12, "Brutal+4"},
    {5, "Whiteout", "Void Launch", {"Blizzard", "Slim Pickings"}, 9, "Brutal+3"},
    {6, "War Is Hell", "Void Thrashing", {"Evasive Maneuvers", "Lava Burst", "Scorched Earth"}, 6, "Brutal+1"},
    {7, "Temple of Pain", "Temple of the Past", {"Avenger", "Barrier", "Mineral Shields"}, 9, "Brutal+3"},
    {8, "Spear of Your Doom", "Lock & Load", {"Orbital Strike", "Purifier Beam", "Temporal Field"}, 4, "Brutal+1"},
    {9, "Special Delivery", "Void Launch", {"Aggressive Deployment", "Alien Incubation", "Self Destruction"}, 8, "Brutal+2"},
    {10, "Urban Warfare", "Rifts to Korhal", {"Minesweeper", "Photon Overload", "We Move Unseen"}, 10, "Brutal+3"},
    {11, "Wheel of Misfortune", "Void Thrashing", {"Chaos Studios"}, -1, "-"},
    {12, "Perfect Storm", "Chain of Ascension", {"Blizzard", "Shortsighted", "Twister"}, 7, "Brutal+2"},
    {13, "Boom Town", "Oblivion Express", {"Fear", "Going Nuclear", "Self Destruction"}, 9, "Brutal+3"},
    {14, "Dark Ritual", "Temple of the Past", {"Power Overwhelming", "Speed Freaks"}, 7, "Brutal+2"},
    {15, "Out of Order", "The Vermillion Problem", {"Darkness", "Micro Transactions", "Time Warp"}, 8, "Brutal+2"},
    {16, "Burning Legion", "Lock & Load", {"Avenger", "Scorched Earth", "Void Rifts"}, 17, "Brutal+5"},
    {17, "Triple Threat", "Void Launch", {"Laser Drill", "Orbital Strike", "Outbreak"}, 6, "Brutal+1"},
    {18, "World on Fire", "The Vermillion Problem", {"Going Nuclear", "Missile Command", "Purifier Beam"}, 8, "Brutal+2"},
    {19, "Flipping Out", "Chain of Ascension", {"Evasive Maneuvers", "Fear", "Vertigo"}, -1, "-"},
    {20, "Growing Threat", "Mist Opportunities", {"Alien Incubation", "Mutually Assured Destruction", "Transmutation"}, 14, "Brutal+4"},
    {21, "Miner Concerns", "Rifts to Korhal", {"Fear", "Mag-nificent", "Slim Pickings"}, 12, "Brutal+4"},
    {22, "Diplomatic Immunity", "Oblivion Express", {"Barrier", "Evasive Maneuvers", "Polarity"}, 10, "Brutal+3"},
    {23, "Wheel of Misfortune", "Lock & Load", {"Chaos Studios"}, -1, "-"},
    {24, "Breath of Destruction", "Mist Opportunities", {"Aggressive Deployment", "Avenger", "Speed Freaks"}, 10, "Brutal+3"},
    {25, "Fright Night", "Rifts to Korhal", {"Afraid of the Dark", "Trick or Treat"}, -1, "-"},
    {26, "Conflagration", "Temple of the Past", {"Alien Incubation", "Scorched Earth", "Walking Infested"}, 6, "Brutal+1"},
    {27, "Explosive Results", "Void Thrashing", {"Going Nuclear", "Mutually Assured Destruction", "Self Destruction"}, 11, "Brutal+4"},
    {28, "Fowl Play", "Oblivion Express", {"Sharing is Caring", "Turkey Shoot"}, -1, "-"},
    {29, "Power Trip", "The Vermillion Problem", {"Long Range", "Photon Overload", "Power Overwhelming"}, 8, "Brutal+2"},
    {30, "Shir Chaos", "Chain of Ascension", {"Just Die!", "Outbreak", "Speed Freaks"}, 12, "Brutal+4"},
    {31, "Survival of the Fittest", "Void Thrashing", {"Barrier", "Transmutation"}, 9, "Brutal+3"},
    {32, "Violent Night", "Void Launch", {"Gift Exchange", "Naughty List"}, -1, "-"},
    {33, "Rumble in the Jungle", "Mist Opportunities", {"Aggressive Deployment", "Temporal Field", "We Move Unseen"}, 7, "Brutal+2"},
    {34, "The Ultimate Price", "Lock & Load", {"Micro Transactions", "Mineral Shields", "Missile Command"}, 10, "Brutal+3"},
    {35, "Cremation", "Miner Evacuation", {"Scorched Earth", "Void Reanimators"}, 7, "Brutal+2"},
    {36, "Shields Up!", "Void Launch", {"Diffusion", "Mineral Shields", "Shortsighted"}, 6, "Brutal+1"},
    {37, "Ulnar New Year", "Lock & Load", {"Fireworks", "Lucky Envelopes"}, -1, "-"},
    {38, "Sick Micro", "Mist Opportunities", {"Black Death", "Micro Transactions"}, 12, "Brutal+4"},
    {39, "Blast from the Past", "Temple of the Past", {"Eminent Domain", "Mutually Assured Destruction"}, 6, "Brutal+1"},
    {40, "Portal Power", "Void Thrashing", {"Power Overwhelming", "Void Rifts"}, 15, "Brutal+5"},
    {41, "Wheel of Misfortune", "The Vermillion Problem", {"Chaos Studios"}, -1, "-"},
    {42, "Hostile Takeover", "Rifts to Korhal", {"Eminent Domain", "Laser Drill", "Missile Command"}, 6, "Brutal+1"},
    {43, "Grave Danger", "Temple of the Past", {"Transmutation", "Void Reanimators", "Walking Infested"}, 14, "Brutal+4"},
    {44, "The Injustice League", "Rifts to Korhal", {"Hardened Will", "Heroes from the Storm", "Inspiration"}, 14, "Brutal+4"},
    {45, "No Money, More Problems", "The Vermillion Problem", {"Self Destruction", "Slim Pickings", "Twister"}, 10, "Brutal+3"},
    {46, "Field of Screams", "Mist Opportunities", {"Minesweeper", "Polarity"}, 13, "Brutal+4"},
    {47, "The Quick and the Dead", "Dead of Night", {"Black Death", "Speed Freaks"}, 9, "Brutal+3"},
    {48, "Out of Sight", "Miner Evacuation", {"Purifier Beam", "We Move Unseen"}, 5, "Brutal+1"},
    {49, "Wheel of Misfortune", "Oblivion Express", {"Chaos Studios"}, -1, "-"},
    {50, "Frostbite", "Dead of Night", {"Blizzard", "Mag-nificent"}, 8, "Brutal+2"},
    {51, "Certain Demise", "Chain of Ascension", {"Avenger", "Void Reanimators"}, 10, "Brutal+3"},
    {52, "Delivery Guaranteed", "Void Launch", {"Barrier", "Going Nuclear", "Just Die!"}, 12, "Brutal+4"},
    {53, "Dead Heat", "The Vermillion Problem", {"Fatal Attraction", "Self Destruction", "Walking Infested"}, 8, "Brutal+2"},
    {54, "Cold is the Void", "Oblivion Express", {"Blizzard", "Void Reanimators", "Void Rifts"}, 19, "Brutal+6"},
    {55, "Futile Resistance", "Lock & Load", {"Concussive Attacks", "Propagators", "Temporal Field"}, 10, "Brutal+3"},
    {56, "Hard Target", "Void Thrashing", {"Barrier", "Hardened Will", "Polarity"}, 11, "Brutal+4"},
    {57, "Charnel House", "Miner Evacuation", {"Black Death", "Darkness", "Photon Overload"}, 10, "Brutal+3"},
    {58, "Slow and Steady", "Mist Opportunities", {"Concussive Attacks", "Speed Freaks", "Time Warp"}, 4, "Brutal+1"},
    {59, "Instant Karma", "Chain of Ascension", {"Double-Edged", "Mutually Assured Destruction", "Self Destruction"}, 11, "Brutal+4"},
    {60, "Worn Out Welcome", "Temple of the Past", {"Inspiration", "Just Die!", "Long Range"}, 11, "Brutal+4"},
    {61, "Moths to the Flame", "Rifts to Korhal", {"Alien Incubation", "Fatal Attraction", "Purifier Beam"}, 7, "Brutal+2"},
    {62, "Fear and Lava", "The Vermillion Problem", {"Fear", "Void Rifts"}, 13, "Brutal+4"},
    {63, "Wheel of Misfortune", "Void Launch", {"Chaos Studios"}, -1, "-"},
    {64, "Kill Bot Wars", "Mist Opportunities", {"Kill Bots", "Moment Of Silence"}, 8, "Brutal+2"},
    {65, "Magnetic Attraction", "Void Thrashing", {"Fatal Attraction", "Mag-nificent", "We Move Unseen"}, 10, "Brutal+3"},
    {66, "Death from Below", "Miner Evacuation", {"Lava Burst", "Minesweeper", "Scorched Earth"}, 11, "Brutal+4"},
    {67, "Watch the Skies", "Void Launch", {"Aggressive Deployment", "Missile Command", "Orbital Strike"}, 7, "Brutal+2"},
    {68, "Think Fast", "Rifts to Korhal", {"Propagators", "Speed Freaks"}, 10, "Brutal+3"},
    {69, "What Goes Around", "Scythe of Amon", {"Double-Edged", "Mutually Assured Destruction"}, 8, "Brutal+2"},
    {70, "Bannable Offense", "Malwarfare", {"Evasive Maneuvers", "Power Overwhelming"}, 6, "Brutal+1"},
    {71, "Masters of Midnight", "Part and Parcel", {"Fear", "Life Leech", "Shortsighted"}, 5, "Brutal+1"},
    {72, "Night Drive", "Cradle of Death", {"Afraid of the Dark", "Mag-nificent"}, -1, "-"},
    {73, "Get Out More", "Dead of Night", {"Eminent Domain", "Mineral Shields", "Self Destruction"}, 6, "Brutal+1"},
    {74, "Quick Killers", "Temple of the Past", {"Inspiration", "Kill Bots", "Speed Freaks"}, 10, "Brutal+3"},
    {75, "Unstable Environment", "Scythe of Amon", {"Lava Burst", "Time Warp", "Twister"}, 6, "Brutal+1"},
    {76, "Rest in Peace", "Void Thrashing", {"Moment Of Silence", "Propagators"}, 10, "Brutal+3"},
    {77, "Endless Infection", "Void Launch", {"Alien Incubation", "Black Death", "Walking Infested"}, 11, "Brutal+4"},
    {78, "Battle Hardened", "Chain of Ascension", {"Hardened Will", "Inspiration", "Missile Command"}, 7, "Brutal+2"},
    {79, "Wheel of Misfortune", "Cradle of Death", {"Chaos Studios"}, -1, "-"},
    {80, "Chilling Adaptation", "Miner Evacuation", {"Blizzard", "Transmutation"}, 11, "Brutal+4"},
    {81, "Fright Night", "Rifts to Korhal", {"Afraid of the Dark", "Trick or Treat"}, -1, "-"},
    {82, "Price of Progress", "Malwarfare", {"Kill Bots", "Micro Transactions"}, 11, "Brutal+4"},
    {83, "Negative Reinforcement", "Part and Parcel", {"Avenger", "Fatal Attraction"}, 8, "Brutal+2"},
    {84, "Fowl Play", "Oblivion Express", {"Sharing is Caring", "Turkey Shoot"}, -1, "-"},
    {85, "Retribution", "Mist Opportunities", {"Double-Edged", "Void Rifts"}, 13, "Brutal+4"},
    {86, "Rise from the Ashes", "Scythe of Amon", {"Lava Burst", "Self Destruction", "Void Reanimators"}, 11, "Brutal+4"},
    {87, "Warp Zone", "Lock & Load", {"Temporal Field", "Time Warp", "Twister"}, 4, "Brutal+1"},
    {88, "Well Trained", "Oblivion Express", {"Aggressive Deployment", "Avenger", "Inspiration"}, 10, "Brutal+3"},
    {89, "Violent Night", "Void Launch", {"Gift Exchange", "Naughty List"}, -1, "-"},
    {90, "Doomsday Report", "The Vermillion Problem", {"Going Nuclear", "Orbital Strike", "Propagators"}, 12, "Brutal+4"},
    {91, "Safety Violation", "Miner Evacuation", {"Mag-nificent", "Laser Drill", "Self Destruction"}, 9, "Brutal+3"},
    {92, "Temple of Rebirth", "Temple of the Past", {"Barrier", "Just Die!", "Life Leech"}, 10, "Brutal+3"},
    {93, "Overclocked", "Malwarfare", {"Avenger", "Photon Overload", "Speed Freaks"}, 8, "Brutal+2"},
    {94, "Attrition Warfare", "Void Thrashing", {"Diffusion", "Minesweeper"}, 9, "Brutal+3"},
    {95, "Ulnar New Year", "Lock & Load", {"Fireworks", "Lucky Envelopes"}, -1, "-"},
    {96, "Together Forever", "Chain of Ascension", {"Just Die!", "Polarity"}, 14, "Brutal+4"},
    {97, "Specter of Death", "Part and Parcel", {"Void Reanimators", "We Move Unseen"}, 8, "Brutal+2"},
    {98, "Distant Threat", "Dead of Night", {"Long Range", "Purifier Beam", "Time Warp"}, 5, "Brutal+1"},
    {99, "Radiation Zone", "Rifts to Korhal", {"Black Death", "Going Nuclear", "Outbreak"}, 13, "Brutal+4"},
    {100, "Inner Power", "Temple of the Past", {"Alien Incubation", "Power Overwhelming"}, 7, "Brutal+2"},
    {101, "Moving Fees", "Cradle of Death", {"Micro Transactions", "Scorched Earth"}, 7, "Brutal+2"},
    {102, "Memorable Boss", "Void Launch", {"Heroes from the Storm", "Moment Of Silence"}, 12, "Brutal+4"},
    {103, "Flip My Base", "Mist Opportunities", {"Eminent Domain", "Missile Command", "Purifier Beam"}, 6, "Brutal+1"},
    {104, "Firewall", "Malwarfare", {"Barrier", "Lava Burst", "Minesweeper"}, 11, "Brutal+4"},
    {105, "Death and Taxes", "Dead of Night", {"Black Death", "Micro Transactions", "Slim Pickings"}, 17, "Brutal+5"},
    {106, "Wheel of Misfortune", "Miner Evacuation", {"Chaos Studios"}, -1, "-"},
    {107, "Rubber and Glue", "Void Launch", {"Diffusion", "Double-Edged", "Temporal Field"}, 7, "Brutal+2"},
    {108, "Hell Train", "Oblivion Express", {"Just Die!", "Scorched Earth"}, 9, "Brutal+3"},
    {109, "Infection Detected", "Malwarfare", {"Alien Incubation", "Outbreak", "Self Destruction"}, 8, "Brutal+2"},
    {110, "Opportunities Unleashed", "Mist Opportunities", {"Barrier", "Power Overwhelming", "Speed Freaks"}, 9, "Brutal+3"},
    {111, "One For All", "Temple of the Past", {"Hardened Will", "Inspiration", "Transmutation"}, 11, "Brutal+4"},
    {112, "Experimental Artillery", "Part and Parcel", {"Laser Drill", "Long Range", "Photon Overload"}, 5, "Brutal+1"},
    {113, "Nuclear Family", "Chain of Ascension", {"Hardened Will", "Mutually Assured Destruction", "Outbreak"}, 10, "Brutal+3"},
    {114, "Die Together", "Scythe of Amon", {"Just Die!", "Polarity"}, 14, "Brutal+4"},
    {115, "Burning Evacuation", "Miner Evacuation", {"Avenger", "Eminent Domain", "Scorched Earth"}, 8, "Brutal+2"},
    {116, "Assembly of Vengeance", "Temple of the Past", {"Avenger", "Heroes from the Storm"}, 15, "Brutal+5"},
    {117, "Railroad Switch", "Oblivion Express", {"Just Die!", "Polarity"}, 14, "Brutal+4"},
    {118, "Hostile Territory", "Dead of Night", {"Barrier", "Photon Overload", "Power Overwhelming"}, 8, "Brutal+2"},
    {119, "Media Blackout", "The Vermillion Problem", {"Darkness", "Missile Command", "We Move Unseen"}, 8, "Brutal+2"},
    {120, "Thunder Dome", "Lock & Load", {"Heroes from the Storm", "Mag-nificent"}, 14, "Brutal+4"},
    {121, "Dodge This", "Scythe of Amon", {"Darkness", "Going Nuclear", "Orbital Strike"}, 6, "Brutal+1"},
    {122, "Binary Choice", "Malwarfare", {"Polarity", "Void Reanimators"}, 12, "Brutal+4"},
    {123, "The Ascended", "Chain of Ascension", {"Inspiration", "Transmutation"}, 9, "Brutal+3"},
    {124, "Enhanced Defenses", "Cradle of Death", {"Barrier", "Going Nuclear", "Photon Overload"}, 6, "Brutal+1"},
    {125, "Double Trouble", "Void Launch", {"Kill Bots", "Propagators"}, 14, "Brutal+4"},
    {126, "And Drops And Rifts", "Part and Parcel", {"Aggressive Deployment", "Void Rifts"}, 13, "Brutal+4"},
    {127, "Catch the Train", "Oblivion Express", {"Afraid of the Dark", "Shortsighted", "Speed Freaks"}, -1, "-"},
    {128, "Temple of Terror", "Temple of the Past", {"Afraid of the Dark", "Alien Incubation", "Fear"}, -1, "-"},
    {129, "Shining Bright", "Mist Opportunities", {"Concussive Attacks", "Laser Drill", "Purifier Beam"}, 5, "Brutal+1"},
    {130, "Endless Sparkles", "Miner Evacuation", {"Alien Incubation", "Self Destruction"}, 5, "Brutal+1"},
    {131, "Graveyard Shift", "Dead of Night", {"Afraid of the Dark", "Purifier Beam"}, -1, "-"},
    {132, "Robotic Revival", "Part and Parcel", {"Fear", "Just Die!", "Laser Drill"}, 12, "Brutal+4"},
    {133, "Secret Storm", "Lock & Load", {"Aggressive Deployment", "Twister", "We Move Unseen"}, 8, "Brutal+2"},
    {134, "Undermined", "Rifts to Korhal", {"Minesweeper", "Void Rifts"}, 16, "Brutal+5"},
    {135, "Astigmatism", "Malwarfare", {"Long Range", "Shortsighted", "We Move Unseen"}, 6, "Brutal+1"},
    {136, "Never Say Die", "Mist Opportunities", {"Barrier", "Just Die!", "Life Leech"}, 10, "Brutal+3"},
    {137, "Medieval Times", "Scythe of Amon", {"Black Death", "Transmutation"}, 14, "Brutal+4"},
    {138, "Onslaught", "Dead of Night", {"Aggressive Deployment", "Outbreak", "Void Reanimators"}, 11, "Brutal+4"},
    {139, "Equivalent Exchange", "Void Thrashing", {"Diffusion", "Just Die!"}, 10, "Brutal+3"},
    {140, "Encroaching Madness", "Mist Opportunities", {"Eminent Domain", "Purifier Beam", "Void Rifts"}, 13, "Brutal+4"},
    {141, "Season of Giving", "Cradle of Death", {"Blizzard", "Gift Exchange", "Naughty List"}, -1, "-"},
    {142, "Choices Choices", "Temple of the Past", {"Hardened Will", "Inspiration", "Mutually Assured Destruction"}, 9, "Brutal+3"},
    {143, "Inordinate Response", "Scythe of Amon", {"Avenger", "Power Overwhelming"}, 10, "Brutal+3"},
    {144, "By Fire Be Purged", "Oblivion Express", {"Going Nuclear", "Lava Burst", "Scorched Earth"}, 8, "Brutal+2"},
    {145, "Timely Reinforcements", "Miner Evacuation", {"Temporal Field", "Void Rifts"}, 11, "Brutal+4"},
    {146, "Blind Tribute", "Chain of Ascension", {"Kill Bots", "Shortsighted"}, 7, "Brutal+2"},
    {147, "Barrier to Entry", "Lock & Load", {"Barrier", "Just Die!", "Mineral Shields"}, 11, "Brutal+4"},
    {148, "Like Swatting Insects", "Cradle of Death", {"Alien Incubation", "Missile Command", "Outbreak"}, 8, "Brutal+2"},
    {149, "Getting Along", "The Vermillion Problem", {"Polarity", "Sharing is Caring"}, -1, "-"},
    {150, "Knock Knock", "Dead of Night", {"Avenger", "Propagators"}, 13, "Brutal+4"},
    {151, "Dance Dance Evolution", "Mist Opportunities", {"Moment Of Silence", "Speed Freaks", "Transmutation"}, 11, "Brutal+4"},
    {152, "Train of Pain", "Oblivion Express", {"Double-Edged", "Going Nuclear", "Mutually Assured Destruction"}, 11, "Brutal+4"},
    {153, "Resilient Rifts", "Rifts to Korhal", {"Just Die!", "Void Rifts"}, 17, "Brutal+5"},
    {154, "Chain Explosion", "Chain of Ascension", {"Lava Burst", "Walking Infested"}, 5, "Brutal+1"},
    {155, "Call It a Comeback", "Temple of the Past", {"Avenger", "Eminent Domain", "Just Die!"}, 13, "Brutal+4"},
    {156, "Explosive Hunt", "Part and Parcel", {"Minesweeper", "Slim Pickings"}, 11, "Brutal+4"},
    {157, "Tax Day", "Void Launch", {"Fear", "Micro Transactions"}, 8, "Brutal+2"},
    {158, "Aggressive Recruitment", "Malwarfare", {"Propagators", "Speed Freaks", "We Move Unseen"}, 13, "Brutal+4"},
    {159, "Scary Scavengers", "Dead of Night", {"Outbreak", "Slim Pickings"}, 8, "Brutal+2"},
    {160, "Call of the Void", "Void Thrashing", {"Void Reanimators", "Void Rifts"}, 15, "Brutal+5"},
    {161, "In the Name of Love", "Cradle of Death", {"Concussive Attacks", "Purifier Beam", "Time Warp"}, 4, "Brutal+1"},
    {162, "Locked and Loaded", "Lock & Load", {"Kill Bots", "Mineral Shields", "Temporal Field"}, 9, "Brutal+3"},
    {163, "Of Mines and Miners", "Miner Evacuation", {"Mag-nificent", "Minesweeper", "Missile Command"}, 13, "Brutal+4"},
    {164, "The League of Vermillians", "The Vermillion Problem", {"Evasive Maneuvers", "Heroes from the Storm"}, 11, "Brutal+4"},
    {165, "Operation Cooperation", "Lock & Load", {"Polarity", "Propagators"}, 15, "Brutal+5"},
    {166, "Hot n Cold", "Void Launch", {"Blizzard", "Lava Burst", "Scorched Earth"}, 9, "Brutal+3"},
    {167, "What We Do in the Shadows", "Void Thrashing", {"Darkness", "Shortsighted", "We Move Unseen"}, 6, "Brutal+1"},
    {168, "Hardware Malfunction", "Malwarfare", {"Kill Bots", "Laser Drill", "Minesweeper"}, 14, "Brutal+4"},
    {169, "Shared Pain", "Part and Parcel", {"Diffusion", "Fatal Attraction"}, 6, "Brutal+1"},
    {170, "The Longest Night", "Dead of Night", {"Just Die!", "Long Range", "Photon Overload"}, 10, "Brutal+3"},
    {171, "The Quick and the Undead", "Scythe of Amon", {"Speed Freaks", "Void Reanimators"}, 7, "Brutal+2"},
    {172, "Decade of Decadence", "Rifts to Korhal", {"Fireworks", "Gift Exchange", "Lucky Envelopes"}, -1, "-"},
    {173, "Bubble Pop", "Cradle of Death", {"Barrier", "Mineral Shields", "Time Warp"}, 5, "Brutal+1"},
    {174, "Coordinated Defense", "The Vermillion Problem", {"Missile Command", "Polarity"}, 10, "Brutal+3"},
    {175, "Beggars Can't Be Choosers", "Lock & Load", {"Mag-nificent", "Minesweeper", "Slim Pickings"}, 15, "Brutal+5"},
    {176, "My Bots", "Mist Opportunities", {"Boom Bots", "Kill Bots"}, 16, "Brutal+5"},
    {177, "Multitasking Trainer", "Malwarfare", {"Micro Transactions", "Void Rifts"}, 15, "Brutal+5"},
    {178, "Hello My Old Friend", "Scythe of Amon", {"Black Death", "Darkness", "Moment Of Silence"}, 11, "Brutal+4"},
    {179, "Of One Mind", "Rifts to Korhal", {"Boom Bots", "Polarity", "Sharing is Caring"}, -1, "-"},
    {180, "Blasting Off Again", "Void Launch", {"Going Nuclear", "Lava Burst", "Power Overwhelming"}, 11, "Brutal+4"},
    {181, "Enter the Nexus", "Cradle of Death", {"Blizzard", "Heroes from the Storm"}, 14, "Brutal+4"},
    {182, "Death is Fleeting", "Dead of Night", {"Just Die!", "Void Reanimators"}, 12, "Brutal+4"},
    {183, "Mass Manufacturing", "Part and Parcel", {"Propagators", "Void Rifts"}, 18, "Brutal+5"},
    {184, "Fire in the Hole", "Miner Evacuation", {"Boom Bots", "Missile Command"}, 13, "Brutal+4"},
};

// Reference: Week #132 (index 131: Robotic Revival) started 2026-11-09 10:00:00 UTC
inline constexpr int64_t kWeeklyCycleRefTimestamp = 1794218400LL;
inline constexpr int kWeeklyCycleRefIndex = 131; // 0-based index
inline constexpr int64_t kSecondsPerWeek = 7 * 86400LL;
inline constexpr int kTotalWeeklyMutations = 184;

inline int GetCurrentWeeklyMutationIndex(int64_t now_ts) {
    int64_t elapsed = now_ts - kWeeklyCycleRefTimestamp;
    int64_t weeks = (elapsed >= 0) ? (elapsed / kSecondsPerWeek) : ((elapsed - kSecondsPerWeek + 1) / kSecondsPerWeek);
    int idx = static_cast<int>((kWeeklyCycleRefIndex + (weeks % kTotalWeeklyMutations) + kTotalWeeklyMutations) % kTotalWeeklyMutations);
    return idx;
}

inline int64_t GetCurrentWeeklyMutationStart(int64_t now_ts) {
    int64_t elapsed = now_ts - kWeeklyCycleRefTimestamp;
    int64_t weeks = (elapsed >= 0) ? (elapsed / kSecondsPerWeek) : ((elapsed - kSecondsPerWeek + 1) / kSecondsPerWeek);
    return kWeeklyCycleRefTimestamp + weeks * kSecondsPerWeek;
}

inline int64_t GetUpcomingWeeklyMutationTimestamp(int week_idx, int64_t now_ts) {
    int curr_idx = GetCurrentWeeklyMutationIndex(now_ts);
    int weeks_until = (week_idx - curr_idx + kTotalWeeklyMutations) % kTotalWeeklyMutations;
    int64_t curr_start = GetCurrentWeeklyMutationStart(now_ts);
    return curr_start + weeks_until * kSecondsPerWeek;
}

inline std::string FormatUpcomingDate(int week_idx, int64_t now_ts) {
    int curr_idx = GetCurrentWeeklyMutationIndex(now_ts);
    if (week_idx == curr_idx) {
        return "Active";
    }
    int64_t ts = GetUpcomingWeeklyMutationTimestamp(week_idx, now_ts);
    std::time_t t = static_cast<std::time_t>(ts);
    std::tm tm_buf;
#ifdef _WIN32
    gmtime_s(&tm_buf, &t);
#else
    gmtime_r(&t, &tm_buf);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday);
    return std::string(buf);
}

} // namespace sc2
