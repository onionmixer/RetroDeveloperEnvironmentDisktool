#include "rdedisktool/macintosh/MacFileExporters.h"

#include <algorithm>
#include <cstring>

namespace rde {

namespace {

inline void putBE16(std::vector<uint8_t>& out, size_t off, uint16_t v) {
    out[off]     = static_cast<uint8_t>((v >> 8) & 0xFF);
    out[off + 1] = static_cast<uint8_t>(v & 0xFF);
}
inline void putBE32(std::vector<uint8_t>& out, size_t off, uint32_t v) {
    out[off]     = static_cast<uint8_t>((v >> 24) & 0xFF);
    out[off + 1] = static_cast<uint8_t>((v >> 16) & 0xFF);
    out[off + 2] = static_cast<uint8_t>((v >> 8) & 0xFF);
    out[off + 3] = static_cast<uint8_t>(v & 0xFF);
}

} // namespace

std::vector<uint8_t> buildAppleDoubleSidecar(const AppleDoubleInput& in) {
    // Always emit the three entries (3, 9, 2) — even when payloads are empty.
    // SPEC §1648 example shows this layout matches the Python writer.
    constexpr uint16_t kEntryCount = 3;
    constexpr size_t   kHeaderLen  = 4 + 4 + 16 + 2;            // 26
    constexpr size_t   kEntryDescLen = 12;
    const size_t firstPayloadOff = kHeaderLen + kEntryCount * kEntryDescLen;

    const uint32_t nameLen = static_cast<uint32_t>(in.macRomanName.size());
    const uint32_t finderLen = static_cast<uint32_t>(in.finderInfo.size());
    const uint32_t rsrcLen = static_cast<uint32_t>(in.resourceFork.size());

    const uint32_t nameOff   = static_cast<uint32_t>(firstPayloadOff);
    const uint32_t finderOff = nameOff + nameLen;
    const uint32_t rsrcOff   = finderOff + finderLen;

    const size_t total = static_cast<size_t>(rsrcOff) + rsrcLen;
    std::vector<uint8_t> out(total, 0);

    putBE32(out, 0,  0x00051607u);   // magic
    putBE32(out, 4,  0x00020000u);   // version
    // bytes 8..23 are filler — already zero
    putBE16(out, 24, kEntryCount);

    // Entry descriptors (id 3, 9, 2 in this order — same as Python)
    size_t off = kHeaderLen;
    auto writeEntry = [&](uint32_t id, uint32_t pOff, uint32_t pLen) {
        putBE32(out, off,     id);
        putBE32(out, off + 4, pOff);
        putBE32(out, off + 8, pLen);
        off += kEntryDescLen;
    };
    writeEntry(3, nameOff,   nameLen);
    writeEntry(9, finderOff, finderLen);
    writeEntry(2, rsrcOff,   rsrcLen);

    if (nameLen > 0) {
        std::memcpy(out.data() + nameOff,
                    in.macRomanName.data(), nameLen);
    }
    if (finderLen > 0) {
        std::memcpy(out.data() + finderOff,
                    in.finderInfo.data(), finderLen);
    }
    if (rsrcLen > 0) {
        std::memcpy(out.data() + rsrcOff,
                    in.resourceFork.data(), rsrcLen);
    }
    return out;
}

std::vector<uint8_t> buildMacBinary(const MacBinaryInput& in) {
    constexpr size_t kHeader = 128;
    auto roundUp128 = [](size_t v) -> size_t {
        return (v + 127) & ~static_cast<size_t>(127);
    };
    const size_t dataPadded = roundUp128(in.dataFork.size());
    const size_t rsrcPadded = roundUp128(in.resourceFork.size());
    const size_t total = kHeader + dataPadded + rsrcPadded;

    std::vector<uint8_t> out(total, 0);

    // Header
    out[0] = 0;  // old version
    const size_t nameLen = std::min<size_t>(in.macRomanName.size(), 63);
    out[1] = static_cast<uint8_t>(nameLen);
    std::memcpy(out.data() + 2, in.macRomanName.data(), nameLen);
    std::memcpy(out.data() + 65, in.fileType, 4);
    std::memcpy(out.data() + 69, in.creator,  4);
    out[73] = in.finderFlagsHi;
    out[74] = 0;
    std::memcpy(out.data() + 75, in.finderInfoLocation, 6);
    out[81] = in.protectedFlag;
    out[82] = 0;
    putBE32(out, 83, in.dataLength);
    putBE32(out, 87, in.rsrcLength);
    putBE32(out, 91, in.createDate);
    putBE32(out, 95, in.modifyDate);

    // Forks
    if (!in.dataFork.empty()) {
        std::memcpy(out.data() + kHeader, in.dataFork.data(), in.dataFork.size());
    }
    if (!in.resourceFork.empty()) {
        std::memcpy(out.data() + kHeader + dataPadded,
                    in.resourceFork.data(), in.resourceFork.size());
    }
    return out;
}

} // namespace rde
