#ifndef RDEDISKTOOL_MACINTOSH_MACMFMENCODER_H
#define RDEDISKTOOL_MACINTOSH_MACMFMENCODER_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rde {

// 1.44M Mac MFM track encoder (IBM PC standard, byte-for-byte identical
// to a PC HD floppy track). Inverse of MacMfmDecoder.
//
// Output: raw bit stream (byte-packed MSB-first within each byte). Each
// 16-bit raw window encodes one data byte: clock + data alternating, with
// the standard MFM clock rule (clock = NOT(prev_data OR current_data))
// EXCEPT for the canonical 0xA1 sync byte which uses the special pattern
// 0x4489 (one clock bit dropped) so that no aligned data byte can collide.
//
// Per-track structure:
//   gap (80× 0x4E) + index gap +
//   for each sector in the IBM-standard interleave (1..18 in physical order):
//     12× 0x00 + 3× sync(0xA1) + 0xFE + C/H/R/N + CRC16-CCITT
//     22× 0x4E gap
//     12× 0x00 + 3× sync(0xA1) + 0xFB + 512 data + CRC16-CCITT
//     54× 0x4E gap
//   trailing gap fill
//
// Returns the raw bit stream: a byte-packed bitstream where each "data
// byte" of MFM-encoded output occupies 16 bits, MSB-first within each
// host byte.
struct MacMfmEncodedTrack {
    std::vector<uint8_t> bits;   // byte-packed, MSB-first
    size_t               bitCount = 0;  // valid bits in `bits`
};

MacMfmEncodedTrack encodeMacMfmTrack(const uint8_t* data,
                                       size_t dataSize,
                                       int cylinder,
                                       int head);

} // namespace rde

#endif // RDEDISKTOOL_MACINTOSH_MACMFMENCODER_H
