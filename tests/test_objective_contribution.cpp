#include "../src/replay/objective_contribution_tracker.h"
#include <cassert>
#include <string>

static int count(const std::vector<sc2::Contribution>& items, const std::string& key) {
    for (const auto& item : items) if (item.icon == key) return item.count;
    return -1;
}
int main() {
    sc2::ObjectiveContributionTracker t("Part and Parcel");
    assert(t.BonusKnown());
    t.Born(1, "TychusReaper", 2, 0);
    t.Born(2, "TychusMedic", 2, 0);
    t.Born(3, "TychusReaper", 2, 1);
    t.Born(4, "ZeratulArtifactPickup1", 1, 0);
    t.Died(4, 0, -1, 0, 0, 10);
    t.Born(5, "ZeratulPhotonCannonProjection", 1, 0);
    t.Born(6, "MutatorVoidRift", 3, 0);
    t.Died(6, 1, -1, 0, 0, 20);
    t.Born(7, "MutatorSpiderMine", 3, 0);
    t.Born(8, "Marine", 1, 0);
    t.Died(8, 3, 7, 0, 0, 30);
    t.Born(9, "Marine", 2, 0);
    t.Died(9, 3, 7, 0, 0, 31);
    t.Born(10, "Enemy", 3, 0);
    t.OwnerChange(10, 2, 40);
    t.Born(11, "TarsonisEngine", 8, 0);
    t.Died(11, 0, -1, 100, 100, 160);
    t.Born(12, "Caboose", 8, 0);
    t.Died(12, 0, -1, 100, 100, 161);
    assert(t.BonusLoops().size() == 1);
    auto p1 = t.Contributions(1), p2 = t.Contributions(2);
    assert(count(p1, "Artifact") == 1 && count(p1, "ShadeProjection") == 1);
    assert(count(p1, "voidrifts") == 1 && count(p1, "minesweeper") == 1);
    assert(count(p2, "minesweeper") == -1 && count(p2, "mc") == 1);
    assert(p2[0].icon == "TychusReaper" && p2[1].icon == "TychusMedic");
    sc2::ObjectiveContributionTracker spawned("Dead of Night");
    for (int i = 0; i < 199; ++i) spawned.Born(1000 + i, "Locust", 1, i);
    assert(count(spawned.Contributions(1), "locust") == -1);
    spawned.Born(1199, "DehakaLocust", 1, 199, "CoopMurvarSpawnCreepers");
    assert(count(spawned.Contributions(1), "locust") == -1);
    spawned.Born(1200, "Locust", 1, 200);
    assert(count(spawned.Contributions(1), "locust") == 200);
    spawned.Born(1300, "WidowMine", 3, 0);
    spawned.Died(1300, 1, -1, 0, 0, 1);
    assert(count(spawned.Contributions(1), "minesweeper") == -1);
    spawned.Born(1301, "MutatorSpiderMine", 3, 0);
    assert(count(spawned.Contributions(1), "minesweeper") == 1);
    spawned.Died(1300, 1, -1, 0, 0, 2);
    assert(count(spawned.Contributions(1), "minesweeper") == 1);
    // Real Miner Evacuation event patterns: both aircraft, only the ship,
    // one aircraft followed by the other despawning, and neither objective.
    auto miner = [](int first, int second, int ship) {
        sc2::ObjectiveContributionTracker m("Miner Evacuation");
        m.Born(286, "NovaEradicator", 9, 0);
        m.Born(308, "NovaEradicator", 9, 0);
        m.Born(289, "Blightbringer", 5, 0);
        m.Died(286, first, -1, 0, 0, 739 * 16);
        m.Died(308, second, -1, 0, 0, 752 * 16);
        m.Died(289, ship, -1, 0, 0, 986 * 16);
        return m.BonusLoops();
    };
    auto both = miner(1, 2, 1);
    assert(both.size() == 2 && both[0] == 752 * 16);
    assert(miner(0, 0, 2).size() == 1);
    assert(miner(2, 0, 0).empty());
    assert(miner(0, 0, 0).empty());
    sc2::ObjectiveContributionTracker bunker("Void Thrashing");
    bunker.Born(1, "SIInfestedBunker", 1, 0);
    assert(count(bunker.Contributions(1), "Infested Bunker") == 1);
    sc2::ObjectiveContributionTracker v("Void Launch");
    v.Born(20, "ResearchVessel", 7, 0);
    v.TypeChange(20, "ResearchVesselLanded", 100);
    v.TypeChange(20, "ResearchVessel", 200);
    assert(v.BonusLoops().size() == 1 && v.BonusLoops()[0] == 200);
    sc2::ObjectiveContributionTracker m("Malwarfare");
    m.Born(21, "Bonus", 3, 0);
    m.OwnerChange(21, 9, 100);
    m.OwnerChange(21, 6, 4035);
    assert(m.BonusLoops().empty());
    sc2::ObjectiveContributionTracker soa("Scythe of Amon");
    soa.Born(30, "WarpPrism", 11, 0);
    // 1st sanctuary completed: enters phasing mode at 8305
    soa.TypeChange(30, "WarpPrismPhasing", 8305);
    // Duplicate event should be ignored
    soa.TypeChange(30, "WarpPrismPhasing", 8306);
    assert(soa.BonusLoops().size() == 1 && soa.BonusLoops()[0] == 8305);
    // Departs 1st sanctuary back to transport mode
    soa.TypeChange(30, "WarpPrism", 9000);
    assert(soa.BonusLoops().size() == 1);
    // 2nd sanctuary completed: enters phasing mode at 13815
    soa.TypeChange(30, "WarpPrismPhasing", 13815);
    assert(soa.BonusLoops().size() == 2);
    assert(soa.BonusLoops()[0] == 8305);
    assert(soa.BonusLoops()[1] == 13815);

    assert(!sc2::ObjectiveContributionTracker("Unknown").BonusKnown());
}
