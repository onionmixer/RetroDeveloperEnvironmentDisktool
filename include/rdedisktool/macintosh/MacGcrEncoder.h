#ifndef RDEDISKTOOL_MACINTOSH_MACGCRENCODER_H
#define RDEDISKTOOL_MACINTOSH_MACGCRENCODER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rde {

// Mac GCR (Apple 6-and-2 / Sony 3.5") track encoder.
//
// Inverse of MacGcrDecoder. Algorithm cross-referenced with snow's
// `floppy/src/macformat.rs` (MIT, Thomas W. <thomas@thomasw.dev>) which
// itself adapts Greaseweazle / FluxEngine / MESS code.
//
// Output: raw byte stream of one track (byte-aligned 8 bits per byte,
// MSB-first when packed into the MOOF bitstream). Each sector contributes
// auto-sync gap + address mark + 5-byte header + bit-slip + auto-sync +
// data mark + 1-byte sector ID + 703-byte encoded sector + bit-slip.
//
//   sides == 1 → 400K (single-sided), data is 400 KiB
//   sides == 2 → 800K (double-sided),  data is 800 KiB
//
// Throws std::invalid_argument if `data` is not the expected size or if
// (track, side) is out of range.

// Encode one full Mac CLV track as a byte stream. `data` is the full raw
// volume (400K or 800K, depending on `sides`); `track` is 0..79; `side` is
// 0..(sides-1). Returns the per-track encoded byte stream that the MOOF
// writer should pack into a TRKS bitstream entry.
std::vector<uint8_t> encodeMacGcrTrack(const uint8_t* data,
                                         size_t dataSize,
                                         int sides,
                                         int track,
                                         int side);

} // namespace rde

#endif // RDEDISKTOOL_MACINTOSH_MACGCRENCODER_H
