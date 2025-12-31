#include "rdedisktool/CRC.h"

namespace rde {

// Static member initialization
bool CRC::tablesInitialized = false;
uint16_t CRC::crc16_table[256] = {0};
uint32_t CRC::crc32_table[256] = {0};

void CRC::initCRC16Table() {
    // CRC-16 CCITT polynomial: 0x1021
    for (int i = 0; i < 256; ++i) {
        uint16_t crc = static_cast<uint16_t>(i << 8);
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
        crc16_table[i] = crc;
    }
}

void CRC::initCRC32Table() {
    // CRC-32 polynomial: 0xEDB88320 (reflected)
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
        crc32_table[i] = crc;
    }
}

void CRC::ensureTablesInitialized() {
    if (!tablesInitialized) {
        initCRC16Table();
        initCRC32Table();
        tablesInitialized = true;
    }
}

uint16_t CRC::crc16_ccitt(const uint8_t* data, size_t length) {
    return crc16_ccitt(data, length, 0xFFFF);
}

uint16_t CRC::crc16_ccitt(const uint8_t* data, size_t length, uint16_t init) {
    ensureTablesInitialized();

    uint16_t crc = init;
    for (size_t i = 0; i < length; ++i) {
        uint8_t index = static_cast<uint8_t>((crc >> 8) ^ data[i]);
        crc = (crc << 8) ^ crc16_table[index];
    }
    return crc;
}

uint16_t CRC::crc16_ccitt(const std::vector<uint8_t>& data) {
    return crc16_ccitt(data.data(), data.size());
}

uint32_t CRC::crc32(const uint8_t* data, size_t length) {
    return crc32_finalize(crc32_update(0xFFFFFFFF, data, length));
}

uint32_t CRC::crc32(const std::vector<uint8_t>& data) {
    return crc32(data.data(), data.size());
}

uint32_t CRC::crc32_update(uint32_t crc, const uint8_t* data, size_t length) {
    ensureTablesInitialized();

    for (size_t i = 0; i < length; ++i) {
        uint8_t index = static_cast<uint8_t>(crc ^ data[i]);
        crc = (crc >> 8) ^ crc32_table[index];
    }
    return crc;
}

uint32_t CRC::crc32_finalize(uint32_t crc) {
    return crc ^ 0xFFFFFFFF;
}

bool CRC::verify_crc16(const uint8_t* data, size_t length, uint16_t expected) {
    return crc16_ccitt(data, length) == expected;
}

bool CRC::verify_crc32(const uint8_t* data, size_t length, uint32_t expected) {
    return crc32(data, length) == expected;
}

} // namespace rde
