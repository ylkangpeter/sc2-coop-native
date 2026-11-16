#pragma once

#include "bit_buffer.h"
#include <cstdint>
#include <cstddef>

namespace sc2 {

inline void SkipValue(BitBuffer& buf, uint32_t val_type) {
    switch (val_type) {
        case 0: { // array
            int64_t count = buf.ReadVInt();
            for (int64_t i = 0; i < count && !buf.Done(); ++i) {
                if (buf.Done()) break;
                uint32_t elem_type = buf.ReadBits(8);
                SkipValue(buf, elem_type);
            }
            break;
        }
        case 1: { // bitblob
            int64_t bitlen = buf.ReadVInt();
            if (bitlen > 0) {
                buf.ReadAlignedBytes(static_cast<size_t>((bitlen + 7) / 8));
            }
            break;
        }
        case 2: { // blob
            int64_t len = buf.ReadVInt();
            if (len > 0) {
                buf.ReadAlignedBytes(static_cast<size_t>(len));
            }
            break;
        }
        case 3: { // choice
            buf.ReadVInt(); // tag
            uint32_t elem_type = buf.ReadBits(8);
            SkipValue(buf, elem_type);
            break;
        }
        case 4: { // optional
            bool exists = buf.ReadBool();
            if (exists) {
                uint32_t elem_type = buf.ReadBits(8);
                SkipValue(buf, elem_type);
            }
            break;
        }
        case 5: { // struct
            int64_t field_count = buf.ReadVInt();
            for (int64_t i = 0; i < field_count && !buf.Done(); ++i) {
                buf.ReadVInt(); // tag
                uint32_t elem_type = buf.ReadBits(8);
                SkipValue(buf, elem_type);
            }
            break;
        }
        case 6: // u8
            buf.ReadU8();
            break;
        case 7: // u32 / fourcc / real32
            buf.ReadU32();
            break;
        case 8: // u64 / real64
            buf.ReadU64();
            break;
        case 9: // vint
            buf.ReadVInt();
            break;
        default:
            break;
    }
}

} // namespace sc2
