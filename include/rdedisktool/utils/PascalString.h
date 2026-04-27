#ifndef RDEDISKTOOL_UTILS_PASCALSTRING_H
#define RDEDISKTOOL_UTILS_PASCALSTRING_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace rde {

/**
 * Read a Pascal string (length byte + bytes) from a buffer.
 *
 * @param data    pointer to the length byte
 * @param avail   bytes still available in the buffer starting at `data`
 * @param maxLen  maximum length the field allocates for content (e.g. 27 for
 *                a Mac volume name in a 28-byte field). The actual length is
 *                clamped to min(data[0], maxLen).
 * @return        std::string of the raw payload bytes (no encoding conversion).
 *                Empty if the buffer is too small or the length is zero.
 */
inline std::string readPascalBounded(const uint8_t* data, size_t avail, size_t maxLen) {
    if (avail < 1) return {};
    size_t len = static_cast<size_t>(data[0]);
    if (len > maxLen) len = maxLen;
    if (len + 1 > avail) len = (avail > 1) ? (avail - 1) : 0;
    return std::string(reinterpret_cast<const char*>(data + 1), len);
}

/**
 * Read an unbounded Pascal string. The trailing length byte limits how many
 * bytes follow; this variant is used for HFS catalog keys where the field is
 * variable-length.
 *
 * @param data    pointer to the length byte
 * @param avail   bytes still available
 * @param outConsumed  bytes consumed including the length byte (1 + length)
 */
inline std::string readPascalUnbounded(const uint8_t* data, size_t avail,
                                        size_t& outConsumed) {
    if (avail < 1) { outConsumed = 0; return {}; }
    size_t len = static_cast<size_t>(data[0]);
    if (len + 1 > avail) len = (avail > 1) ? (avail - 1) : 0;
    outConsumed = 1 + len;
    return std::string(reinterpret_cast<const char*>(data + 1), len);
}

} // namespace rde

#endif // RDEDISKTOOL_UTILS_PASCALSTRING_H
