#include "s2_decoder.h"
#include "s2_proto_utils.h"
#include <sstream>

namespace sc2 {

void S2Decoder::SkipType(BitBuffer& buf) {
    if (buf.Done()) return;
    uint32_t type = buf.ReadBits(8);
    SkipValue(buf, type);
}

std::vector<DecodedPlayer> S2Decoder::DecodeDetails(const std::vector<uint8_t>& details_bytes) {
    std::vector<DecodedPlayer> players;
    if (details_bytes.empty()) return players;

    BitBuffer buf(details_bytes);
    uint32_t top_type = buf.ReadBits(8);
    if (top_type != 5) return players; // Must be struct

    int64_t root_fields = buf.ReadVInt();
    for (int64_t f = 0; f < root_fields && !buf.Done(); ++f) {
        int64_t root_tag = buf.ReadVInt();
        uint32_t list_type = buf.ReadBits(8);

        if (root_tag == 0) { // m_playerList
            if (list_type == 4) { // optional!
                bool exists = buf.ReadBool();
                if (!exists) continue;
                list_type = buf.ReadBits(8); // array type
            }

            if (list_type == 0) { // array
                int64_t player_count = buf.ReadVInt();
                for (int64_t p = 0; p < player_count && !buf.Done(); ++p) {
                    uint32_t p_type = buf.ReadBits(8);
                    if (p_type != 5) {
                        SkipValue(buf, p_type);
                        continue;
                    }

                    DecodedPlayer player;
                    player.player_id = static_cast<int>(p + 1);

                    int64_t p_fields = buf.ReadVInt();
                    for (int64_t pf = 0; pf < p_fields && !buf.Done(); ++pf) {
                        int64_t pf_tag = buf.ReadVInt();
                        uint32_t val_type = buf.ReadBits(8);

                        if (pf_tag == 0 && val_type == 2) { // m_name
                            int64_t len = buf.ReadVInt();
                            player.name = buf.ReadString(static_cast<size_t>(len));
                        } else if (pf_tag == 1 && val_type == 5) { // m_toon
                            int64_t toon_fields = buf.ReadVInt();
                            int region_id = 0;
                            uint64_t toon_id = 0;
                            uint32_t realm_id = 0;

                            for (int64_t tf = 0; tf < toon_fields && !buf.Done(); ++tf) {
                                int64_t t_tag = buf.ReadVInt();
                                uint32_t t_type = buf.ReadBits(8);
                                if (t_tag == 0 && t_type == 9) { // m_region
                                    region_id = static_cast<int>(buf.ReadVInt());
                                } else if (t_tag == 2 && t_type == 9) { // m_realm
                                    realm_id = static_cast<uint32_t>(buf.ReadVInt());
                                } else if (t_tag == 4 && (t_type == 7 || t_type == 9)) { // m_id
                                    toon_id = (t_type == 7) ? buf.ReadU32() : static_cast<uint64_t>(buf.ReadVInt());
                                } else {
                                    SkipValue(buf, t_type);
                                }
                            }
                            if (region_id == 1) player.region = "NA";
                            else if (region_id == 2) player.region = "EU";
                            else if (region_id == 3) player.region = "KR";
                            else if (region_id == 5) player.region = "CN";
                            else player.region = "Global";

                            std::ostringstream oss;
                            oss << region_id << "-S2-" << realm_id << "-" << toon_id;
                            player.handle = oss.str();
                        } else if (pf_tag == 2 && val_type == 2) { // m_race
                            int64_t len = buf.ReadVInt();
                            player.race = buf.ReadString(static_cast<size_t>(len));
                        } else if (pf_tag == 7 && val_type == 9) { // m_observe
                            player.is_observer = (buf.ReadVInt() != 0);
                        } else if (pf_tag == 8 && val_type == 9) { // m_result
                            player.result = static_cast<int>(buf.ReadVInt());
                        } else {
                            SkipValue(buf, val_type);
                        }
                    }
                    players.push_back(player);
                }
            } else {
                SkipValue(buf, list_type);
            }
        } else {
            SkipValue(buf, list_type);
        }
    }

    return players;
}

} // namespace sc2
