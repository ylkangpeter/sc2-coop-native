#pragma once
#include "s2_decoder.h"
#include <algorithm>
#include <string_view>

namespace sc2 {
// Stable protocol indices: retry mutation indices are one-based; dialog buttons
// are arranged in four panels of fifteen entries.
inline const std::vector<std::string> kRetryMutationNames = {
    "Random",
    "Walking Infested",
    "Outbreak",
    "Darkness",
    "Time Warp",
    "Speed Freaks",
    "Mag-nificent",
    "Mineral Shields",
    "Barrier",
    "Avenger",
    "Evasive Maneuvers",
    "Scorched Earth",
    "Lava Burst",
    "Self Destruction",
    "Aggressive Deployment",
    "Alien Incubation",
    "Laser Drill",
    "Long Range",
    "Shortsighted",
    "Mutually Assured Destruction",
    "We Move Unseen",
    "Nap Time",
    "Slim Pickings",
    "Concussive Attacks",
    "Stone Zealots",
    "Just Die!",
    "Temporal Field",
    "Void Rifts",
    "Twister",
    "Orbital Strike",
    "Purifier Beam",
    "Blizzard",
    "Fear",
    "Photon Overload",
    "Minesweeper",
    "Chaos Studios",
    "Void Reanimators",
    "Going Nuclear",
    "Life Leech",
    "Power Overwhelming",
    "Micro Transactions",
    "Missile Command",
    "Vertigo",
    "Undying Evil",
    "Polarity",
    "Transmutation",
    "Afraid of the Dark",
    "Trick or Treat",
    "Turkey Shoot",
    "Sharing Is Caring",
    "Diffusion",
    "Black Death",
    "Eminent Domain",
    "Gift Exchange",
    "Naughty List",
    "Extreme Caution",
    "Insubordination",
    "Heroes from the Storm",
    "Inspiration",
    "Hardened Will",
    "Fireworks",
    "Lucky Envelopes",
    "Sluggishness",
    "Double-Edged",
    "Fatal Attraction",
    "Propagators",
    "Moment of Silence",
    "Kill Bots",
    "Boom Bots",
};
inline const std::vector<std::string> kDialogMutationNames = {
    "Random",
    "Walking Infested",
    "Outbreak",
    "Darkness",
    "Time Warp",
    "Speed Freaks",
    "Mag-nificent",
    "Mineral Shields",
    "Barrier",
    "Avenger",
    "Evasive Maneuvers",
    "Scorched Earth",
    "Lava Burst",
    "Self Destruction",
    "Aggressive Deployment",
    "Alien Incubation",
    "Laser Drill",
    "Long Range",
    "Shortsighted",
    "Mutually Assured Destruction",
    "We Move Unseen",
    "Slim Pickings",
    "Concussive Attacks",
    "Just Die!",
    "Temporal Field",
    "Void Rifts",
    "Twister",
    "Orbital Strike",
    "Purifier Beam",
    "Blizzard",
    "Fear",
    "Photon Overload",
    "Minesweeper",
    "Void Reanimators",
    "Going Nuclear",
    "Life Leech",
    "Power Overwhelming",
    "Micro Transactions",
    "Missile Command",
    "Vertigo",
    "Polarity",
    "Transmutation",
    "Diffusion",
    "Black Death",
    "Eminent Domain",
    "Heroes from the Storm",
    "Inspiration",
    "Hardened Will",
    "Double-Edged",
    "Fatal Attraction",
    "Propagators",
    "Moment of Silence",
    "Kill Bots",
    "Boom Bots",
};
inline std::string NormalizeMutation(std::string name) {
    if (name == "Heroes from the Storm (old)") return "Heroes from the Storm";
    if (name == "Extreme Caution") return "Afraid of the Dark";
    if (name == "Moment Of Silence") return "Moment of Silence";
    if (name == "Sharing is Caring") return "Sharing Is Caring";
    return name;
}
inline void ApplyMatchMutators(ReplayRecord& record, const std::vector<DecodedSlot>& slots,
                               bool extension, bool weekly, const std::vector<std::string>& cache_mutators,
                               const std::vector<LobbyAction>& actions, int start_loop,
                               const std::vector<std::string>& tracker_mutators) {
    record.mutators.clear();
    auto add = [&](std::string name, bool repeat = false) {
        name = NormalizeMutation(std::move(name));
        if (repeat || std::find(record.mutators.begin(), record.mutators.end(), name) == record.mutators.end())
            record.mutators.push_back(std::move(name));
    };
    record.is_weekly = extension && weekly;
    record.is_custom_mutation = extension && !record.is_weekly;
    if (record.is_weekly) for (const auto& name : cache_mutators) add(name);
    if (!extension && record.brutal_plus > 0) {
        for (const auto& slot : slots) if (slot.control == 2 && slot.brutal_plus > 0) {
            for (int index : slot.retry_mutators)
                if (index > 0 && static_cast<size_t>(index) <= kRetryMutationNames.size()) add(kRetryMutationNames[index - 1]);
            break;
        }
    }
    if (extension && !record.is_weekly) {
        int panel = 0, last_loop = -1;
        for (const auto& action : actions) {
            if (action.loop <= 0 || (start_loop >= 0 && action.loop >= start_loop) || action.loop == last_loop) continue;
            last_loop = action.loop;
            int control = action.control;
            if (control >= 41 && control <= 83) {
                size_t index = static_cast<size_t>((control - 41) / 3 + panel * 15);
                if (index >= kDialogMutationNames.size()) continue;
                auto name = NormalizeMutation(kDialogMutationNames[index]);
                auto found = std::find(record.mutators.begin(), record.mutators.end(), name);
                if (found != record.mutators.end() && name != "Random") record.mutators.erase(found);
                else add(name, name == "Random");
            } else if (control == 123 && panel > 0) --panel;
            else if (control == 124 && panel < 3) ++panel;
            else if (control >= 88 && control <= 106) {
                size_t index = static_cast<size_t>((control - 88) / 2);
                if (index < record.mutators.size()) record.mutators.erase(record.mutators.begin() + index);
            }
        }
    }
    for (const auto& name : tracker_mutators) add(name);
    if (!tracker_mutators.empty() && !record.is_weekly && record.brutal_plus == 0) record.is_custom_mutation = true;
}
} // namespace sc2
