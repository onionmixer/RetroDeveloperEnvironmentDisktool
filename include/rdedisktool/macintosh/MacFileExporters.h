#ifndef RDEDISKTOOL_MACINTOSH_MACFILEEXPORTERS_H
#define RDEDISKTOOL_MACINTOSH_MACFILEEXPORTERS_H

#include <cstdint>
#include <string>
#include <vector>

namespace rde {

/**
 * AppleDouble v2 sidecar input record.
 *
 * Per SPEC §1648:
 *   - magic 0x00051607, version 0x00020000, 16-byte filler
 *   - entry table with three entries when fully populated:
 *       id 3: real name (MacRoman)
 *       id 9: Finder info (32 bytes — 16-byte FInfo + 16-byte FXInfo)
 *       id 2: resource fork
 */
struct AppleDoubleInput {
    std::string macRomanName;       // file name (MacRoman, raw)
    std::vector<uint8_t> finderInfo; // 32 bytes (FInfo + FXInfo). Empty allowed.
    std::vector<uint8_t> resourceFork; // raw bytes. Empty allowed.
};

/**
 * Build the binary content of an AppleDouble v2 sidecar.
 * Entries that have empty payloads are intentionally still written (so the
 * sidecar shape matches the Python reference tool byte-for-byte).
 */
std::vector<uint8_t> buildAppleDoubleSidecar(const AppleDoubleInput& in);

/**
 * MacBinary v1 input record. Per SPEC §1690 a 128-byte header is followed
 * by the data fork (zero-padded to 128B), then the resource fork (zero-
 * padded to 128B).
 */
struct MacBinaryInput {
    std::string macRomanName;       // max 63 bytes
    uint8_t  fileType[4]  = {0};
    uint8_t  creator[4]   = {0};
    uint8_t  finderFlagsHi = 0;     // header offset 73
    uint8_t  finderInfoLocation[6] = {0}; // header offset 75..80
    uint8_t  protectedFlag = 0;     // header offset 81
    uint32_t dataLength = 0;
    uint32_t rsrcLength = 0;
    uint32_t createDate = 0;
    uint32_t modifyDate = 0;
    std::vector<uint8_t> dataFork;
    std::vector<uint8_t> resourceFork;
};

std::vector<uint8_t> buildMacBinary(const MacBinaryInput& in);

} // namespace rde

#endif // RDEDISKTOOL_MACINTOSH_MACFILEEXPORTERS_H
