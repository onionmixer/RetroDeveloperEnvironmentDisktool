#include "rdedisktool/macintosh/MacGcrEncoder.h"
#include "rdedisktool/macintosh/MacGcrDecoder.h"  // for macGcrSectorsForTrack/macGcrLinearBlock

#include <stdexcept>
#include <string>

namespace rde {

namespace {

// 6-and-2 forward table — same as MacGcrDecoder's kGcrEncode.
constexpr std::array<uint8_t, 64> kGcrEncode = {
    0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6, 0xA7, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2, 0xB3,
    0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3,
    0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE5, 0xE6, 0xE7, 0xE9, 0xEA, 0xEB, 0xEC,
    0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF,
};

// Apple-standard sequences (per Inside Mac, snow, FluxEngine).
constexpr uint8_t kAutoSync[6]  = {0xFF, 0x3F, 0xCF, 0xF3, 0xFC, 0xFF};
constexpr uint8_t kAddrMark[3]  = {0xD5, 0xAA, 0x96};
constexpr uint8_t kDataMark[3]  = {0xD5, 0xAA, 0xAD};
constexpr uint8_t kBitSlip[4]   = {0xDE, 0xAA, 0xFF, 0xFF};

// Sector interleave per zone (5 zones, sectors-per-track decreases inward).
// Matches snow's macformat.rs SECTOR_INTERLEAVE.
const std::vector<int> kInterleave[5] = {
    {0, 6, 1, 7, 2, 8, 3, 9, 4, 10, 5, 11},   // zone 0: 12 sectors
    {0, 6, 1, 7, 2, 8, 3, 9, 4, 10, 5},        // zone 1: 11
    {0, 5, 1, 6, 2, 7, 3, 8, 4, 9},            // zone 2: 10
    {0, 5, 1, 6, 2, 7, 3, 8, 4},               // zone 3: 9
    {0, 4, 1, 5, 2, 6, 3, 7},                  // zone 4: 8
};

constexpr size_t kSectorTagSize  = 12;
constexpr size_t kSectorDataSize = 512;
constexpr size_t kSectorPayload  = kSectorTagSize + kSectorDataSize;  // 524

inline void pushBytes(std::vector<uint8_t>& out, const uint8_t* p, size_t n) {
    out.insert(out.end(), p, p + n);
}

// Encode a logical 0..63 value with the 6-and-2 forward table.
inline void pushEnc(std::vector<uint8_t>& out, uint8_t v) {
    out.push_back(kGcrEncode[v & 0x3F]);
}

// Encode the 524-byte sector payload (12-byte tag + 512-byte data) into the
// 703-byte GCR pre-image + 4-byte trailing checksum, then map each 6-bit
// value through kGcrEncode. Mirrors snow's encode_sector_data exactly.
std::vector<uint8_t> encodeSectorPayload(const uint8_t* payload) {
    constexpr size_t LL = kSectorPayload / 3;  // 174

    std::vector<uint8_t> b1(LL + 1, 0);
    std::vector<uint8_t> b2(LL + 1, 0);
    std::vector<uint8_t> b3(LL + 1, 0);
    uint32_t c1 = 0, c2 = 0, c3 = 0;
    size_t in = 0;

    for (size_t j = 0;; ++j) {
        c1 = (c1 & 0xFFu) << 1;
        if (c1 & 0x100u) c1 += 1;

        const uint8_t v_a = payload[in++];
        c3 += v_a;
        if (c1 & 0x100u) {
            c3 += 1;
            c1 &= 0xFFu;
        }
        b1[j] = static_cast<uint8_t>(v_a ^ static_cast<uint8_t>(c1));

        const uint8_t v_b = payload[in++];
        c2 += v_b;
        if (c3 > 0xFFu) {
            c2 += 1;
            c3 &= 0xFFu;
        }
        b2[j] = static_cast<uint8_t>(v_b ^ static_cast<uint8_t>(c3));

        if (in == kSectorPayload) {
            // Last iteration: only 2 input bytes consumed. b3[LL] stays 0.
            break;
        }

        const uint8_t v_c = payload[in++];
        c1 += v_c;
        if (c2 > 0xFFu) {
            c1 += 1;
            c2 &= 0xFFu;
        }
        b3[j] = static_cast<uint8_t>(v_c ^ static_cast<uint8_t>(c2));
    }
    b3[LL] = 0;
    const uint32_t c4 = ((c1 & 0xC0u) >> 6) | ((c2 & 0xC0u) >> 4) | ((c3 & 0xC0u) >> 2);

    // Re-pack into quartets (w4, w1, w2, w3) per snow's format.
    std::vector<uint8_t> sixBit;
    sixBit.reserve(703);
    for (size_t i = 0; i <= LL; ++i) {
        const uint8_t w1 = b1[i] & 0x3F;
        const uint8_t w2 = b2[i] & 0x3F;
        const uint8_t w3 = b3[i] & 0x3F;
        const uint8_t w4 = static_cast<uint8_t>(((b1[i] & 0xC0) >> 2) |
                                                  ((b2[i] & 0xC0) >> 4) |
                                                  ((b3[i] & 0xC0) >> 6));
        sixBit.push_back(w4);
        sixBit.push_back(w1);
        sixBit.push_back(w2);
        if (i != LL) sixBit.push_back(w3);
    }
    sixBit.push_back(static_cast<uint8_t>(c4) & 0x3F);
    sixBit.push_back(static_cast<uint8_t>(c3) & 0x3F);
    sixBit.push_back(static_cast<uint8_t>(c2) & 0x3F);
    sixBit.push_back(static_cast<uint8_t>(c1) & 0x3F);

    // Map through forward table.
    std::vector<uint8_t> out;
    out.reserve(sixBit.size());
    for (uint8_t v : sixBit) out.push_back(kGcrEncode[v]);
    return out;
}

void pushSectorHeader(std::vector<uint8_t>& out, int track, int side,
                       int tsector, int sides) {
    // 6× auto-sync gap before each sector.
    for (int i = 0; i < 6; ++i) pushBytes(out, kAutoSync, sizeof(kAutoSync));

    pushBytes(out, kAddrMark, sizeof(kAddrMark));

    uint8_t checksum = 0;
    const uint8_t trackLow = static_cast<uint8_t>(track) & 0x3F;
    pushEnc(out, trackLow);
    checksum ^= trackLow;

    const uint8_t sec = static_cast<uint8_t>(tsector) & 0x1F;
    pushEnc(out, sec);
    checksum ^= sec;

    // Bit 5 = head/side, bit 0 = track high (track >> 6).
    const uint8_t hthigh = static_cast<uint8_t>(
        ((static_cast<uint8_t>(side) & 1) << 5) |
         ((static_cast<uint8_t>(track) >> 6) & 1));
    pushEnc(out, hthigh);
    checksum ^= hthigh;

    // Format: 0x02 = 400K (single-sided), 0x22 = 800K (double-sided).
    const uint8_t format = (sides == 2) ? 0x22 : 0x02;
    pushEnc(out, format);
    checksum ^= format;

    pushEnc(out, checksum & 0x3F);

    pushBytes(out, kBitSlip, sizeof(kBitSlip));
}

void pushSectorData(std::vector<uint8_t>& out, int tsector,
                     const uint8_t* tag, const uint8_t* data) {
    pushBytes(out, kAutoSync, sizeof(kAutoSync));
    pushBytes(out, kDataMark, sizeof(kDataMark));

    pushEnc(out, static_cast<uint8_t>(tsector) & 0x1F);

    std::vector<uint8_t> payload(kSectorPayload, 0);
    for (size_t i = 0; i < kSectorTagSize; ++i)  payload[i]                  = tag[i];
    for (size_t i = 0; i < kSectorDataSize; ++i) payload[kSectorTagSize + i] = data[i];

    auto encoded = encodeSectorPayload(payload.data());
    pushBytes(out, encoded.data(), encoded.size());

    pushBytes(out, kBitSlip, sizeof(kBitSlip));
}

} // namespace

std::vector<uint8_t> encodeMacGcrTrack(const uint8_t* data,
                                         size_t dataSize,
                                         int sides,
                                         int track,
                                         int side) {
    if (sides != 1 && sides != 2) {
        throw std::invalid_argument(
            "encodeMacGcrTrack: sides must be 1 or 2 (got " +
            std::to_string(sides) + ")");
    }
    if (track < 0 || track >= 80) {
        throw std::invalid_argument(
            "encodeMacGcrTrack: track out of range (0..79)");
    }
    if (side < 0 || side >= sides) {
        throw std::invalid_argument(
            "encodeMacGcrTrack: side out of range");
    }
    const int sectorsPerTrack = macGcrSectorsForTrack(track);
    const size_t expected = (sides == 2) ? (800u * 1024u) : (400u * 1024u);
    if (dataSize != expected) {
        throw std::invalid_argument(
            "encodeMacGcrTrack: dataSize " + std::to_string(dataSize) +
            " (expected " + std::to_string(expected) + ")");
    }

    const int zone = track / 16;
    const auto& interleave = kInterleave[zone];

    static const uint8_t kZeroTag[kSectorTagSize] = {0};

    std::vector<uint8_t> out;
    out.reserve(static_cast<size_t>(sectorsPerTrack) * 1024);

    for (int tsector : interleave) {
        if (tsector < 0 || tsector >= sectorsPerTrack) continue;
        const size_t lin = macGcrLinearBlock(sides, track, side, tsector);
        const size_t off = lin * kSectorDataSize;
        if (off + kSectorDataSize > dataSize) {
            throw std::invalid_argument(
                "encodeMacGcrTrack: linear block overflow (track=" +
                std::to_string(track) + ", side=" + std::to_string(side) +
                ", sector=" + std::to_string(tsector) + ")");
        }
        pushSectorHeader(out, track, side, tsector, sides);
        pushSectorData(out, tsector, kZeroTag, data + off);
    }
    return out;
}

} // namespace rde
