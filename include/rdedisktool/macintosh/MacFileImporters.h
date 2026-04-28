#ifndef RDEDISKTOOL_MACINTOSH_MACFILEIMPORTERS_H
#define RDEDISKTOOL_MACINTOSH_MACFILEIMPORTERS_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rde {

/**
 * Common parsed-input record. Both `parseMacBinary` and
 * `parseAppleDouble` populate this from their respective on-disk
 * formats. Symmetric to the exporter's MacBinaryInput / AppleDoubleInput
 * structs.
 */
struct ParsedMacFile {
    std::string macRomanName;     // raw MacRoman, no trailing zeros
    uint8_t fileType[4]   = {0,0,0,0};
    uint8_t creator[4]    = {0,0,0,0};
    uint8_t finderFlagsHi = 0;             // FInfo[8]
    uint8_t finderFlagsLo = 0;             // FInfo[9]
    uint8_t finderInfoLocation[6] = {0};   // FInfo[10..15] (window/icon position)
    uint8_t finderInfoExtended[16] = {0};  // FXInfo (16 bytes; zero from MacBinary v1)
    uint32_t createDate = 0;               // Mac epoch
    uint32_t modifyDate = 0;               // Mac epoch
    bool     protectedFlag = false;
    std::vector<uint8_t> dataFork;
    std::vector<uint8_t> resourceFork;
};

/**
 * Parse a MacBinary v1 file (128-byte header, then data fork padded to a
 * 128-byte boundary, then resource fork padded to 128 bytes). Refuses
 * version-byte != 0 (MacBinary II / III) and zero-named files.
 *
 * Returns true on success; on failure populates `error` and leaves `out`
 * unspecified.
 */
bool parseMacBinary(const std::vector<uint8_t>& bytes,
                     ParsedMacFile& out,
                     std::string& error);

/**
 * Parse an AppleDouble pair: `dataPath` holds the data fork as a regular
 * file (may be empty); `sidecarPath` holds the AppleDouble v2 sidecar
 * (magic 0x00051607, version 0x00020000) carrying entry-id 2 (resource
 * fork), entry-id 9 (Finder info — 32 bytes = FInfo + FXInfo) and
 * optional entry-id 3 (real name). Both files must exist.
 *
 * Returns true on success.
 */
bool parseAppleDouble(const std::filesystem::path& dataPath,
                       const std::filesystem::path& sidecarPath,
                       ParsedMacFile& out,
                       std::string& error);

} // namespace rde

#endif // RDEDISKTOOL_MACINTOSH_MACFILEIMPORTERS_H
