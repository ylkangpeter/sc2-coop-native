#include "replay/s2_decoder.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

class TrackerEvents {
public:
    std::vector<uint8_t> data;
    void vint(int64_t value) {
        bool negative = value < 0;
        uint64_t n = negative ? -value : value;
        uint8_t first = static_cast<uint8_t>(((n & 63) << 1) | negative);
        n >>= 6;
        data.push_back(first | (n ? 128 : 0));
        while (n) {
            uint8_t next = n & 127;
            n >>= 7;
            data.push_back(next | (n ? 128 : 0));
        }
    }
    void event(int delta, int id, int fields) {
        data.push_back(3); vint(0); data.push_back(9); vint(delta);
        data.push_back(9); vint(id); data.push_back(5); vint(fields);
    }
    void number(int tag, int value) { vint(tag); data.push_back(9); vint(value); }
    void str(int tag, const std::string& value) {
        vint(tag); data.push_back(2); vint(value.size());
        data.insert(data.end(), value.begin(), value.end());
    }
    void unit(int id, int tag, const char* type, int owner) {
        event(1, id, 4); number(0,tag); number(1,0); str(2,type); number(3,owner);
    }
    void stats(int player) {
        event(1,0,2); number(0,player); vint(1); data.push_back(5); vint(3);
        number(2,100); number(3,50); number(29,4096);
    }
};

int main() {
    TrackerEvents events;
    events.unit(1,1,"Marine",3);
    events.event(1,3,3); events.number(0,1); events.number(1,0); events.number(2,1);
    events.unit(6,2,"Refinery",1);
    events.event(1,7,2); events.number(0,2); events.number(1,0);
    events.event(1,5,2); events.number(0,1); events.str(1,"TychusSpectreVisionSuit");
    events.stats(1);
    events.event(1,2,3); events.number(0,1); events.number(1,0); events.number(2,3);
    events.stats(1);
    sc2::PlayerInfo p1, p2; p1.commander="Tychus";
    const std::vector<sc2::DroneOrder> drones = {{5,2u << 18}};
    auto result = sc2::S2Decoder::DecodeTrackerEvents(events.data,p1,p2,"Rifts to Korhal",drones);
    assert(result.complete);
    assert(result.p1_series.army_value.size() == 2);
    assert(result.p1_series.army_value[0] == 750);
    assert(std::abs(result.p1_series.collection_rate[0]-169.055f) < 0.01f);
    assert(result.amon_units.size() >= 1);
    bool controlled = false;
    for (const auto& item : result.p1_contributions)
        if (item.icon == "mc" && item.count == 1) controlled = true;
    assert(controlled);
    auto cutoff = sc2::S2Decoder::DecodeTrackerEvents(events.data,p1,p2,"Rifts to Korhal",drones,6);
    assert(cutoff.complete && cutoff.p1_series.army_value.size() == 1);
    std::cout << "Tracker owner, upgrade, completion and cutoff tests passed.\n";
}
