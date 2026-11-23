#include "../src/replay/match_mutators.h"
#include <cassert>
int main() {
    sc2::ReplayRecord record;
    sc2::DecodedSlot slot;
    slot.control = 2;
    slot.brutal_plus = 2;
    slot.retry_mutators = {2, 3, 0, 500};
    record.brutal_plus = 2;
    sc2::ApplyMatchMutators(record, {slot}, false, false, {}, {}, -1, {});
    assert(record.mutators.size() == 2 && record.mutators[0] == "Walking Infested" && record.mutators[1] == "Outbreak");
    assert(!record.is_weekly && !record.is_custom_mutation);
    record.brutal_plus = 0;
    sc2::ApplyMatchMutators(record, {}, true, true, {"Void Rifts"}, {{10, 41}}, 100, {});
    assert(record.is_weekly && !record.is_custom_mutation && record.mutators.size() == 1 && record.mutators[0] == "Void Rifts");
    sc2::ApplyMatchMutators(record, {}, true, false, {}, {{10, 44}, {11, 44}, {12, 47}, {13, 124}, {14, 41}, {100, 41}}, 100, {});
    assert(!record.is_weekly && record.is_custom_mutation && record.mutators.size() == 2);
    assert(record.mutators[0] == sc2::kDialogMutationNames[2]);
    assert(record.mutators[1] == sc2::kDialogMutationNames[15]);
    sc2::ApplyMatchMutators(record, {}, false, false, {}, {}, -1, {"Void Rifts", "Void Rifts"});
    assert(record.is_custom_mutation && record.mutators.size() == 1);
}
