#include "replay/tracker_army_value.h"
#include <cassert>
#include <iostream>
int main() {
    sc2::PlayerInfo raynor; raynor.commander="Raynor";
    sc2::TrackerArmyValue base(raynor);
    base.Born("Marine", ""); base.Born("Marine", "");
    assert(base.Value()==100);
    base.Died("Marine", false);
    assert(base.Value()==50);
    raynor.prestige_name="Rebel Raider";
    sc2::TrackerArmyValue rebel(raynor);
    rebel.Born("Marine", "");
    assert(rebel.Value()==75);
    sc2::PlayerInfo kerrigan; kerrigan.commander="Kerrigan"; kerrigan.masteries[2]=30;
    sc2::TrackerArmyValue discounted(kerrigan);
    discounted.Born("HydraliskLurker", "");
    assert(discounted.Value()==135);
    sc2::PlayerInfo tychus; tychus.commander="Tychus";
    sc2::TrackerArmyValue outlaws(tychus);
    outlaws.Born("TychusReaper", "");
    assert(outlaws.Value()==600);
    outlaws.Died("TychusReaper", false);
    assert(outlaws.Value()==600);
    std::cout << "Tracker army value tests passed.\n";
}
