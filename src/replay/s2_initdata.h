#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>

namespace sc2 {

struct DecodedInitSlot {
    std::string commander;
    int commander_level = 0;
    int commander_mastery_level = 0;
    std::vector<int> masteries;
    int prestige = 0;
    int brutal_plus = 0;
    int difficulty = 0; // 1=Casual, 2=Normal, 3=Hard, 4=Brutal
    std::string toon_handle;
};

class S2BitPackedBuffer {
public:
    const uint8_t* m_data = nullptr;
    size_t m_size = 0;
    size_t m_used = 0;
    uint32_t m_next = 0;
    int m_nextbits = 0;

    S2BitPackedBuffer(const uint8_t* data, size_t size) : m_data(data), m_size(size) {}

    bool done() const { return m_nextbits == 0 && m_used >= m_size; }
    void byte_align() { m_nextbits = 0; }

    std::string read_aligned_bytes(size_t bytes) {
        byte_align();
        if (m_used + bytes > m_size) {
            m_used = m_size;
            return "";
        }
        std::string res(reinterpret_cast<const char*>(m_data + m_used), bytes);
        m_used += bytes;
        return res;
    }

    uint64_t read_bits(int bits) {
        uint64_t result = 0;
        int resultbits = 0;
        while (resultbits != bits) {
            if (m_nextbits == 0) {
                if (m_used >= m_size) return 0;
                m_next = m_data[m_used++];
                m_nextbits = 8;
            }
            int copybits = std::min(bits - resultbits, m_nextbits);
            uint32_t copy = m_next & ((1u << copybits) - 1);
            result |= (static_cast<uint64_t>(copy) << (bits - resultbits - copybits));
            m_next >>= copybits;
            m_nextbits -= copybits;
            resultbits += copybits;
        }
        return result;
    }
};

class S2InitDataDecoder {
public:
    static bool Decode(const std::vector<uint8_t>& bytes, std::vector<DecodedInitSlot>& out_slots) {
        out_slots.clear();
        if (bytes.empty()) return false;
        S2BitPackedBuffer buf(bytes.data(), bytes.size());
        try {
            DecodeType(73, buf, out_slots);
            return !out_slots.empty();
        } catch (...) {
            return false;
        }
    }

private:
    static int64_t DecodeInt(int64_t min_val, int bits, S2BitPackedBuffer& buf) {
        return min_val + static_cast<int64_t>(buf.read_bits(bits));
    }

    static void DecodeType(int tid, S2BitPackedBuffer& buf, std::vector<DecodedInitSlot>& out_slots) {
        switch (tid) {
            case 1: { // _int
                DecodeInt(0LL, 4, buf);
                break;
            }
            case 2: { // _int
                DecodeInt(0LL, 5, buf);
                break;
            }
            case 3: { // _int
                DecodeInt(0LL, 6, buf);
                break;
            }
            case 6: { // _int
                DecodeInt(0LL, 32, buf);
                break;
            }
            case 9: { // _blob
                size_t len = static_cast<size_t>(DecodeInt(0LL, 8, buf));
                buf.read_aligned_bytes(len);
                break;
            }
            case 10: { // _int
                DecodeInt(0LL, 8, buf);
                break;
            }
            case 12: { // _int
                DecodeInt(0LL, 3, buf);
                break;
            }
            case 13: { // _bool
                buf.read_bits(1);
                break;
            }
            case 20: { // _blob
                size_t len = static_cast<size_t>(DecodeInt(0LL, 7, buf));
                buf.read_aligned_bytes(len);
                break;
            }
            case 21: { // _int
                DecodeInt(0LL, 64, buf);
                break;
            }
            case 24: { // _int
                DecodeInt(0LL, 2, buf);
                break;
            }
            case 25: { // _optional
                if (buf.read_bits(1) != 0) DecodeType(10, buf, out_slots);
                break;
            }
            case 29: { // _blob
                size_t len = static_cast<size_t>(DecodeInt(0LL, 10, buf));
                buf.read_aligned_bytes(len);
                break;
            }
            case 30: { // _blob
                size_t len = static_cast<size_t>(DecodeInt(0LL, 11, buf));
                buf.read_aligned_bytes(len);
                break;
            }
            case 35: { // _blob
                buf.read_aligned_bytes(40);
                break;
            }
            case 36: { // _array
                int64_t count = DecodeInt(0LL, 6, buf);
                for (int64_t i = 0; i < count; ++i) {
                    DecodeType(35, buf, out_slots);
                }
                break;
            }
            case 41: { // _optional
                if (buf.read_bits(1) != 0) DecodeType(9, buf, out_slots);
                break;
            }
            case 42: { // _optional
                if (buf.read_bits(1) != 0) DecodeType(35, buf, out_slots);
                break;
            }
            case 43: { // _optional
                if (buf.read_bits(1) != 0) DecodeType(6, buf, out_slots);
                break;
            }
            case 44: { // _struct
                DecodeType(25, buf, out_slots); // m_race
                break;
            }
            case 45: { // _struct
                DecodeType(25, buf, out_slots); // m_team
                break;
            }
            case 46: { // _blob
                size_t len = static_cast<size_t>(DecodeInt(0LL, 9, buf));
                buf.read_aligned_bytes(len);
                break;
            }
            case 47: { // _int
                DecodeInt(-2147483648LL, 32, buf);
                break;
            }
            case 48: { // _optional
                if (buf.read_bits(1) != 0) DecodeType(47, buf, out_slots);
                break;
            }
            case 49: { // _struct
                DecodeType(9, buf, out_slots); // m_name
                DecodeType(41, buf, out_slots); // m_clanTag
                DecodeType(42, buf, out_slots); // m_clanLogo
                DecodeType(25, buf, out_slots); // m_highestLeague
                DecodeType(43, buf, out_slots); // m_combinedRaceLevels
                DecodeType(6, buf, out_slots); // m_randomSeed
                DecodeType(44, buf, out_slots); // m_racePreference
                DecodeType(45, buf, out_slots); // m_teamPreference
                DecodeType(13, buf, out_slots); // m_testMap
                DecodeType(13, buf, out_slots); // m_testAuto
                DecodeType(13, buf, out_slots); // m_examine
                DecodeType(13, buf, out_slots); // m_customInterface
                DecodeType(6, buf, out_slots); // m_testType
                DecodeType(24, buf, out_slots); // m_observe
                DecodeType(46, buf, out_slots); // m_hero
                DecodeType(46, buf, out_slots); // m_skin
                DecodeType(46, buf, out_slots); // m_mount
                DecodeType(20, buf, out_slots); // m_toonHandle
                DecodeType(48, buf, out_slots); // m_scaledRating
                break;
            }
            case 50: { // _array
                int64_t count = DecodeInt(0LL, 5, buf);
                for (int64_t i = 0; i < count; ++i) {
                    DecodeType(49, buf, out_slots);
                }
                break;
            }
            case 51: { // _struct
                DecodeType(13, buf, out_slots); // m_lockTeams
                DecodeType(13, buf, out_slots); // m_teamsTogether
                DecodeType(13, buf, out_slots); // m_advancedSharedControl
                DecodeType(13, buf, out_slots); // m_randomRaces
                DecodeType(13, buf, out_slots); // m_battleNet
                DecodeType(13, buf, out_slots); // m_amm
                DecodeType(13, buf, out_slots); // m_competitive
                DecodeType(13, buf, out_slots); // m_practice
                DecodeType(13, buf, out_slots); // m_cooperative
                DecodeType(13, buf, out_slots); // m_noVictoryOrDefeat
                DecodeType(13, buf, out_slots); // m_heroDuplicatesAllowed
                DecodeType(24, buf, out_slots); // m_fog
                DecodeType(24, buf, out_slots); // m_observers
                DecodeType(24, buf, out_slots); // m_userDifficulty
                DecodeType(21, buf, out_slots); // m_clientDebugFlags
                DecodeType(13, buf, out_slots); // m_buildCoachEnabled
                break;
            }
            case 52: { // _int
                DecodeInt(1LL, 4, buf);
                break;
            }
            case 53: { // _int
                DecodeInt(1LL, 8, buf);
                break;
            }
            case 54: { // _bitarray
                int64_t len = DecodeInt(0LL, 6, buf);
                for (int64_t i = 0; i < len; ++i) buf.read_bits(1);
                break;
            }
            case 55: { // _bitarray
                int64_t len = DecodeInt(0LL, 8, buf);
                for (int64_t i = 0; i < len; ++i) buf.read_bits(1);
                break;
            }
            case 56: { // _bitarray
                int64_t len = DecodeInt(0LL, 2, buf);
                for (int64_t i = 0; i < len; ++i) buf.read_bits(1);
                break;
            }
            case 57: { // _struct
                DecodeType(54, buf, out_slots); // m_allowedColors
                DecodeType(55, buf, out_slots); // m_allowedRaces
                DecodeType(54, buf, out_slots); // m_allowedDifficulty
                DecodeType(55, buf, out_slots); // m_allowedControls
                DecodeType(56, buf, out_slots); // m_allowedObserveTypes
                DecodeType(55, buf, out_slots); // m_allowedAIBuilds
                break;
            }
            case 58: { // _array
                int64_t count = DecodeInt(0LL, 5, buf);
                for (int64_t i = 0; i < count; ++i) {
                    DecodeType(57, buf, out_slots);
                }
                break;
            }
            case 59: { // _struct
                DecodeType(6, buf, out_slots); // m_randomValue
                DecodeType(29, buf, out_slots); // m_gameCacheName
                DecodeType(51, buf, out_slots); // m_gameOptions
                DecodeType(12, buf, out_slots); // m_gameSpeed
                DecodeType(12, buf, out_slots); // m_gameType
                DecodeType(2, buf, out_slots); // m_maxUsers
                DecodeType(2, buf, out_slots); // m_maxObservers
                DecodeType(2, buf, out_slots); // m_maxPlayers
                DecodeType(52, buf, out_slots); // m_maxTeams
                DecodeType(3, buf, out_slots); // m_maxColors
                DecodeType(53, buf, out_slots); // m_maxRaces
                DecodeType(10, buf, out_slots); // m_maxControls
                DecodeType(10, buf, out_slots); // m_mapSizeX
                DecodeType(10, buf, out_slots); // m_mapSizeY
                DecodeType(6, buf, out_slots); // m_mapFileSyncChecksum
                DecodeType(30, buf, out_slots); // m_mapFileName
                DecodeType(9, buf, out_slots); // m_mapAuthorName
                DecodeType(6, buf, out_slots); // m_modFileSyncChecksum
                DecodeType(58, buf, out_slots); // m_slotDescriptions
                DecodeType(3, buf, out_slots); // m_defaultDifficulty
                DecodeType(10, buf, out_slots); // m_defaultAIBuild
                DecodeType(36, buf, out_slots); // m_cacheHandles
                DecodeType(13, buf, out_slots); // m_hasExtensionMod
                DecodeType(13, buf, out_slots); // m_hasNonBlizzardExtensionMod
                DecodeType(13, buf, out_slots); // m_isBlizzardMap
                DecodeType(13, buf, out_slots); // m_isPremadeFFA
                DecodeType(13, buf, out_slots); // m_isCoopMode
                DecodeType(13, buf, out_slots); // m_isRealtimeMode
                break;
            }
            case 60: { // _optional
                if (buf.read_bits(1) != 0) DecodeType(1, buf, out_slots);
                break;
            }
            case 61: { // _optional
                if (buf.read_bits(1) != 0) DecodeType(2, buf, out_slots);
                break;
            }
            case 62: { // _struct
                DecodeType(61, buf, out_slots); // m_color
                break;
            }
            case 63: { // _array
                int64_t count = DecodeInt(0LL, 4, buf);
                for (int64_t i = 0; i < count; ++i) {
                    DecodeType(46, buf, out_slots);
                }
                break;
            }
            case 64: { // _array
                int64_t count = DecodeInt(0LL, 17, buf);
                for (int64_t i = 0; i < count; ++i) {
                    DecodeType(6, buf, out_slots);
                }
                break;
            }
            case 65: { // _array
                int64_t count = DecodeInt(0LL, 16, buf);
                for (int64_t i = 0; i < count; ++i) {
                    DecodeType(6, buf, out_slots);
                }
                break;
            }
            case 66: { // _array
                int64_t count = DecodeInt(0LL, 3, buf);
                for (int64_t i = 0; i < count; ++i) {
                    DecodeType(6, buf, out_slots);
                }
                break;
            }
            case 67: DecodeInt(0, 32, buf); break;
            case 68: { // _array
                int64_t count = DecodeInt(0LL, 17, buf);
                for (int64_t i = 0; i < count; ++i) {
                    DecodeType(67, buf, out_slots);
                }
                break;
            }
            case 69: { // _struct
                DecodeType(10, buf, out_slots); // m_control
                DecodeType(60, buf, out_slots); // m_userId
                DecodeType(1, buf, out_slots); // m_teamId
                DecodeType(62, buf, out_slots); // m_colorPref
                DecodeType(44, buf, out_slots); // m_racePref
                DecodeType(3, buf, out_slots); // m_difficulty
                DecodeType(10, buf, out_slots); // m_aiBuild
                DecodeType(6, buf, out_slots); // m_handicap
                DecodeType(24, buf, out_slots); // m_observe
                DecodeType(6, buf, out_slots); // m_logoIndex
                DecodeType(46, buf, out_slots); // m_hero
                DecodeType(46, buf, out_slots); // m_skin
                DecodeType(46, buf, out_slots); // m_mount
                DecodeType(63, buf, out_slots); // m_artifacts
                DecodeType(25, buf, out_slots); // m_workingSetSlotId
                DecodeType(64, buf, out_slots); // m_rewards
                DecodeType(20, buf, out_slots); // m_toonHandle
                DecodeType(65, buf, out_slots); // m_licenses
                DecodeType(60, buf, out_slots); // m_tandemLeaderId
                DecodeType(46, buf, out_slots); // m_commander
                DecodeType(6, buf, out_slots); // m_commanderLevel
                DecodeType(13, buf, out_slots); // m_hasSilencePenalty
                DecodeType(60, buf, out_slots); // m_tandemId
                DecodeType(6, buf, out_slots); // m_commanderMasteryLevel
                DecodeType(66, buf, out_slots); // m_commanderMasteryTalents
                DecodeType(6, buf, out_slots); // m_trophyId
                DecodeType(68, buf, out_slots); // m_rewardOverrides
                DecodeType(6, buf, out_slots); // m_brutalPlusDifficulty
                DecodeType(66, buf, out_slots); // m_retryMutationIndexes
                DecodeType(6, buf, out_slots); // m_aCEnemyRace
                DecodeType(6, buf, out_slots); // m_aCEnemyWaveType
                DecodeType(6, buf, out_slots); // m_selectedCommanderPrestige
                break;
            }
            case 70: { // _array
                int64_t count = DecodeInt(0LL, 5, buf);
                for (int64_t i = 0; i < count; ++i) {
                    DecodedInitSlot slot;
                    DecodeSlot(buf, slot);
                    out_slots.push_back(slot);
                }
                break;
            }
            case 71: { // _struct
                DecodeType(12, buf, out_slots); // m_phase
                DecodeType(2, buf, out_slots); // m_maxUsers
                DecodeType(2, buf, out_slots); // m_maxObservers
                DecodeType(70, buf, out_slots); // m_slots
                DecodeType(6, buf, out_slots); // m_randomSeed
                DecodeType(60, buf, out_slots); // m_hostUserId
                DecodeType(13, buf, out_slots); // m_isSinglePlayer
                DecodeType(10, buf, out_slots); // m_pickedMapTag
                DecodeType(6, buf, out_slots); // m_gameDuration
                DecodeType(3, buf, out_slots); // m_defaultDifficulty
                DecodeType(10, buf, out_slots); // m_defaultAIBuild
                break;
            }
            case 72: { // _struct
                DecodeType(50, buf, out_slots); // m_userInitialData
                DecodeType(59, buf, out_slots); // m_gameDescription
                DecodeType(71, buf, out_slots); // m_lobbyState
                break;
            }
            case 73: { // _struct
                DecodeType(72, buf, out_slots); // m_syncLobbyState
                break;
            }
            default: break;
        }
    }

    static void DecodeSlot(S2BitPackedBuffer& buf, DecodedInitSlot& slot) {
        // Slot struct (type 69):
        // 0: m_control (type 10: int 8 bits)
        DecodeInt(0, 8, buf);
        // 1: m_userId (type 60: optional 1)
        if (buf.read_bits(1) != 0) DecodeInt(0, 4, buf);
        // 2: m_teamId (type 1: int 4 bits)
        DecodeInt(0, 4, buf);
        // 3: m_colorPref (type 62: struct m_color optional 2)
        if (buf.read_bits(1) != 0) DecodeInt(0, 5, buf);
        // 4: m_racePref (type 44: struct m_race optional 10)
        if (buf.read_bits(1) != 0) DecodeInt(0, 8, buf);
        // 5: m_difficulty (type 3: int 6 bits)
        slot.difficulty = static_cast<int>(DecodeInt(0, 6, buf));
        // 6: m_aiBuild (type 10: int 8 bits)
        DecodeInt(0, 8, buf);
        // 7: m_handicap (type 6: int 32 bits)
        DecodeInt(0, 32, buf);
        // 8: m_observe (type 24: int 2 bits)
        DecodeInt(0, 2, buf);
        // 9: m_logoIndex (type 6: int 32 bits)
        DecodeInt(0, 32, buf);
        // 10: m_hero (type 46: blob 9 bits)
        size_t hero_len = static_cast<size_t>(DecodeInt(0, 9, buf));
        buf.read_aligned_bytes(hero_len);
        // 11: m_skin (type 46: blob 9 bits)
        size_t skin_len = static_cast<size_t>(DecodeInt(0, 9, buf));
        buf.read_aligned_bytes(skin_len);
        // 12: m_mount (type 46: blob 9 bits)
        size_t mount_len = static_cast<size_t>(DecodeInt(0, 9, buf));
        buf.read_aligned_bytes(mount_len);
        // 13: m_artifacts (type 63: array (0,4) of 46)
        int64_t art_count = DecodeInt(0, 4, buf);
        for (int64_t a = 0; a < art_count; ++a) {
            size_t l = static_cast<size_t>(DecodeInt(0, 9, buf));
            buf.read_aligned_bytes(l);
        }
        // 14: m_workingSetSlotId (type 25: optional 10)
        if (buf.read_bits(1) != 0) DecodeInt(0, 8, buf);
        // 15: m_rewards (type 64: array (0,17) of 6)
        int64_t rew_count = DecodeInt(0, 17, buf);
        for (int64_t r = 0; r < rew_count; ++r) DecodeInt(0, 32, buf);
        // 16: m_toonHandle (type 20: blob 7 bits)
        size_t toon_len = static_cast<size_t>(DecodeInt(0, 7, buf));
        slot.toon_handle = buf.read_aligned_bytes(toon_len);
        // 17: m_licenses (type 65: array (0,16) of 6)
        int64_t lic_count = DecodeInt(0, 16, buf);
        for (int64_t l = 0; l < lic_count; ++l) DecodeInt(0, 32, buf);
        // 18: m_tandemLeaderId (type 60: optional 1)
        if (buf.read_bits(1) != 0) DecodeInt(0, 4, buf);
        // 19: m_commander (type 46: blob 9 bits)
        size_t cmdr_len = static_cast<size_t>(DecodeInt(0, 9, buf));
        slot.commander = buf.read_aligned_bytes(cmdr_len);
        // 20: m_commanderLevel (type 6: int 32 bits)
        slot.commander_level = static_cast<int>(DecodeInt(0, 32, buf));
        // 21: m_hasSilencePenalty (type 13: bool)
        buf.read_bits(1);
        // 22: m_tandemId (type 60: optional 1)
        if (buf.read_bits(1) != 0) DecodeInt(0, 4, buf);
        // 23: m_commanderMasteryLevel (type 6: int 32 bits)
        slot.commander_mastery_level = static_cast<int>(DecodeInt(0, 32, buf));
        // 24: m_commanderMasteryTalents (type 66: array (0,3) of 6)
        int64_t mast_count = DecodeInt(0, 3, buf);
        for (int64_t m = 0; m < mast_count; ++m) {
            slot.masteries.push_back(static_cast<int>(DecodeInt(0, 32, buf)));
        }
        // 25: m_trophyId (type 6: int 32 bits)
        DecodeInt(0, 32, buf);
        // 26: m_rewardOverrides (type 68: array (0,17) of 67 [m_key (type 6), m_rewards (type 64: array (0,17) of 6)])
        int64_t ro_count = DecodeInt(0, 17, buf);
        for (int64_t ro = 0; ro < ro_count; ++ro) {
            DecodeInt(0, 32, buf); // m_key
            int64_t rw_count = DecodeInt(0, 17, buf); // m_rewards length
            for (int64_t rw = 0; rw < rw_count; ++rw) {
                DecodeInt(0, 32, buf); // reward id
            }
        }
        // 27: m_brutalPlusDifficulty (type 6: int 32 bits, valid values 0..6)
        slot.brutal_plus = static_cast<int>(DecodeInt(0, 32, buf));
        if (slot.brutal_plus < 0 || slot.brutal_plus > 6) {
            slot.brutal_plus = 0;
        }
        // 28: m_retryMutationIndexes (type 66: array (0,3) of 6)
        int64_t ret_count = DecodeInt(0, 3, buf);
        for (int64_t rt = 0; rt < ret_count; ++rt) DecodeInt(0, 32, buf);
        // 29: m_aCEnemyRace (type 6: int 32 bits)
        DecodeInt(0, 32, buf);
        // 30: m_aCEnemyWaveType (type 6: int 32 bits)
        DecodeInt(0, 32, buf);
        // 31: m_selectedCommanderPrestige (type 6: int 32 bits)
        slot.prestige = static_cast<int>(DecodeInt(0, 32, buf));
    }
};

} // namespace sc2
