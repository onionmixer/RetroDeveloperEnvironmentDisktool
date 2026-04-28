#ifndef RDEDISKTOOL_MACINTOSH_MACMFMDECODER_H
#define RDEDISKTOOL_MACINTOSH_MACMFMDECODER_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace rde {

// 1.44M Mac MFM track decoder.
//
// The Mac SuperDrive's 1.44M format is byte-for-byte the IBM PC standard:
//   * 80 cylinders × 2 heads × 18 sectors × 512 bytes  (= 1,474,560 bytes)
//   * MFM encoding (each data bit = 2 raw bits: clock + data)
//   * Pre-IDAM: gap + 12× 0x00 + 3× 0xA1 sync (special, 0x4489 in raw MFM)
//                 + 0xFE marker + C/H/R/N + CRC16-CCITT
//   * Pre-DAM:  gap + 12× 0x00 + 3× 0xA1 sync + 0xFB marker
//                 + 512 data bytes + CRC16-CCITT
//
// Decoder strategy: scan the raw MOOF bitstream for the special 16-bit
// pattern 0x4489 (0xA1 with one missing clock bit, the canonical sync), then
// from that bit-aligned position read 16 raw bits per data byte (taking the
// odd-indexed bit of each pair as the data bit).
struct MacMfmSector {
    uint8_t  cylinder = 0;       // 0..79  (= track)
    uint8_t  head     = 0;       // 0 / 1  (= side)
    uint8_t  sector   = 0;       // 1..18  (per IBM convention, 1-indexed)
    uint8_t  sizeCode = 2;       // N: 0=128, 1=256, 2=512, 3=1024 (Mac uses 2)
    bool     headerCrcOk = false;
    bool     dataCrcOk   = false;
    std::array<uint8_t, 512> data{};
};

// Decode all sectors in one MFM track. `bits` is the byte-packed MOOF
// bitstream (MSB-first within each byte); `bitCount` is the valid bit count.
std::vector<MacMfmSector> decodeMacMfmTrack(const uint8_t* bits,
                                              size_t bitCount,
                                              std::string& errorAccumulator);

// Linear sector index for IBM-format 1.44M (track-major, head alternating
// per track stripe — matches raw .img layout).
inline size_t macMfmLinearBlock(int cylinder, int head, int sector1Indexed) {
    constexpr int kSectorsPerTrack = 18;
    constexpr int kHeads           = 2;
    return static_cast<size_t>(
        ((cylinder * kHeads) + head) * kSectorsPerTrack +
        (sector1Indexed - 1));
}

} // namespace rde

#endif // RDEDISKTOOL_MACINTOSH_MACMFMDECODER_H
