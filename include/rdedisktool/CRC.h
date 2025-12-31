#ifndef RDEDISKTOOL_CRC_H
#define RDEDISKTOOL_CRC_H

#include <cstdint>
#include <cstddef>
#include <vector>

namespace rde {

/**
 * CRC calculation utilities for disk image operations
 */
class CRC {
public:
    /**
     * Calculate CRC-16 CCITT (used by MSX, IBM PC floppy)
     * Polynomial: 0x1021, Init: 0xFFFF
     */
    static uint16_t crc16_ccitt(const uint8_t* data, size_t length);
    static uint16_t crc16_ccitt(const std::vector<uint8_t>& data);

    /**
     * Calculate CRC-16 CCITT with custom initial value
     */
    static uint16_t crc16_ccitt(const uint8_t* data, size_t length, uint16_t init);

    /**
     * Calculate CRC-32 (used by WOZ format)
     * Polynomial: 0xEDB88320 (reflected), Init: 0xFFFFFFFF
     */
    static uint32_t crc32(const uint8_t* data, size_t length);
    static uint32_t crc32(const std::vector<uint8_t>& data);

    /**
     * Update CRC-32 incrementally
     */
    static uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t length);

    /**
     * Finalize CRC-32 (XOR with 0xFFFFFFFF)
     */
    static uint32_t crc32_finalize(uint32_t crc);

    /**
     * Verify CRC-16 CCITT
     */
    static bool verify_crc16(const uint8_t* data, size_t length, uint16_t expected);

    /**
     * Verify CRC-32
     */
    static bool verify_crc32(const uint8_t* data, size_t length, uint32_t expected);

private:
    // Lookup tables (dynamically initialized)
    static uint16_t crc16_table[256];
    static uint32_t crc32_table[256];

    // Table initialization
    static void initCRC16Table();
    static void initCRC32Table();
    static bool tablesInitialized;
    static void ensureTablesInitialized();
};

} // namespace rde

#endif // RDEDISKTOOL_CRC_H
