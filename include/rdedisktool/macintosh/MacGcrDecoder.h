#ifndef RDEDISKTOOL_MACINTOSH_MACGCRDECODER_H
#define RDEDISKTOOL_MACINTOSH_MACGCRDECODER_H

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rde {

// Mac GCR track decoder (Apple 6-and-2 / Sony "GCR-IIB" variant).
//
// Decodes a MOOF / WOZ-style bitstream of one CLV track into its logical
// sectors. The bitstream comes byte-packed, high-to-low within each byte
// (per the MOOF spec). Sector boundaries are found by scanning for the
// canonical address mark `D5 AA 96` and data mark `D5 AA AD` byte sequences.
//
// Algorithm references:
//   * snow / macformat.rs (encoder side, 297 LOC)
//   * fluxfox / bitstream_codec/gcr.rs (encoder + decoder, 518 LOC)
//   * Inside Macintosh: Files (Sony 3.5" GCR section)
//
// Per-sector raw payload is 524 bytes: 12-byte Sony "tag" + 512-byte data.
// rdedisktool only consumes the 512-byte data fork (HFS / MFS volumes
// don't reference the tag area).
struct MacGcrSector {
    uint8_t  track  = 0;          // 0..79 (high bit comes from headHigh)
    uint8_t  sector = 0;          // 0..11 (logical sector within track)
    uint8_t  side   = 0;          // 0 / 1
    uint8_t  format = 0;          // sample: 0x02 = 400K, 0x22 = 800K
    bool     headerChecksumOk = false;
    bool     dataChecksumOk   = false;
    std::array<uint8_t, 12>  tag{};
    std::array<uint8_t, 512> data{};
};

// Decode all sectors found in one bitstream track. `bits` is the byte-packed
// stream from the MOOF TRKS chunk; `bitCount` is the valid bit count (so the
// last byte may be partial — extra padding bits are ignored).
std::vector<MacGcrSector> decodeMacGcrTrack(const uint8_t* bits,
                                              size_t bitCount,
                                              std::string& errorAccumulator);

// Mac CLV (constant linear velocity) sector counts per zone.
// Track ranges:
//   Zone 0 = tracks  0..15  : 12 sectors/track
//   Zone 1 = tracks 16..31  : 11
//   Zone 2 = tracks 32..47  : 10
//   Zone 3 = tracks 48..63  :  9
//   Zone 4 = tracks 64..79  :  8
inline int macGcrSectorsForTrack(int track) {
    if (track < 16) return 12;
    if (track < 32) return 11;
    if (track < 48) return 10;
    if (track < 64) return 9;
    return 8;
}

// Linear sector index (= byte offset / 512) for a given (track, side, logical
// sector) on a Mac 400K (single-sided) or 800K (double-sided) volume.
//   sides == 1 → 400K
//   sides == 2 → 800K (track stripe alternates side 0 then side 1)
inline size_t macGcrLinearBlock(int sides, int track, int side, int sector) {
    size_t cumulative = 0;
    for (int t = 0; t < track; ++t) {
        cumulative += static_cast<size_t>(macGcrSectorsForTrack(t)) * sides;
    }
    cumulative += static_cast<size_t>(side) * macGcrSectorsForTrack(track);
    cumulative += static_cast<size_t>(sector);
    return cumulative;
}

} // namespace rde

#endif // RDEDISKTOOL_MACINTOSH_MACGCRDECODER_H
