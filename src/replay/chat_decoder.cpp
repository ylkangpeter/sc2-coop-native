#include "s2_decoder.h"

namespace sc2 {

std::vector<DecodedChatMessage> S2Decoder::DecodeMessageEvents(const std::vector<uint8_t>& msg_bytes) {
    std::vector<DecodedChatMessage> messages;
    if (msg_bytes.empty()) return messages;

    BitBuffer buf(msg_bytes);
    int current_gameloop = 0;

    while (!buf.Done() && buf.RemainingBytes() > 0) {
        // 1. svaruint32 gameloop delta (choice of 2 bits tag, followed by 6, 14, 22, or 32 bits)
        uint32_t tag = buf.ReadBits(2);
        uint32_t delta = 0;
        if (tag == 0) {
            delta = buf.ReadBits(6);
        } else if (tag == 1) {
            delta = buf.ReadBits(14);
        } else if (tag == 2) {
            delta = buf.ReadBits(22);
        } else if (tag == 3) {
            delta = buf.ReadBits(32);
        }
        current_gameloop += static_cast<int>(delta);

        // 2. replay_userid (5 bits)
        uint32_t user_id = buf.ReadBits(5);

        // 3. message_eventid (4 bits)
        uint32_t event_id = buf.ReadBits(4);

        if (event_id == 0) { // NNet.Game.SChatMessage
            uint32_t recipient = buf.ReadBits(3);
            (void)recipient;
            uint32_t text_len = buf.ReadBits(11);
            buf.ByteAlign();
            std::string text = buf.ReadString(text_len);
            buf.ByteAlign();

            if (!text.empty()) {
                DecodedChatMessage msg;
                msg.gameloop = current_gameloop;
                msg.time_seconds = current_gameloop / 16.0;
                msg.player_id = static_cast<int>(user_id + 1);
                msg.text = text;
                messages.push_back(msg);
            }
        } else if (event_id == 1) { // NNet.Game.SPingMessage
            uint32_t recipient = buf.ReadBits(3);
            (void)recipient;
            buf.ReadBits(32); // point.x
            buf.ReadBits(32); // point.y
            buf.ByteAlign();
        } else if (event_id == 2) { // NNet.Game.SLoadingProgressMessage
            buf.ReadBits(32); // progress
            buf.ByteAlign();
        } else if (event_id == 3) { // NNet.Game.SServerPingMessage
            // empty struct
            buf.ByteAlign();
        } else if (event_id == 4) { // NNet.Game.SReconnectNotifyMessage
            buf.ReadBits(2); // status
            buf.ByteAlign();
        } else {
            // Unknown event id, terminate loop safely
            break;
        }
    }

    return messages;
}

} // namespace sc2
