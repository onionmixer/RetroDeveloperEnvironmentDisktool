#ifndef RDEDISKTOOL_UTILS_MACROMAN_H
#define RDEDISKTOOL_UTILS_MACROMAN_H

#include <cstdint>
#include <string>

namespace rde {

/**
 * Convert a Mac-Roman encoded byte string (typical for HFS / MFS file names
 * and volume labels) to UTF-8.
 *
 * The 0x00..0x7F range is identical to ASCII. The 0x80..0xFF range follows
 * the Apple "Macintosh Roman" mapping
 * (https://www.unicode.org/Public/MAPPINGS/VENDORS/APPLE/ROMAN.TXT).
 *
 * Bytes are translated independently — there is no shift state.
 */
std::string macRomanToUtf8(const uint8_t* data, size_t length);

inline std::string macRomanToUtf8(const std::string& s) {
    return macRomanToUtf8(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

} // namespace rde

#endif // RDEDISKTOOL_UTILS_MACROMAN_H
