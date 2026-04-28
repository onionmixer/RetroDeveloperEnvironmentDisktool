#include "rdedisktool/macintosh/MacGcrDecoder.h"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace rde {

namespace {

// Apple 6-and-2 GCR forward table (from snow / FluxEngine / Inside Mac).
// Index = 6-bit logical value; value = 8-bit on-disk byte (bit 7 set
// always, range 0x96..0xFF). Mirrors snow's GCR_ENCTABLE.
constexpr std::array<uint8_t, 64> kGcrEncode = {
    0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6, 0xA7, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2, 0xB3,
    0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3,
    0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE5, 0xE6, 0xE7, 0xE9, 0xEA, 0xEB, 0xEC,
    0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF,
};

// Inverse table (256 entries). 0xFF = invalid (not a GCR-encoded byte).
struct GcrDecodeTable {
    std::array<uint8_t, 256> tbl{};
    constexpr GcrDecodeTable() {
        for (auto& x : tbl) x = 0xFF;
        for (size_t i = 0; i < kGcrEncode.size(); ++i) {
            tbl[kGcrEncode[i]] = static_cast<uint8_t>(i);
        }
    }
};
constexpr GcrDecodeTable kGcrDecode{};

// Marks (post-sync byte sequences in the bitstream).
constexpr uint8_t kAddrMark0 = 0xD5;
constexpr uint8_t kAddrMark1 = 0xAA;
constexpr uint8_t kAddrMark2 = 0x96;  // address mark
constexpr uint8_t kDataMark2 = 0xAD;  // data mark

// Sector data is encoded as 703 GCR-encoded bytes (175 quartet groups
// minus the last quartet's `w3`, plus 4 trailing checksum bytes).
constexpr size_t kEncodedSectorBytes  = 175 * 4 - 1 + 4;
constexpr size_t kSectorPayloadBytes  = 524;  // 12 tag + 512 data
constexpr size_t kEncodedHeaderBytes  = 5;    // track_low, sector, head_high,
                                                // format, header checksum

// Walk byte stream for the address mark `D5 AA 96`. Returns the byte
// offset just past the mark, or std::nullopt if not found.
std::optional<size_t> findMark(const uint8_t* data, size_t len,
                                 size_t startAt, uint8_t mark2) {
    for (size_t i = startAt; i + 2 < len; ++i) {
        if (data[i]   == kAddrMark0 &&
            data[i+1] == kAddrMark1 &&
            data[i+2] == mark2) {
            return i + 3;
        }
    }
    return std::nullopt;
}

// Convert a MOOF bitstream (bit-packed MSB-first within each byte, with
// arbitrary bit-slip between encoded GCR bytes) into a byte stream of
// GCR-encoded bytes. Apple's GCR controller emulates the hardware shift
// register: shift bits in MSB-first; whenever bit 7 of the register becomes
// 1, capture the full byte (which is guaranteed to be one of the 64 valid
// GCR codes 0x96..0xFF) and reset. This handles auto-sync gaps naturally
// because sync bytes (0xFF with bit-slip) just produce 0xFF in the stream.
std::vector<uint8_t> alignBitstreamToBytes(const uint8_t* bits,
                                             size_t bitCount) {
    std::vector<uint8_t> out;
    if (bits == nullptr || bitCount == 0) return out;
    out.reserve(bitCount / 8 + 8);

    uint16_t reg = 0;
    bool primed = false;  // ignore bytes captured before the first "real" sync
    for (size_t i = 0; i < bitCount; ++i) {
        const uint8_t bit = (bits[i >> 3] >> (7 - (i & 7))) & 1u;
        reg = static_cast<uint16_t>((reg << 1) | bit);
        if (reg & 0x80) {
            const uint8_t b = static_cast<uint8_t>(reg & 0xFF);
            if (!primed) {
                // Wait until we've consumed at least 8 bits before trusting.
                if (i >= 8) primed = true;
            }
            if (primed) out.push_back(b);
            reg = 0;
        }
    }
    return out;
}

// Decode a single 6-bit value from one bitstream byte. 0xFF on invalid.
inline uint8_t decodeSixBit(uint8_t b) { return kGcrDecode.tbl[b]; }

// Decode the 5-byte sector header (post-AM). Returns 4 logical bytes
// + computed XOR checksum match flag.
struct DecodedHeader {
    uint8_t trackLow      = 0;
    uint8_t sector        = 0;
    uint8_t headHigh      = 0;  // bit 5 = side, bit 0 = track_high
    uint8_t format        = 0;
    bool    checksumOk    = false;
};
bool decodeHeader(const uint8_t* hdr, DecodedHeader& out) {
    uint8_t v[5];
    for (int i = 0; i < 5; ++i) {
        v[i] = decodeSixBit(hdr[i]);
        if (v[i] == 0xFF) return false;
    }
    out.trackLow = v[0];
    out.sector   = v[1];
    out.headHigh = v[2];
    out.format   = v[3];
    const uint8_t expectedSum = static_cast<uint8_t>(v[0] ^ v[1] ^ v[2] ^ v[3]);
    out.checksumOk = (expectedSum == v[4]);
    return true;
}

// Decode 703 GCR-encoded bytes into the 524-byte sector payload + verify
// the 4-byte trailing checksum. Returns true if both decode succeeded
// and checksum matched.
//
// Inverse of snow's encode_sector_data (macformat.rs). The 6-bit values
// from the stream form a sequence of quartets (w4, w1, w2, w3) for
// i=0..173, then a triplet (w4, w1, w2) for i=174, then 4 checksum
// values (c4, c3, c2, c1).
bool decodeSectorData(const uint8_t* enc,
                       std::array<uint8_t, kSectorPayloadBytes>& out,
                       bool& checksumOk) {
    constexpr size_t LL = kSectorPayloadBytes / 3;  // 174

    // 1. Decode 703 GCR bytes to 6-bit values.
    std::array<uint8_t, kEncodedSectorBytes> v{};
    for (size_t i = 0; i < kEncodedSectorBytes; ++i) {
        v[i] = decodeSixBit(enc[i]);
        if (v[i] == 0xFF) return false;
    }

    // 2. Reassemble b1[i], b2[i], b3[i] arrays from quartets.
    std::array<uint8_t, LL + 1> b1{}, b2{}, b3{};
    size_t cursor = 0;
    for (size_t i = 0; i <= LL; ++i) {
        const uint8_t w4 = v[cursor++];
        const uint8_t w1 = v[cursor++];
        const uint8_t w2 = v[cursor++];
        const uint8_t w3 = (i == LL) ? 0 : v[cursor++];
        b1[i] = static_cast<uint8_t>(((w4 << 2) & 0xC0) | (w1 & 0x3F));
        b2[i] = static_cast<uint8_t>(((w4 << 4) & 0xC0) | (w2 & 0x3F));
        b3[i] = static_cast<uint8_t>(((w4 << 6) & 0xC0) | (w3 & 0x3F));
    }
    const uint8_t c4_stream = v[cursor++];
    const uint8_t c3_stream = v[cursor++];
    const uint8_t c2_stream = v[cursor++];
    const uint8_t c1_stream = v[cursor++];
    (void)cursor;

    // 3. Reverse the c1/c2/c3 mixing to recover the input bytes.
    uint32_t c1 = 0, c2 = 0, c3 = 0;
    size_t outIdx = 0;
    for (size_t j = 0; j <= LL; ++j) {
        // Step a: c1 ROL by 1; capture old bit 7 as carry.
        const bool carryC1 = (c1 & 0x80) != 0;
        c1 = ((c1 << 1) | (carryC1 ? 1u : 0u)) & 0xFFu;

        // Recover val_a = b1[j] ^ c1 (after step 6's mask, c1 is in 0..0xFF
        // already because we masked c1 above before XOR).
        const uint8_t val_a = static_cast<uint8_t>(b1[j] ^ c1);
        out[outIdx++] = val_a;
        c3 += val_a;
        if (carryC1) c3 += 1;

        // Step b: val_b = b2[j] ^ (c3 & 0xff after potential mask).
        const bool carryC3 = (c3 > 0xFFu);
        const uint8_t c3_low = static_cast<uint8_t>(c3 & 0xFFu);
        const uint8_t val_b = static_cast<uint8_t>(b2[j] ^ c3_low);
        if (j < LL) {                         // last iteration only has 2 bytes
            out[outIdx++] = val_b;
        } else {
            // Verify b2[LL] decoded value matches the encoder's "no third
            // byte" semantics: encoder breaks after 2nd byte at i=LL, so
            // val_b is the LAST input byte (LL*3 + 1 = 523-th, 0-indexed).
            out[outIdx++] = val_b;
        }
        c2 += val_b;
        if (carryC3) {
            c2 += 1;
            c3 = c3_low;
        }

        if (j == LL) break;  // no third byte in last iteration

        // Step c: val_c = b3[j] ^ (c2 after potential mask).
        const bool carryC2 = (c2 > 0xFFu);
        const uint8_t c2_low = static_cast<uint8_t>(c2 & 0xFFu);
        const uint8_t val_c = static_cast<uint8_t>(b3[j] ^ c2_low);
        out[outIdx++] = val_c;
        c1 += val_c;
        if (carryC2) {
            c1 += 1;
            c2 = c2_low;
        }
    }

    if (outIdx != kSectorPayloadBytes) {
        return false;
    }

    // 4. Verify checksum.
    const uint8_t expC4 = static_cast<uint8_t>(
        ((c1 & 0xC0) >> 6) | ((c2 & 0xC0) >> 4) | ((c3 & 0xC0) >> 2));
    const uint8_t expC3 = static_cast<uint8_t>(c3 & 0x3F);
    const uint8_t expC2 = static_cast<uint8_t>(c2 & 0x3F);
    const uint8_t expC1 = static_cast<uint8_t>(c1 & 0x3F);
    checksumOk = (expC4 == c4_stream) && (expC3 == c3_stream) &&
                 (expC2 == c2_stream) && (expC1 == c1_stream);
    return true;
}

} // namespace

std::vector<MacGcrSector> decodeMacGcrTrack(const uint8_t* bitsRaw,
                                              size_t bitCount,
                                              std::string& errorAccumulator) {
    std::vector<MacGcrSector> result;
    if (bitsRaw == nullptr || bitCount < 64) return result;

    // Re-align the bit-packed MOOF stream to one-GCR-byte-per-output-byte.
    const std::vector<uint8_t> aligned = alignBitstreamToBytes(bitsRaw, bitCount);
    const uint8_t* bits     = aligned.data();
    const size_t   byteCount = aligned.size();
    if (byteCount == 0) return result;

    size_t cursor = 0;
    while (cursor + kEncodedHeaderBytes < byteCount) {
        // 1. Find next address mark.
        auto amOpt = findMark(bits, byteCount, cursor, kAddrMark2);
        if (!amOpt.has_value()) break;
        size_t hdrPos = amOpt.value();
        if (hdrPos + kEncodedHeaderBytes > byteCount) break;

        // 2. Decode the 5-byte header.
        DecodedHeader hdr;
        if (!decodeHeader(bits + hdrPos, hdr)) {
            cursor = hdrPos + 1;
            continue;
        }

        // 3. Find the matching data mark within a reasonable window.
        // Inside Mac: bit-slip seq + auto-sync group + DM. ~8 bytes + sync
        // (~6) + DM-prefix (~3). Plus encoder sentinel slack.
        const size_t searchStart = hdrPos + kEncodedHeaderBytes;
        auto dmOpt = findMark(bits, byteCount, searchStart, kDataMark2);
        if (!dmOpt.has_value()) {
            // No DM in track — broken sector, skip.
            cursor = searchStart;
            continue;
        }
        const size_t dmPos = dmOpt.value();
        if (dmPos > searchStart + 64) {
            // DM too far away — treat the AM as spurious; advance.
            cursor = searchStart;
            continue;
        }

        // 4. Decode sector ID byte (not the same as header.sector — most of
        // the time they match, but encoder writes it as a separate field).
        if (dmPos + 1 + kEncodedSectorBytes > byteCount) break;
        const uint8_t sectorId = decodeSixBit(bits[dmPos]);
        if (sectorId == 0xFF) {
            cursor = dmPos + 1;
            continue;
        }

        // 5. Decode the encoded sector payload + checksum.
        MacGcrSector s;
        s.track  = static_cast<uint8_t>(
            hdr.trackLow | ((hdr.headHigh & 0x01) << 6));
        s.sector = hdr.sector & 0x1F;
        s.side   = (hdr.headHigh >> 5) & 0x01;
        s.format = hdr.format;
        s.headerChecksumOk = hdr.checksumOk;

        std::array<uint8_t, kSectorPayloadBytes> payload{};
        bool ckOk = false;
        if (!decodeSectorData(bits + dmPos + 1, payload, ckOk)) {
            cursor = dmPos + 1;
            continue;
        }
        s.dataChecksumOk = ckOk;
        std::memcpy(s.tag.data(),  payload.data(),     12);
        std::memcpy(s.data.data(), payload.data() + 12, 512);
        result.push_back(s);

        cursor = dmPos + 1 + kEncodedSectorBytes;
    }

    if (result.empty() && errorAccumulator.empty()) {
        errorAccumulator = "no sectors decoded from track";
    }
    return result;
}

} // namespace rde
