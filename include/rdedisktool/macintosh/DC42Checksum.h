#ifndef RDEDISKTOOL_MACINTOSH_DC42CHECKSUM_H
#define RDEDISKTOOL_MACINTOSH_DC42CHECKSUM_H

#include <cstddef>
#include <cstdint>

namespace rde {

/**
 * Apple Disk Copy 4.2 data/tag checksum.
 *
 * Per SPEC_MACDISKIMAGE.md §285:
 *
 *   checksum = 0
 *   for each big_endian_u16 word in image_data:
 *     checksum = (checksum + word) modulo 2^32
 *     checksum = rotate_right_32(checksum, 1)
 *
 * The same algorithm applies to both data and tag bytes.
 *
 * @param data    Pointer to byte buffer.
 * @param length  Length in bytes. If odd, the trailing byte is ignored
 *                (the SPEC enforces an even data_size, so this is only a
 *                defensive measure).
 * @return        Final 32-bit checksum.
 */
uint32_t dc42Checksum(const uint8_t* data, size_t length);

} // namespace rde

#endif // RDEDISKTOOL_MACINTOSH_DC42CHECKSUM_H
