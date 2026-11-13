#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <stdexcept>
#include <algorithm>

namespace sc2 {

class BitBuffer {
public:
    BitBuffer(const uint8_t* data, size_t size, bool big_endian = true)
        : m_data(data), m_size(size), m_used(0), m_next(0), m_nextbits(0), m_big_endian(big_endian) {}

    explicit BitBuffer(const std::vector<uint8_t>& vec, bool big_endian = true)
        : BitBuffer(vec.data(), vec.size(), big_endian) {}

    bool Done() const {
        return m_nextbits == 0 && m_used >= m_size;
    }

    size_t RemainingBytes() const {
        return m_used < m_size ? (m_size - m_used) : 0;
    }

    void ByteAlign() {
        m_nextbits = 0;
        m_next = 0;
    }

    uint32_t ReadBits(int bits) {
        if (bits <= 0) return 0;
        uint32_t result = 0;
        int resultbits = 0;

        while (resultbits != bits) {
            if (m_nextbits == 0) {
                if (m_used >= m_size) {
                    return result; // Truncated gracefully
                }
                m_next = m_data[m_used++];
                m_nextbits = 8;
            }

            int copybits = std::min(bits - resultbits, m_nextbits);
            uint32_t copy = (m_next & ((1u << copybits) - 1u));

            if (m_big_endian) {
                result |= copy << (bits - resultbits - copybits);
            } else {
                result |= copy << resultbits;
            }

            m_next >>= copybits;
            m_nextbits -= copybits;
            resultbits += copybits;
        }

        return result;
    }

    std::vector<uint8_t> ReadAlignedBytes(size_t count) {
        ByteAlign();
        std::vector<uint8_t> out;
        size_t available = (m_used < m_size) ? (m_size - m_used) : 0;
        size_t to_read = std::min(count, available);
        if (to_read > 0) {
            out.assign(m_data + m_used, m_data + m_used + to_read);
            m_used += to_read;
        }
        return out;
    }

    std::string ReadString(size_t length) {
        auto bytes = ReadAlignedBytes(length);
        return std::string(bytes.begin(), bytes.end());
    }

    int64_t ReadVInt() {
        uint32_t b = ReadBits(8);
        bool negative = (b & 1) != 0;
        int64_t result = (b >> 1) & 0x3f;
        int bits = 6;
        while ((b & 0x80) != 0 && bits < 64) {
            b = ReadBits(8);
            result |= static_cast<int64_t>(b & 0x7f) << bits;
            bits += 7;
        }
        return negative ? -result : result;
    }

    bool ReadBool() {
        return ReadBits(8) != 0;
    }

    uint8_t ReadU8() {
        auto bytes = ReadAlignedBytes(1);
        return bytes.empty() ? 0 : bytes[0];
    }

    uint32_t ReadU32() {
        auto bytes = ReadAlignedBytes(4);
        if (bytes.size() < 4) return 0;
        return (static_cast<uint32_t>(bytes[0]) << 24) |
               (static_cast<uint32_t>(bytes[1]) << 16) |
               (static_cast<uint32_t>(bytes[2]) << 8) |
               (static_cast<uint32_t>(bytes[3]));
    }

    uint64_t ReadU64() {
        auto bytes = ReadAlignedBytes(8);
        if (bytes.size() < 8) return 0;
        uint64_t res = 0;
        for (int i = 0; i < 8; ++i) {
            res = (res << 8) | bytes[i];
        }
        return res;
    }

private:
    const uint8_t* m_data;
    size_t m_size;
    size_t m_used;
    uint32_t m_next;
    int m_nextbits;
    bool m_big_endian;
};

} // namespace sc2
