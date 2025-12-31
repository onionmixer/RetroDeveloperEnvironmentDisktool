#include "rdedisktool/apple/NibbleEncoder.h"
#include <algorithm>
#include <stdexcept>

namespace rde {

// 6-and-2 GCR encoding table
// Maps 6-bit values (0x00-0x3F) to valid disk bytes
const std::array<uint8_t, 64> NibbleEncoder::ENCODE_TABLE = {
    0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6,
    0xA7, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2, 0xB3,
    0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC,
    0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3,
    0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE,
    0xDF, 0xE5, 0xE6, 0xE7, 0xE9, 0xEA, 0xEB, 0xEC,
    0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6,
    0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF
};

// 6-and-2 GCR decoding table (inverse of encode table)
// Maps disk bytes to 6-bit values, 0xFF = invalid
const std::array<uint8_t, 256> NibbleEncoder::DECODE_TABLE = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // 00-07
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // 08-0F
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // 10-17
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // 18-1F
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // 20-27
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // 28-2F
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // 30-37
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // 38-3F
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // 40-47
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // 48-4F
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // 50-57
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // 58-5F
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // 60-67
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // 68-6F
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // 70-77
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // 78-7F
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // 80-87
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // 88-8F
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x01, // 90-97
    0xFF, 0xFF, 0x02, 0x03, 0xFF, 0x04, 0x05, 0x06, // 98-9F
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0x08, // A0-A7
    0xFF, 0xFF, 0xFF, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, // A8-AF
    0xFF, 0xFF, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, // B0-B7
    0xFF, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, // B8-BF
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // C0-C7
    0xFF, 0xFF, 0xFF, 0x1B, 0xFF, 0x1C, 0x1D, 0x1E, // C8-CF
    0xFF, 0xFF, 0xFF, 0x1F, 0xFF, 0xFF, 0x20, 0x21, // D0-D7
    0xFF, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, // D8-DF
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x29, 0x2A, 0x2B, // E0-E7
    0xFF, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, // E8-EF
    0xFF, 0xFF, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, // F0-F7
    0xFF, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F  // F8-FF
};

// Physical sector order on disk (DOS 3.3 interleave)
const std::array<uint8_t, 16> NibbleEncoder::PHYSICAL_SECTOR_ORDER = {
    0, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 15
};

const std::array<uint8_t, 64>& NibbleEncoder::getEncodeTable() {
    return ENCODE_TABLE;
}

const std::array<uint8_t, 256>& NibbleEncoder::getDecodeTable() {
    return DECODE_TABLE;
}

void NibbleEncoder::encode44(uint8_t value, uint8_t& odd, uint8_t& even) {
    // 4-and-4 encoding: splits a byte into two disk bytes
    // Odd byte contains bits 7,5,3,1 (in positions 6,4,2,0)
    // Even byte contains bits 6,4,2,0 (in positions 6,4,2,0)
    odd = 0xAA | ((value >> 1) & 0x55);
    even = 0xAA | (value & 0x55);
}

uint8_t NibbleEncoder::decode44(uint8_t odd, uint8_t even) {
    // Reverse of encode44
    return ((odd & 0x55) << 1) | (even & 0x55);
}

std::vector<uint8_t> NibbleEncoder::encodeSector(const std::vector<uint8_t>& data) {
    if (data.size() != SECTOR_DATA_SIZE) {
        throw std::invalid_argument("Sector data must be 256 bytes");
    }

    std::vector<uint8_t> result(NIBBLIZED_SIZE);

    // Step 1: Pre-nibblize (6-and-2 encoding)
    // Convert 256 bytes to 342 6-bit values

    // Buffer for 6-bit values (before GCR encoding)
    std::array<uint8_t, 342> buffer;

    // First, handle the auxiliary buffer (86 bytes)
    // These hold the low 2 bits of each data byte
    for (int i = 0; i < 86; ++i) {
        uint8_t aux = 0;

        // For each group of 3 bytes in the auxiliary section
        // Take 2 bits from bytes at positions i, i+86, i+172
        if (i < 86) {
            aux |= ((data[i] & 0x01) << 1) | ((data[i] & 0x02) >> 1);
        }
        if (i + 86 < 256) {
            aux |= ((data[i + 86] & 0x01) << 3) | ((data[i + 86] & 0x02) << 1);
        }
        if (i + 172 < 256) {
            aux |= ((data[i + 172] & 0x01) << 5) | ((data[i + 172] & 0x02) << 3);
        }

        buffer[i] = aux;
    }

    // Then, handle the main data (256 bytes, high 6 bits)
    for (int i = 0; i < 256; ++i) {
        buffer[86 + i] = data[i] >> 2;
    }

    // Step 2: XOR checksumming
    uint8_t checksum = 0;
    for (int i = 341; i >= 0; --i) {
        uint8_t val = buffer[i];
        buffer[i] = val ^ checksum;
        checksum = val;
    }

    // Store checksum as the last byte
    // (before GCR encoding)

    // Step 3: GCR encoding
    for (int i = 0; i < 342; ++i) {
        result[i] = ENCODE_TABLE[buffer[i] & 0x3F];
    }
    result[342] = ENCODE_TABLE[checksum & 0x3F];

    return result;
}

std::vector<uint8_t> NibbleEncoder::decodeSector(const std::vector<uint8_t>& nibbles) {
    if (nibbles.size() < NIBBLIZED_SIZE) {
        throw std::invalid_argument("Nibble data too short");
    }

    std::vector<uint8_t> result(SECTOR_DATA_SIZE);

    // Step 1: GCR decoding
    std::array<uint8_t, 343> buffer;
    for (int i = 0; i < 343; ++i) {
        uint8_t decoded = DECODE_TABLE[nibbles[i]];
        if (decoded == 0xFF) {
            throw std::runtime_error("Invalid nibble byte in sector data");
        }
        buffer[i] = decoded;
    }

    // Step 2: XOR de-checksumming
    uint8_t checksum = 0;
    for (int i = 0; i < 342; ++i) {
        buffer[i] ^= checksum;
        checksum = buffer[i];
    }

    // Verify checksum
    if (checksum != buffer[342]) {
        throw std::runtime_error("Sector checksum mismatch");
    }

    // Step 3: De-nibblize (reverse 6-and-2)
    // Reconstruct 256 bytes from 342 6-bit values

    for (int i = 0; i < 256; ++i) {
        // High 6 bits from main data area
        uint8_t high = buffer[86 + i] << 2;

        // Low 2 bits from auxiliary area
        int auxIndex = i % 86;
        int auxShift = (i / 86) * 2;

        uint8_t low = (buffer[auxIndex] >> auxShift) & 0x03;

        // Swap bits 0 and 1 of the low 2 bits
        low = ((low & 0x01) << 1) | ((low & 0x02) >> 1);

        result[i] = high | low;
    }

    return result;
}

std::vector<uint8_t> NibbleEncoder::encodeAddressField(uint8_t volume, uint8_t track, uint8_t sector) {
    std::vector<uint8_t> result;
    result.reserve(14);

    // Prologue
    result.push_back(ADDR_PROLOGUE_1);  // D5
    result.push_back(ADDR_PROLOGUE_2);  // AA
    result.push_back(ADDR_PROLOGUE_3);  // 96

    // Volume (4-and-4 encoded)
    uint8_t odd, even;
    encode44(volume, odd, even);
    result.push_back(odd);
    result.push_back(even);

    // Track (4-and-4 encoded)
    encode44(track, odd, even);
    result.push_back(odd);
    result.push_back(even);

    // Sector (4-and-4 encoded)
    encode44(sector, odd, even);
    result.push_back(odd);
    result.push_back(even);

    // Checksum (4-and-4 encoded) = volume XOR track XOR sector
    uint8_t chksum = volume ^ track ^ sector;
    encode44(chksum, odd, even);
    result.push_back(odd);
    result.push_back(even);

    // Epilogue
    result.push_back(EPILOGUE_1);  // DE
    result.push_back(EPILOGUE_2);  // AA
    result.push_back(EPILOGUE_3);  // EB

    return result;
}

bool NibbleEncoder::decodeAddressField(const uint8_t* data, uint8_t& volume,
                                       uint8_t& track, uint8_t& sector) {
    // Decode 4-and-4 encoded values
    volume = decode44(data[0], data[1]);
    track = decode44(data[2], data[3]);
    sector = decode44(data[4], data[5]);
    uint8_t checksum = decode44(data[6], data[7]);

    // Verify checksum
    return (volume ^ track ^ sector) == checksum;
}

std::vector<uint8_t> NibbleEncoder::buildTrack(
    const std::array<std::vector<uint8_t>, 16>& sectorData,
    uint8_t volume, uint8_t track) {

    std::vector<uint8_t> result;
    result.reserve(TRACK_NIBBLE_SIZE);

    // Initial gap
    for (int i = 0; i < 48; ++i) {
        result.push_back(SYNC_BYTE);
    }

    // Write sectors in physical order
    for (int phys = 0; phys < 16; ++phys) {
        uint8_t sector = PHYSICAL_SECTOR_ORDER[phys];

        // Sync bytes before address field
        for (int i = 0; i < 5; ++i) {
            result.push_back(SYNC_BYTE);
        }

        // Address field
        auto addr = encodeAddressField(volume, track, sector);
        result.insert(result.end(), addr.begin(), addr.end());

        // Gap between address and data
        for (int i = 0; i < 5; ++i) {
            result.push_back(SYNC_BYTE);
        }

        // Data field prologue
        result.push_back(DATA_PROLOGUE_1);  // D5
        result.push_back(DATA_PROLOGUE_2);  // AA
        result.push_back(DATA_PROLOGUE_3);  // AD

        // Encoded sector data
        auto encoded = encodeSector(sectorData[sector]);
        result.insert(result.end(), encoded.begin(), encoded.end());

        // Data field epilogue
        result.push_back(EPILOGUE_1);  // DE
        result.push_back(EPILOGUE_2);  // AA
        result.push_back(EPILOGUE_3);  // EB

        // Gap after sector (variable to fill track)
    }

    // Pad to standard track size
    while (result.size() < TRACK_NIBBLE_SIZE) {
        result.push_back(SYNC_BYTE);
    }

    // Truncate if too long
    if (result.size() > TRACK_NIBBLE_SIZE) {
        result.resize(TRACK_NIBBLE_SIZE);
    }

    return result;
}

int NibbleEncoder::findAddressField(const std::vector<uint8_t>& data, size_t startPos) {
    for (size_t i = startPos; i + 2 < data.size(); ++i) {
        if (data[i] == ADDR_PROLOGUE_1 &&
            data[i + 1] == ADDR_PROLOGUE_2 &&
            data[i + 2] == ADDR_PROLOGUE_3) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int NibbleEncoder::findDataField(const std::vector<uint8_t>& data, size_t startPos) {
    for (size_t i = startPos; i + 2 < data.size(); ++i) {
        if (data[i] == DATA_PROLOGUE_1 &&
            data[i + 1] == DATA_PROLOGUE_2 &&
            data[i + 2] == DATA_PROLOGUE_3) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::array<std::vector<uint8_t>, 16> NibbleEncoder::parseTrack(
    const std::vector<uint8_t>& trackData, uint8_t expectedTrack) {

    std::array<std::vector<uint8_t>, 16> result;
    std::array<bool, 16> found = {};

    size_t pos = 0;
    int sectorsFound = 0;

    while (pos < trackData.size() && sectorsFound < 16) {
        // Find address field
        int addrPos = findAddressField(trackData, pos);
        if (addrPos < 0) break;

        // Skip prologue
        size_t dataStart = addrPos + 3;
        if (dataStart + 8 > trackData.size()) break;

        // Decode address
        uint8_t volume, track, sector;
        if (!decodeAddressField(&trackData[dataStart], volume, track, sector)) {
            pos = addrPos + 1;
            continue;
        }

        // Verify track number
        if (track != expectedTrack || sector >= 16) {
            pos = addrPos + 1;
            continue;
        }

        // Find data field
        int dataFieldPos = findDataField(trackData, dataStart + 8);
        if (dataFieldPos < 0 || dataFieldPos > addrPos + 100) {
            pos = addrPos + 1;
            continue;
        }

        // Extract nibblized data
        size_t nibbleStart = dataFieldPos + 3;
        if (nibbleStart + NIBBLIZED_SIZE > trackData.size()) break;

        try {
            std::vector<uint8_t> nibbles(
                trackData.begin() + nibbleStart,
                trackData.begin() + nibbleStart + NIBBLIZED_SIZE);

            result[sector] = decodeSector(nibbles);
            found[sector] = true;
            ++sectorsFound;
        } catch (...) {
            // Decode error, continue searching
        }

        pos = nibbleStart + NIBBLIZED_SIZE;
    }

    // Fill any missing sectors with zeros
    for (int i = 0; i < 16; ++i) {
        if (!found[i]) {
            result[i].resize(SECTOR_DATA_SIZE, 0);
        }
    }

    return result;
}

} // namespace rde
