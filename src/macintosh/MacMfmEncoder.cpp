#include "rdedisktool/macintosh/MacMfmEncoder.h"
#include "rdedisktool/macintosh/MacMfmDecoder.h"  // for macMfmLinearBlock

#include <stdexcept>
#include <string>

namespace rde {

namespace {

constexpr int kSectorsPerTrack = 18;
constexpr int kHeads           = 2;
constexpr int kCylinders       = 80;
constexpr size_t kSectorSize   = 512;

// MFM track gap sizes (PC HD 1.44M reference, IBM System 34).
constexpr int kPreIndexGap   = 80;     // pre-index gap (0x4E)
constexpr int kIndexSync     = 12;     // 12× 0x00
constexpr int kPreIdGap      = 50;     // 0x4E filler before each ID (gap1/gap3)
constexpr int kIdGap         = 22;     // 22× 0x4E between ID and DATA fields
constexpr int kPreDataSync   = 12;     // 12× 0x00 before DAM
constexpr int kPostDataGap   = 54;     // 54× 0x4E after DATA CRC

// CRC16-CCITT (polynomial 0x1021, init 0xFFFF) — same as decoder.
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

// MFM bit emitter: pushes a single raw bit (clock or data) into a packed
// MSB-first buffer.
struct BitSink {
    std::vector<uint8_t> bits;
    size_t bitCount = 0;

    void pushBit(uint8_t b) {
        if ((bitCount & 7) == 0) bits.push_back(0);
        if (b & 1) bits.back() |= static_cast<uint8_t>(1u << (7 - (bitCount & 7)));
        ++bitCount;
    }
    // Emit a 16-bit MFM word (MSB first).
    void pushWord16(uint16_t w) {
        for (int i = 15; i >= 0; --i) pushBit(static_cast<uint8_t>((w >> i) & 1));
    }
};

// Encode one data byte to a 16-bit MFM word given the previous data bit
// (for the clock rule between byte boundaries). Returns (word, last_data_bit).
//
// MFM rule: each data bit is preceded by a clock bit. Clock = 1 iff both
// the previous data bit AND the current data bit are 0.
//
// Word layout (MSB → LSB): C7 D7 C6 D6 C5 D5 C4 D4 C3 D3 C2 D2 C1 D1 C0 D0
std::pair<uint16_t, bool> mfmEncodeByte(uint8_t v, bool prevDataBit) {
    uint16_t w = 0;
    bool prev = prevDataBit;
    for (int i = 7; i >= 0; --i) {
        const bool data = ((v >> i) & 1) != 0;
        const bool clock = !prev && !data;
        // Clock at position 2i+1, data at position 2i.
        if (clock) w = static_cast<uint16_t>(w | (1u << (2 * i + 1)));
        if (data)  w = static_cast<uint16_t>(w | (1u << (2 * i + 0)));
        prev = data;
    }
    return {w, prev};
}

// 0xA1 sync byte: standard MFM would be 0x4A89, but we use 0x4489 (drop
// the clock bit between bits 4 and 5 of A1) per IBM convention so the
// pattern can't be matched on aligned data.
constexpr uint16_t kSyncA1Raw = 0x4489;

// Push N copies of a normal MFM-encoded byte through a BitSink, threading
// the previous-data-bit state across bytes.
void pushMfmBytes(BitSink& sink, const uint8_t* src, size_t n, bool& prev) {
    for (size_t i = 0; i < n; ++i) {
        auto [w, lp] = mfmEncodeByte(src[i], prev);
        sink.pushWord16(w);
        prev = lp;
    }
}
void pushMfmFill(BitSink& sink, uint8_t v, size_t n, bool& prev) {
    for (size_t i = 0; i < n; ++i) {
        auto [w, lp] = mfmEncodeByte(v, prev);
        sink.pushWord16(w);
        prev = lp;
    }
}

// Push 3 sync bytes (0xA1 with dropped clock = 0x4489 raw).
void pushSyncA1x3(BitSink& sink, bool& prev) {
    for (int i = 0; i < 3; ++i) {
        sink.pushWord16(kSyncA1Raw);
    }
    // After 0xA1 sync, last data bit is 1 (LSB of 0xA1).
    prev = true;
}

} // namespace

MacMfmEncodedTrack encodeMacMfmTrack(const uint8_t* data,
                                       size_t dataSize,
                                       int cylinder,
                                       int head) {
    if (cylinder < 0 || cylinder >= kCylinders) {
        throw std::invalid_argument("encodeMacMfmTrack: cylinder out of range");
    }
    if (head < 0 || head >= kHeads) {
        throw std::invalid_argument("encodeMacMfmTrack: head out of range");
    }
    constexpr size_t kExpected =
        static_cast<size_t>(kCylinders) * kHeads * kSectorsPerTrack * kSectorSize;
    if (dataSize != kExpected) {
        throw std::invalid_argument(
            "encodeMacMfmTrack: dataSize " + std::to_string(dataSize) +
            " (expected " + std::to_string(kExpected) + ")");
    }

    BitSink sink;
    bool prev = false;

    // Pre-index gap.
    pushMfmFill(sink, 0x4E, kPreIndexGap, prev);

    for (int sec1 = 1; sec1 <= kSectorsPerTrack; ++sec1) {
        // ID address mark:
        //   12× 0x00 sync, 3× sync 0xA1 (raw 0x4489), 0xFE, C, H, R, N, CRC.
        pushMfmFill(sink, 0x00, kIndexSync, prev);
        pushSyncA1x3(sink, prev);

        uint8_t hdr[5] = {
            0xFE,                           // IDAM marker
            static_cast<uint8_t>(cylinder), // C
            static_cast<uint8_t>(head),     // H
            static_cast<uint8_t>(sec1),     // R (1..18)
            2                                // N=2 (512-byte sectors)
        };
        // CRC over 3× 0xA1 + 0xFE + (C,H,R,N).
        uint8_t crcBuf[3 + 5] = {0xA1, 0xA1, 0xA1, hdr[0], hdr[1], hdr[2], hdr[3], hdr[4]};
        const uint16_t hdrCrc = crc16Ccitt(crcBuf, sizeof(crcBuf));
        pushMfmBytes(sink, hdr, sizeof(hdr), prev);
        const uint8_t crcBytes[2] = {
            static_cast<uint8_t>((hdrCrc >> 8) & 0xFF),
            static_cast<uint8_t>(hdrCrc & 0xFF)
        };
        pushMfmBytes(sink, crcBytes, sizeof(crcBytes), prev);

        // Gap between ID and DATA.
        pushMfmFill(sink, 0x4E, kIdGap, prev);

        // DATA address mark:
        //   12× 0x00 sync, 3× sync 0xA1, 0xFB, 512 data bytes, CRC.
        pushMfmFill(sink, 0x00, kPreDataSync, prev);
        pushSyncA1x3(sink, prev);

        const uint8_t dataMarker = 0xFB;
        const size_t lin = macMfmLinearBlock(cylinder, head, sec1);
        const uint8_t* secBytes = data + lin * kSectorSize;
        if (lin * kSectorSize + kSectorSize > dataSize) {
            throw std::invalid_argument(
                "encodeMacMfmTrack: linear block overflow");
        }

        std::vector<uint8_t> dataCrcBuf;
        dataCrcBuf.reserve(3 + 1 + kSectorSize);
        dataCrcBuf.push_back(0xA1);
        dataCrcBuf.push_back(0xA1);
        dataCrcBuf.push_back(0xA1);
        dataCrcBuf.push_back(dataMarker);
        dataCrcBuf.insert(dataCrcBuf.end(), secBytes, secBytes + kSectorSize);
        const uint16_t dataCrc = crc16Ccitt(dataCrcBuf.data(), dataCrcBuf.size());

        pushMfmBytes(sink, &dataMarker, 1, prev);
        pushMfmBytes(sink, secBytes, kSectorSize, prev);
        const uint8_t dCrcBytes[2] = {
            static_cast<uint8_t>((dataCrc >> 8) & 0xFF),
            static_cast<uint8_t>(dataCrc & 0xFF)
        };
        pushMfmBytes(sink, dCrcBytes, sizeof(dCrcBytes), prev);

        // Post-data gap.
        pushMfmFill(sink, 0x4E, kPostDataGap, prev);
    }

    // Pad to a final small trailing gap so that the bitstream length is a
    // multiple of 8 bits (already is, since each byte = 16 bits) and ends
    // on a clean boundary.
    return MacMfmEncodedTrack{std::move(sink.bits), sink.bitCount};
}

} // namespace rde
