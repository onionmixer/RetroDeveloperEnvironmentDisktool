#include "rdedisktool/macintosh/DC42Checksum.h"

namespace rde {

uint32_t dc42Checksum(const uint8_t* data, size_t length) {
    uint32_t checksum = 0;
    const size_t pairs = length / 2;
    for (size_t i = 0; i < pairs; ++i) {
        const size_t byteOffset = i * 2;
        const uint16_t word =
            (static_cast<uint16_t>(data[byteOffset]) << 8) |
             static_cast<uint16_t>(data[byteOffset + 1]);
        checksum = (checksum + word) & 0xFFFFFFFFu;
        checksum = ((checksum >> 1) | (checksum << 31)) & 0xFFFFFFFFu;
    }
    return checksum;
}

} // namespace rde
