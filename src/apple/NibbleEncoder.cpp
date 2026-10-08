#include "rdedisktool/apple/NibbleEncoder.h"
#include "rdedisktool/apple/AppleDiskImage.h"
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

    // 6-and-2 as written by DOS 3.3 RWTS (PRENIB16 + WRITE16).
    // The 342 six-bit values go to disk in this order: 86 auxiliary values,
    // then the high 6 bits of data[0..255]. Auxiliary value k holds the low
    // two bits (swapped) of data[k] in bits 1-0, data[k+86] in bits 3-2 and
    // data[k+172] in bits 5-4. For k = 84, 85 there is no data[k+172];
    // RWTS puts data[0] / data[1] there (ignored on read).
    auto swap2 = [](uint8_t b) -> uint8_t {
        return static_cast<uint8_t>(((b & 0x01) << 1) | ((b & 0x02) >> 1));
    };

    std::array<uint8_t, 342> buffer;
    for (int k = 0; k < 86; ++k) {
        const int hiIndex = (k + 172 < 256) ? k + 172 : k - 84;
        buffer[k] = static_cast<uint8_t>(swap2(data[k]) |
                                         (swap2(data[k + 86]) << 2) |
                                         (swap2(data[hiIndex]) << 4));
    }
    for (int i = 0; i < 256; ++i) {
        buffer[86 + i] = data[i] >> 2;
    }

    // Each value is written XORed with the previous one; the 343rd nibble
    // is the last value itself (the checksum).
    std::vector<uint8_t> result(NIBBLIZED_SIZE);
    uint8_t prev = 0;
    for (int i = 0; i < 342; ++i) {
        result[i] = ENCODE_TABLE[(buffer[i] ^ prev) & 0x3F];
        prev = buffer[i];
    }
    result[342] = ENCODE_TABLE[prev & 0x3F];

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
    // Decode 4-and-4 encoded values. The fixed 1 bits are not checked on
    // their own: DOS 3.3 RWTS reads a sector whose address bytes miss a fixed
    // bit as long as the decoded values and checksum are right (sa2 test).
    volume = decode44(data[0], data[1]);
    track = decode44(data[2], data[3]);
    sector = decode44(data[4], data[5]);
    uint8_t checksum = decode44(data[6], data[7]);

    // Verify checksum
    return (volume ^ track ^ sector) == checksum;
}

bool NibbleEncoder::ParsedTrack::allFound() const {
    for (size_t s = 0; s < sectorCount && s < found.size(); ++s) {
        if (!found[s]) return false;
    }
    return true;
}

NibbleEncoder::TrackNibbles NibbleEncoder::buildTrackNibbles(
    const std::array<std::vector<uint8_t>, 16>& sectors,
    uint8_t volume, uint8_t track, size_t gap1Syncs) {

    TrackNibbles result;
    result.nibbles.reserve(gap1Syncs + 16 * SECTOR_NIBBLES);
    result.isSync.reserve(gap1Syncs + 16 * SECTOR_NIBBLES);

    auto put = [&result](uint8_t nibble, bool sync) {
        result.nibbles.push_back(nibble);
        result.isSync.push_back(sync ? 1 : 0);
    };
    auto putSyncs = [&put](size_t count) {
        for (size_t i = 0; i < count; ++i) put(SYNC_BYTE, true);
    };

    // DOS logical sector stored at each physical position
    std::array<uint8_t, 16> logicalAt{};
    for (uint8_t logical = 0; logical < 16; ++logical) {
        logicalAt[AppleInterleave::DOS33_INTERLEAVE[logical]] = logical;
    }

    putSyncs(gap1Syncs);
    for (uint8_t phys = 0; phys < 16; ++phys) {
        for (uint8_t b : encodeAddressField(volume, track, phys)) put(b, false);
        putSyncs(GAP2_SYNCS);

        put(DATA_PROLOGUE_1, false);
        put(DATA_PROLOGUE_2, false);
        put(DATA_PROLOGUE_3, false);
        for (uint8_t b : encodeSector(sectors[logicalAt[phys]])) put(b, false);
        put(EPILOGUE_1, false);
        put(EPILOGUE_2, false);
        put(EPILOGUE_3, false);
        putSyncs(GAP3_SYNCS);
    }

    return result;
}

std::vector<uint8_t> NibbleEncoder::buildNibTrack(
    const std::array<std::vector<uint8_t>, 16>& sectors,
    uint8_t volume, uint8_t track, size_t trackSize) {

    if (trackSize < 16 * SECTOR_NIBBLES) {
        throw std::invalid_argument("NIB track size too small for 16 sectors");
    }
    return buildTrackNibbles(sectors, volume, track,
                             trackSize - 16 * SECTOR_NIBBLES).nibbles;
}

std::vector<uint8_t> NibbleEncoder::nibblesToWozBits(const TrackNibbles& track,
                                                     uint32_t& bitCount, int syncBits) {
    std::vector<uint8_t> bits;
    bits.reserve(track.nibbles.size() * 10 / 8 + 1);

    uint32_t count = 0;
    auto pushBit = [&bits, &count](int bit) {
        if ((count & 7) == 0) bits.push_back(0);
        if (bit) bits.back() |= static_cast<uint8_t>(0x80 >> (count & 7));
        ++count;
    };

    for (size_t i = 0; i < track.nibbles.size(); ++i) {
        for (int b = 7; b >= 0; --b) pushBit((track.nibbles[i] >> b) & 1);
        if (track.isSync[i]) {
            for (int z = 8; z < syncBits; ++z) pushBit(0);
        }
    }

    bitCount = count;
    return bits;
}

std::vector<uint8_t> NibbleEncoder::wozBitsToNibbles(const std::vector<uint8_t>& bits,
                                                     uint32_t bitCount,
                                                     int revolutions,
                                                     std::vector<uint64_t>* firstBit) {
    std::vector<uint8_t> result;
    if (firstBit) {
        firstBit->clear();
    }
    if (bitCount == 0 || bits.size() * 8 < bitCount || revolutions <= 0) {
        return result;
    }
    result.reserve(static_cast<size_t>(bitCount) / 8 * revolutions);

    uint8_t latch = 0;
    uint64_t start = 0;
    for (uint64_t k = 0; k < static_cast<uint64_t>(bitCount) * revolutions; ++k) {
        const uint32_t pos = static_cast<uint32_t>(k % bitCount);
        const int bit = (bits[pos >> 3] >> (7 - (pos & 7))) & 1;
        if (latch == 0 && bit) {
            start = k;   // zero bits before the first 1 are skipped
        }
        latch = static_cast<uint8_t>((latch << 1) | bit);
        if (latch & 0x80) {
            result.push_back(latch);
            if (firstBit) {
                firstBit->push_back(start);
            }
            latch = 0;
        }
    }
    return result;
}

std::vector<uint8_t> NibbleEncoder::dataFieldNibbles(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> out{DATA_PROLOGUE_1, DATA_PROLOGUE_2, DATA_PROLOGUE_3};
    const auto encoded = encodeSector(data);
    out.insert(out.end(), encoded.begin(), encoded.end());
    out.push_back(EPILOGUE_1);
    out.push_back(EPILOGUE_2);
    return out;
}

NibbleEncoder::ParsedTrack NibbleEncoder::parseNibbleStream(
    const std::vector<uint8_t>& nibbles, uint8_t expectedTrack) {

    ParsedTrack result;
    const size_t n = nibbles.size();
    constexpr size_t DATA_SEARCH_WINDOW = 32;  // RWTS READ16 gives up after $20 nibbles

    // DOS logical sector stored at each physical position
    std::array<uint8_t, 16> logicalAt{};
    for (uint8_t logical = 0; logical < 16; ++logical) {
        logicalAt[AppleInterleave::DOS33_INTERLEAVE[logical]] = logical;
    }

    size_t i = 0;
    while (i + 14 <= n) {
        if (!(nibbles[i] == ADDR_PROLOGUE_1 && nibbles[i + 1] == ADDR_PROLOGUE_2 &&
              nibbles[i + 2] == ADDR_PROLOGUE_3)) {
            ++i;
            continue;
        }

        uint8_t volume, track, sector;
        if (!decodeAddressField(&nibbles[i + 3], volume, track, sector) ||
            track != expectedTrack || sector >= 16 ||
            nibbles[i + 11] != EPILOGUE_1 || nibbles[i + 12] != EPILOGUE_2) {
            ++i;
            continue;
        }

        // Data prologue must follow before the next address field
        size_t dataPos = 0;
        bool haveData = false;
        for (size_t j = i + 13; j + 3 <= n && j < i + 13 + DATA_SEARCH_WINDOW; ++j) {
            if (nibbles[j] == DATA_PROLOGUE_1 && nibbles[j + 1] == DATA_PROLOGUE_2) {
                if (nibbles[j + 2] == DATA_PROLOGUE_3) {
                    dataPos = j;
                    haveData = true;
                }
                break;  // D5 AA 96 (next address) or D5 AA AD: stop either way
            }
        }
        if (!haveData || dataPos + 3 + NIBBLIZED_SIZE + 2 > n) {
            ++i;
            continue;
        }

        const size_t nibStart = dataPos + 3;
        if (nibbles[nibStart + NIBBLIZED_SIZE] != EPILOGUE_1 ||
            nibbles[nibStart + NIBBLIZED_SIZE + 1] != EPILOGUE_2) {
            ++i;
            continue;
        }

        std::vector<uint8_t> data;
        try {
            data = decodeSector(std::vector<uint8_t>(
                nibbles.begin() + nibStart, nibbles.begin() + nibStart + NIBBLIZED_SIZE));
        } catch (const std::exception&) {
            ++i;
            continue;
        }

        const uint8_t logical = logicalAt[sector];
        if (!result.found[logical]) {
            result.sectors[logical] = std::move(data);
            result.found[logical] = true;
            result.dataAt[logical] = static_cast<uint32_t>(dataPos);
            for (size_t k = 0; k < 8; ++k) {
                if ((nibbles[i + 3 + k] & 0xAA) != 0xAA) {
                    result.fixedBitsMissing[logical] = true;
                }
            }
            if (!result.volumeKnown) {
                result.volume = volume;
                result.volumeKnown = true;
            }
        }
        i = nibStart + NIBBLIZED_SIZE + 2;
    }

    return result;
}

namespace {
    // 5-and-3 translate table: the 32 nibbles DOS 3.2 writes, in value order
    constexpr std::array<uint8_t, 32> ENCODE53_TABLE = {
        0xAB, 0xAD, 0xAE, 0xAF, 0xB5, 0xB6, 0xB7, 0xBA, 0xBB, 0xBD, 0xBE, 0xBF, 0xD6, 0xD7, 0xDA, 0xDB,
        0xDD, 0xDE, 0xDF, 0xEA, 0xEB, 0xED, 0xEE, 0xEF, 0xF5, 0xF6, 0xF7, 0xFA, 0xFB, 0xFD, 0xFE, 0xFF
    };

    std::array<uint8_t, 256> makeDecode53() {
        std::array<uint8_t, 256> t{};
        t.fill(0xFF);
        for (size_t i = 0; i < ENCODE53_TABLE.size(); ++i) {
            t[ENCODE53_TABLE[i]] = static_cast<uint8_t>(i);
        }
        return t;
    }
}

std::vector<uint8_t> NibbleEncoder::decodeSector53(const std::vector<uint8_t>& nibbles) {
    static const std::array<uint8_t, 256> decode53 = makeDecode53();
    if (nibbles.size() < NIBBLIZED53_SIZE) {
        throw std::invalid_argument("Nibble data too short");
    }

    // 410 five-bit values, each written XORed with the previous one; the
    // 411th nibble is the last value (checksum)
    std::array<uint8_t, 410> vals{};
    uint8_t prev = 0;
    for (size_t i = 0; i < 410; ++i) {
        const uint8_t v = decode53[nibbles[i]];
        if (v == 0xFF) {
            throw std::runtime_error("Invalid nibble byte in sector data");
        }
        prev ^= v;
        vals[i] = prev;
    }
    if (decode53[nibbles[410]] != prev) {
        throw std::runtime_error("Sector checksum mismatch");
    }

    // Bytes in groups of five (0..254) plus byte 255. Values 154..408 hold
    // the high five bits of bytes 250+r-5i; values 1..153 the low three
    // bits of each group; values 0 and 409 belong to byte 255.
    std::vector<uint8_t> out(SECTOR_DATA_SIZE, 0);
    out[255] = static_cast<uint8_t>((vals[0] & 0x07) | ((vals[409] & 0x1F) << 3));
    for (int third = 0; third < 3; ++third) {
        for (int g = 1; g <= 51; ++g) {
            const uint8_t v = vals[third * 51 + g];
            out[5 * g - 1] |= static_cast<uint8_t>((v & 0x01) << third);
            out[5 * g - 2] |= static_cast<uint8_t>(((v >> 1) & 0x01) << third);
            out[5 * g - 3 - third] |= static_cast<uint8_t>((v >> 2) & 0x07);
        }
    }
    for (int r = 0; r < 5; ++r) {
        for (int i = 0; i < 51; ++i) {
            out[250 + r - 5 * i] |= static_cast<uint8_t>((vals[154 + 51 * r + i] & 0x1F) << 3);
        }
    }
    return out;
}

std::vector<uint8_t> NibbleEncoder::encodeSector53(const std::vector<uint8_t>& data) {
    if (data.size() < SECTOR_DATA_SIZE) {
        throw std::invalid_argument("Sector data too short");
    }

    // Same layout as decodeSector53, read the other way
    std::array<uint8_t, 410> vals{};
    vals[0] = static_cast<uint8_t>(data[255] & 0x07);
    vals[409] = static_cast<uint8_t>(data[255] >> 3);
    for (int third = 0; third < 3; ++third) {
        for (int g = 1; g <= 51; ++g) {
            vals[third * 51 + g] = static_cast<uint8_t>(
                ((data[5 * g - 1] >> third) & 0x01) |
                (((data[5 * g - 2] >> third) & 0x01) << 1) |
                ((data[5 * g - 3 - third] & 0x07) << 2));
        }
    }
    for (int r = 0; r < 5; ++r) {
        for (int i = 0; i < 51; ++i) {
            vals[154 + 51 * r + i] = static_cast<uint8_t>(data[250 + r - 5 * i] >> 3);
        }
    }

    std::vector<uint8_t> result(NIBBLIZED53_SIZE);
    uint8_t prev = 0;
    for (size_t i = 0; i < vals.size(); ++i) {
        result[i] = ENCODE53_TABLE[vals[i] ^ prev];
        prev = vals[i];
    }
    result[410] = ENCODE53_TABLE[prev];
    return result;
}

std::vector<uint8_t> NibbleEncoder::dataFieldNibbles53(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> out{DATA_PROLOGUE_1, DATA_PROLOGUE_2, DATA_PROLOGUE_3};
    const auto encoded = encodeSector53(data);
    out.insert(out.end(), encoded.begin(), encoded.end());
    out.push_back(EPILOGUE_1);
    out.push_back(EPILOGUE_2);
    return out;
}

NibbleEncoder::TrackNibbles NibbleEncoder::buildTrackNibbles13(
    const std::array<std::vector<uint8_t>, 16>& sectors,
    uint8_t volume, uint8_t track, size_t gap1Syncs) {

    static constexpr std::array<uint8_t, 13> ORDER = {0, 10, 7, 4, 1, 11, 8, 5, 2, 12, 9, 6, 3};
    TrackNibbles result;
    auto put = [&result](uint8_t nibble, bool sync) {
        result.nibbles.push_back(nibble);
        result.isSync.push_back(sync ? 1 : 0);
    };
    auto putSyncs = [&put](size_t count) {
        for (size_t i = 0; i < count; ++i) put(SYNC_BYTE, true);
    };

    putSyncs(gap1Syncs);
    for (uint8_t phys : ORDER) {
        auto addr = encodeAddressField(volume, track, phys);
        addr[2] = ADDR_PROLOGUE_3_13;
        for (uint8_t b : addr) put(b, false);
        putSyncs(14);
        for (uint8_t b : dataFieldNibbles53(sectors[phys])) put(b, false);
        put(EPILOGUE_3, false);
        putSyncs(28);
    }
    return result;
}

NibbleEncoder::ParsedTrack NibbleEncoder::parseNibbleStream13(
    const std::vector<uint8_t>& nibbles, uint8_t expectedTrack) {

    ParsedTrack result;
    result.sectorCount = 13;
    const size_t n = nibbles.size();
    constexpr size_t DATA_SEARCH_WINDOW = 32;

    size_t i = 0;
    while (i + 14 <= n) {
        if (!(nibbles[i] == ADDR_PROLOGUE_1 && nibbles[i + 1] == ADDR_PROLOGUE_2 &&
              nibbles[i + 2] == ADDR_PROLOGUE_3_13)) {
            ++i;
            continue;
        }

        uint8_t volume, track, sector;
        if (!decodeAddressField(&nibbles[i + 3], volume, track, sector) ||
            track != expectedTrack || sector >= 13 ||
            nibbles[i + 11] != EPILOGUE_1 || nibbles[i + 12] != EPILOGUE_2) {
            ++i;
            continue;
        }

        result.addrSeen[sector] = true;
        size_t dataPos = 0;
        bool haveData = false;
        bool anyPrologue = false;
        for (size_t j = i + 13; j + 3 <= n && j < i + 13 + DATA_SEARCH_WINDOW; ++j) {
            if (nibbles[j] == DATA_PROLOGUE_1 && nibbles[j + 1] == DATA_PROLOGUE_2) {
                anyPrologue = true;
                if (nibbles[j + 2] == DATA_PROLOGUE_3) {
                    dataPos = j;
                    haveData = true;
                }
                break;
            }
        }
        if (!anyPrologue && i + 13 + DATA_SEARCH_WINDOW + 2 <= n &&
            !result.found[sector] && !result.addrOnly[sector]) {
            // the whole search window was looked at and holds no D5 AA
            result.addrOnly[sector] = true;
            result.addrAt[sector] = static_cast<uint32_t>(i);
        }
        if (!haveData || dataPos + 3 + NIBBLIZED53_SIZE + 2 > n) {
            ++i;
            continue;
        }

        const size_t nibStart = dataPos + 3;
        if (nibbles[nibStart + NIBBLIZED53_SIZE] != EPILOGUE_1 ||
            nibbles[nibStart + NIBBLIZED53_SIZE + 1] != EPILOGUE_2) {
            ++i;
            continue;
        }

        std::vector<uint8_t> data;
        try {
            data = decodeSector53(std::vector<uint8_t>(
                nibbles.begin() + nibStart, nibbles.begin() + nibStart + NIBBLIZED53_SIZE));
        } catch (const std::exception&) {
            ++i;
            continue;
        }

        if (!result.found[sector]) {
            result.sectors[sector] = std::move(data);
            result.found[sector] = true;
            result.dataAt[sector] = static_cast<uint32_t>(dataPos);
            result.addrOnly[sector] = false;   // a readable copy wins
            for (size_t k = 0; k < 8; ++k) {
                if ((nibbles[i + 3 + k] & 0xAA) != 0xAA) {
                    result.fixedBitsMissing[sector] = true;
                }
            }
            if (!result.volumeKnown) {
                result.volume = volume;
                result.volumeKnown = true;
            }
        }
        i = nibStart + NIBBLIZED53_SIZE + 2;
    }

    return result;
}

bool NibbleEncoder::looksLike13Sector(const std::vector<uint8_t>& nibbles, uint8_t track) {
    size_t count13 = 0, count16 = 0;
    const ParsedTrack p13 = parseNibbleStream13(nibbles, track);
    const ParsedTrack p16 = parseNibbleStream(nibbles, track);
    for (size_t s = 0; s < 16; ++s) {
        count13 += p13.found[s] ? 1 : 0;
        count16 += p16.found[s] ? 1 : 0;
    }
    return count13 > count16;
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

} // namespace rde
