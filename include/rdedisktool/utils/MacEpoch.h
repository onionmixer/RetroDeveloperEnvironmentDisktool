#ifndef RDEDISKTOOL_UTILS_MACEPOCH_H
#define RDEDISKTOOL_UTILS_MACEPOCH_H

#include <cstdint>
#include <ctime>

namespace rde {

// Seconds between the Macintosh epoch (1904-01-01 00:00:00 UTC) and the
// Unix epoch (1970-01-01 00:00:00 UTC). Constant verified per SPEC §112
// and macdiskimage.py:14842.
constexpr int64_t MAC_EPOCH_TO_UNIX_OFFSET = 2082844800LL;

/**
 * Convert a Macintosh-epoch unsigned 32-bit timestamp (seconds since
 * 1904-01-01 UTC) to a Unix-epoch time_t.
 *
 * Returns 0 when the timestamp predates the Unix epoch — the caller can
 * decide whether to treat that as "no value" or display the raw u32.
 */
inline std::time_t fromMacEpoch(uint32_t macSeconds) {
    if (macSeconds == 0) return 0;
    const int64_t unix = static_cast<int64_t>(macSeconds) - MAC_EPOCH_TO_UNIX_OFFSET;
    if (unix <= 0) return 0;
    return static_cast<std::time_t>(unix);
}

/**
 * Convert a Unix-epoch time_t to a Macintosh u32 timestamp.
 * Saturates to 0 / 0xFFFFFFFF on underflow / overflow respectively.
 */
inline uint32_t toMacEpoch(std::time_t unixSeconds) {
    const int64_t v = static_cast<int64_t>(unixSeconds) + MAC_EPOCH_TO_UNIX_OFFSET;
    if (v < 0) return 0;
    if (v > static_cast<int64_t>(0xFFFFFFFFu)) return 0xFFFFFFFFu;
    return static_cast<uint32_t>(v);
}

} // namespace rde

#endif // RDEDISKTOOL_UTILS_MACEPOCH_H
