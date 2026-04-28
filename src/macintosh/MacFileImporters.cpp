#include "rdedisktool/macintosh/MacFileImporters.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>

namespace rde {

namespace {

inline uint32_t readBE32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) <<  8) |
            static_cast<uint32_t>(p[3]);
}
inline uint16_t readBE16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

// Read an entire file into a byte vector. Returns true on success.
bool readWholeFile(const std::filesystem::path& path,
                    std::vector<uint8_t>& out,
                    std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open: " + path.string();
        return false;
    }
    in.seekg(0, std::ios::end);
    const auto sz = in.tellg();
    in.seekg(0, std::ios::beg);
    if (sz < 0) {
        error = "cannot stat: " + path.string();
        return false;
    }
    out.resize(static_cast<size_t>(sz));
    if (sz > 0 && !in.read(reinterpret_cast<char*>(out.data()), sz)) {
        error = "read failed: " + path.string();
        return false;
    }
    return true;
}

} // namespace

// MacBinary I — stairways.com spec (1985 standard).
// Header layout (128 bytes total, BE):
//   0x00       oldVersion (MUST be 0 for v1)
//   0x01..0x40 Pascal Str63 filename (length byte at 0x01)
//   0x41..0x44 file type (4 bytes, MacRoman)
//   0x45..0x48 creator (4 bytes)
//   0x49       Finder flags high byte (locked / invisible / bundle / system)
//   0x4a       zero (filler)
//   0x4b..0x4c vertical position
//   0x4d..0x4e horizontal position
//   0x4f..0x50 window / folder ID
//   0x51       protected flag (bit 0)
//   0x52       zero (filler)
//   0x53..0x56 data fork length (BE u32)         ← NOTE: NOT 0x52
//   0x57..0x5a rsrc fork length (BE u32)
//   0x5b..0x5e created date (Mac epoch)
//   0x5f..0x62 modified date (Mac epoch)
//   0x63..0x64 Get Info comment length
//   0x65..0x7d varies / padding
//   0x7e..0x7f CRC (MacBinary II) or 0 (v1)
bool parseMacBinary(const std::vector<uint8_t>& bytes,
                     ParsedMacFile& out,
                     std::string& error) {
    out = {};
    if (bytes.size() < 128) {
        error = "MacBinary: file shorter than 128-byte header";
        return false;
    }
    const uint8_t* p = bytes.data();
    if (p[0] != 0x00) {
        error = "MacBinary: version byte at 0x00 is not 0 (v1 only)";
        return false;
    }
    const uint8_t nameLen = p[1];
    if (nameLen == 0 || nameLen > 63) {
        error = "MacBinary: invalid Pascal name length " +
                std::to_string(nameLen);
        return false;
    }
    out.macRomanName.assign(reinterpret_cast<const char*>(p + 2), nameLen);

    std::memcpy(out.fileType, p + 0x41, 4);
    std::memcpy(out.creator,  p + 0x45, 4);
    out.finderFlagsHi = p[0x49];
    // FInfo location (vertical/horizontal/window) at 0x4b..0x50 (6 bytes).
    std::memcpy(out.finderInfoLocation, p + 0x4b, 6);
    out.finderFlagsLo = p[0x51];
    out.protectedFlag = (p[0x51] & 0x01) != 0;

    const uint32_t dataLen = readBE32(p + 0x53);
    const uint32_t rsrcLen = readBE32(p + 0x57);
    out.createDate = readBE32(p + 0x5b);
    out.modifyDate = readBE32(p + 0x5f);

    // Body layout: 128B header, dataFork (padded to 128), rsrcFork (padded
    // to 128).
    auto pad128 = [](size_t n) -> size_t {
        return (n + 127U) & ~static_cast<size_t>(127);
    };
    const size_t dataStart = 128;
    const size_t dataPadded = pad128(dataLen);
    const size_t rsrcStart = dataStart + dataPadded;
    const size_t rsrcPadded = pad128(rsrcLen);
    const size_t need = rsrcStart + rsrcPadded;
    if (bytes.size() < need) {
        std::ostringstream oss;
        oss << "MacBinary: payload too short (need " << need
            << " bytes; got " << bytes.size() << ")";
        error = oss.str();
        return false;
    }
    if (dataLen > 0) {
        out.dataFork.assign(bytes.begin() + dataStart,
                             bytes.begin() + dataStart + dataLen);
    }
    if (rsrcLen > 0) {
        out.resourceFork.assign(bytes.begin() + rsrcStart,
                                 bytes.begin() + rsrcStart + rsrcLen);
    }
    // FXInfo not present in MacBinary v1 — leave finderInfoExtended zero.
    return true;
}

// AppleDouble v2 sidecar — Inside Macintosh + AppleDouble specification.
// Layout:
//   0x00..0x03 magic (BE) = 0x00051607
//   0x04..0x07 version    = 0x00020000
//   0x08..0x17 16-byte filler (must be zero)
//   0x18..0x19 entry count (BE u16)
//   0x1a..    entry table: each entry is 12 bytes (entryID, offset, length)
//             entryID 1 = data fork (often unused — data fork lives in
//                                    the host file paired with sidecar)
//             entryID 2 = resource fork
//             entryID 3 = real name (MacRoman)
//             entryID 9 = Finder info (16 byte FInfo + optional 16-byte
//                                       FXInfo, 32 total)
bool parseAppleDouble(const std::filesystem::path& dataPath,
                       const std::filesystem::path& sidecarPath,
                       ParsedMacFile& out,
                       std::string& error) {
    out = {};
    std::vector<uint8_t> sidecar;
    if (!readWholeFile(sidecarPath, sidecar, error)) {
        error = "AppleDouble sidecar: " + error;
        return false;
    }
    if (sidecar.size() < 26) {
        error = "AppleDouble: sidecar shorter than 26 bytes";
        return false;
    }
    if (readBE32(sidecar.data() + 0x00) != 0x00051607u) {
        error = "AppleDouble: missing 0x00051607 magic";
        return false;
    }
    if (readBE32(sidecar.data() + 0x04) != 0x00020000u) {
        error = "AppleDouble: not version 2 (0x00020000)";
        return false;
    }
    const uint16_t nentries = readBE16(sidecar.data() + 0x18);
    if (nentries == 0 || sidecar.size() < 26 + nentries * 12u) {
        error = "AppleDouble: malformed entry table";
        return false;
    }
    for (uint16_t i = 0; i < nentries; ++i) {
        const uint8_t* e = sidecar.data() + 26 + i * 12u;
        const uint32_t entryId = readBE32(e + 0x00);
        const uint32_t off     = readBE32(e + 0x04);
        const uint32_t len     = readBE32(e + 0x08);
        if (off + len > sidecar.size()) {
            error = "AppleDouble: entry " + std::to_string(entryId) +
                    " runs past sidecar EOF";
            return false;
        }
        switch (entryId) {
            case 2:  // resource fork
                if (len > 0) {
                    out.resourceFork.assign(sidecar.begin() + off,
                                              sidecar.begin() + off + len);
                }
                break;
            case 3:  // real name (MacRoman)
                if (len > 0) {
                    out.macRomanName.assign(
                        reinterpret_cast<const char*>(sidecar.data() + off),
                        len);
                }
                break;
            case 9: {  // Finder info: 16 byte FInfo + 16 byte FXInfo
                if (len >= 16) {
                    std::memcpy(out.fileType, sidecar.data() + off,     4);
                    std::memcpy(out.creator,  sidecar.data() + off + 4, 4);
                    out.finderFlagsHi = sidecar[off + 8];
                    out.finderFlagsLo = sidecar[off + 9];
                    out.protectedFlag = (out.finderFlagsLo & 0x01) != 0;
                    std::memcpy(out.finderInfoLocation,
                                sidecar.data() + off + 10, 6);
                }
                if (len >= 32) {
                    std::memcpy(out.finderInfoExtended,
                                sidecar.data() + off + 16, 16);
                }
                break;
            }
            default:
                // Other entry types (file dates, comment, etc.) ignored —
                // they are not load-bearing for HFS catalog write.
                break;
        }
    }

    // Real-name fallback: derive from sidecar filename if entry-id 3 was
    // absent. Strip the conventional "._" or "%" prefix.
    if (out.macRomanName.empty()) {
        std::string stem = sidecarPath.filename().string();
        if (stem.size() >= 2 && stem[0] == '.' && stem[1] == '_') {
            stem.erase(0, 2);
        } else if (!stem.empty() && stem[0] == '%') {
            stem.erase(0, 1);
        }
        // Trim any trailing ".ad" extension Retro68 sometimes emits.
        if (stem.size() > 3 &&
            stem.compare(stem.size() - 3, 3, ".ad") == 0) {
            stem.resize(stem.size() - 3);
        }
        out.macRomanName = stem;
    }

    // Read the host data fork. May be missing or empty (rsrc-only files).
    std::error_code ec;
    if (std::filesystem::exists(dataPath, ec)) {
        std::string readErr;
        if (!readWholeFile(dataPath, out.dataFork, readErr)) {
            error = "AppleDouble data fork: " + readErr;
            return false;
        }
    }
    return true;
}

} // namespace rde
