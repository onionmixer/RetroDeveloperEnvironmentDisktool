#include "rdedisktool/macintosh/MacMfmDecoder.h"

#include <cstring>

namespace rde {

namespace {

// CRC16-CCITT (polynomial 0x1021, init 0xFFFF, no reflection, no XOR).
// Used on IBM PC / Mac 1.44M MFM headers and data fields, computed over
// the 3 sync bytes (0xA1) + marker + payload.
uint16_t crc16Ccitt(const uint8_t* data, size_t len, uint16_t seed = 0xFFFF) {
    uint16_t crc = seed;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                  : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

// Read one bit from a MOOF-style bit-packed stream (MSB-first per byte).
inline uint8_t readBit(const uint8_t* bits, size_t i) {
    return (bits[i >> 3] >> (7 - (i & 7))) & 1u;
}

// Read 16 raw MFM bits starting at bitPos and return the data byte (taking
// the odd-indexed bits of each clock+data pair).
inline uint8_t readMfmByte(const uint8_t* bits, size_t bitPos) {
    uint8_t v = 0;
    for (int i = 0; i < 8; ++i) {
        const uint8_t dataBit = readBit(bits, bitPos + 1 + i * 2);
        v = static_cast<uint8_t>((v << 1) | dataBit);
    }
    return v;
}

// Locate the next 0x4489 sync mark starting at bitPos. Returns the bit
// offset of the START of the 16-bit sync pattern, or std::string::npos
// (cast to size_t) if not found.
constexpr size_t kNotFound = static_cast<size_t>(-1);
size_t findSync4489(const uint8_t* bits, size_t bitCount, size_t startBit) {
    if (bitCount < 16) return kNotFound;
    uint16_t reg = 0;
    // Pre-fill the register with the first 15 bits before startBit (or
    // pad with zeros if startBit < 15).
    size_t i = startBit;
    if (startBit < 15) i = 0;
    else i = startBit - 15;
    for (size_t k = i; k < i + 15 && k < bitCount; ++k) {
        reg = static_cast<uint16_t>((reg << 1) | readBit(bits, k));
    }
    size_t pos = i + 15;
    while (pos < bitCount) {
        reg = static_cast<uint16_t>((reg << 1) | readBit(bits, pos));
        if (reg == 0x4489) {
            return pos - 15;  // start of the 16-bit sync window
        }
        ++pos;
    }
    return kNotFound;
}

// Find the IDAM (3× sync 0x4489 + 0xFE) or DAM (3× sync + 0xFB) marker
// starting from startBit. Returns the bit position immediately AFTER the
// marker byte (= start of payload), and writes the marker byte to *marker.
// Returns kNotFound if not found.
//
// Note: We accept any marker byte at the position; the caller decides what
// to do with it (0xFE = IDAM, 0xFB = DAM, anything else = ignore).
size_t findMfmMarker(const uint8_t* bits, size_t bitCount,
                      size_t startBit, uint8_t* marker) {
    size_t cursor = startBit;
    while (cursor + 16 * 4 <= bitCount) {
        const size_t s1 = findSync4489(bits, bitCount, cursor);
        if (s1 == kNotFound) return kNotFound;

        // Verify two more 0x4489 immediately follow (3-sync IBM convention).
        // The MOOF bitstream may already be byte-aligned to the sync, so we
        // check the next two 16-bit windows.
        if (s1 + 48 + 16 > bitCount) return kNotFound;
        bool tripleSync = true;
        for (int k = 1; k < 3; ++k) {
            uint16_t w = 0;
            for (int b = 0; b < 16; ++b) {
                w = static_cast<uint16_t>((w << 1) | readBit(bits, s1 + k * 16 + b));
            }
            if (w != 0x4489) { tripleSync = false; break; }
        }
        if (!tripleSync) {
            cursor = s1 + 1;
            continue;
        }
        const size_t markerBitPos = s1 + 48;
        if (markerBitPos + 16 > bitCount) return kNotFound;
        *marker = readMfmByte(bits, markerBitPos);
        return markerBitPos + 16;
    }
    return kNotFound;
}

} // namespace

std::vector<MacMfmSector> decodeMacMfmTrack(const uint8_t* bits,
                                              size_t bitCount,
                                              std::string& errorAccumulator) {
    std::vector<MacMfmSector> result;
    if (bits == nullptr || bitCount < 16 * 8) return result;

    size_t cursor = 0;
    int sawHeader = 0;
    int sawData   = 0;
    MacMfmSector pendingHeader{};
    bool havePendingHeader = false;

    while (cursor < bitCount) {
        uint8_t marker = 0;
        const size_t afterMarker = findMfmMarker(bits, bitCount, cursor, &marker);
        if (afterMarker == kNotFound) break;

        if (marker == 0xFE) {
            // IDAM — 4-byte header (C, H, R, N) + 2-byte CRC.
            if (afterMarker + (4 + 2) * 16 > bitCount) break;
            uint8_t hdr[4 + 2];
            for (int i = 0; i < 6; ++i) {
                hdr[i] = readMfmByte(bits, afterMarker + i * 16);
            }
            uint8_t crcInput[3 + 1 + 4];
            crcInput[0] = 0xA1; crcInput[1] = 0xA1; crcInput[2] = 0xA1;
            crcInput[3] = 0xFE;
            std::memcpy(crcInput + 4, hdr, 4);
            const uint16_t calc = crc16Ccitt(crcInput, sizeof(crcInput));
            const uint16_t got  = static_cast<uint16_t>((hdr[4] << 8) | hdr[5]);

            pendingHeader = MacMfmSector{};
            pendingHeader.cylinder    = hdr[0];
            pendingHeader.head        = hdr[1];
            pendingHeader.sector      = hdr[2];
            pendingHeader.sizeCode    = hdr[3];
            pendingHeader.headerCrcOk = (calc == got);
            havePendingHeader         = true;
            ++sawHeader;
            cursor = afterMarker + 6 * 16;
        } else if (marker == 0xFB && havePendingHeader) {
            // DAM — N=2 → 512 data bytes + 2 CRC. Reject other sizes (Mac
            // 1.44M only uses N=2).
            if (pendingHeader.sizeCode != 2) {
                havePendingHeader = false;
                cursor = afterMarker;
                continue;
            }
            if (afterMarker + (512 + 2) * 16 > bitCount) break;
            std::array<uint8_t, 512> dataBuf{};
            for (int i = 0; i < 512; ++i) {
                dataBuf[i] = readMfmByte(bits, afterMarker + i * 16);
            }
            const uint8_t crcHi = readMfmByte(bits, afterMarker + 512 * 16);
            const uint8_t crcLo = readMfmByte(bits, afterMarker + 513 * 16);

            // CRC over 3× 0xA1 + 0xFB + 512 data bytes.
            std::vector<uint8_t> crcInput;
            crcInput.reserve(3 + 1 + 512);
            crcInput.push_back(0xA1);
            crcInput.push_back(0xA1);
            crcInput.push_back(0xA1);
            crcInput.push_back(0xFB);
            crcInput.insert(crcInput.end(), dataBuf.begin(), dataBuf.end());
            const uint16_t calc = crc16Ccitt(crcInput.data(), crcInput.size());
            const uint16_t got  = static_cast<uint16_t>((crcHi << 8) | crcLo);

            pendingHeader.data       = dataBuf;
            pendingHeader.dataCrcOk  = (calc == got);
            result.push_back(pendingHeader);
            havePendingHeader = false;
            ++sawData;
            cursor = afterMarker + 514 * 16;
        } else {
            // Unknown marker — skip past the sync we just saw.
            cursor = afterMarker;
        }
    }

    if (result.empty() && errorAccumulator.empty()) {
        errorAccumulator = "no MFM sectors decoded (IDAM seen=" +
                           std::to_string(sawHeader) + ", DAM seen=" +
                           std::to_string(sawData) + ")";
    }
    return result;
}

} // namespace rde
