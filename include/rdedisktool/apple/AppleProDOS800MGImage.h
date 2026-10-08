#ifndef RDEDISKTOOL_APPLE_PRODOS800MGIMAGE_H
#define RDEDISKTOOL_APPLE_PRODOS800MGIMAGE_H

#include "rdedisktool/apple/AppleProDOS800Image.h"

namespace rde {

/**
 * Apple II 3.5" 800K ProDOS-order image in a 2MG container (.2mg, -f 800mg).
 *
 * 64-byte header (little endian; MAME ap_dsk35.cpp, AppleWin DiskImageHelper):
 *   0x00 "2IMG"  0x04 creator  0x08 header size (u16, 64)  0x0A version (u16)
 *   0x0C format (u32: 0 DOS order, 1 ProDOS order, 2 nibbles)
 *   0x10 flags (u32: bit 31 = locked)  0x14 ProDOS blocks
 *   0x18 data offset  0x1C data length  0x20/0x24 comment offset/length
 *   0x28/0x2C creator data offset/length
 * Only format 1 with 1600 blocks is accepted. The whole file is kept, and
 * saving replaces only the data range, so the header, comment, creator data
 * and any other bytes stay as they were. A locked image is write protected.
 */
class AppleProDOS800MGImage : public AppleProDOS800Image {
public:
    static constexpr size_t HEADER_SIZE = 64;
    static constexpr uint32_t FLAG_LOCKED = 0x80000000u;

    AppleProDOS800MGImage() = default;
    ~AppleProDOS800MGImage() override = default;

    void load(const std::filesystem::path& path) override;
    void save(const std::filesystem::path& path = {}) override;
    void create(const DiskGeometry& geometry) override;

    DiskFormat getFormat() const override { return DiskFormat::Apple800MG; }
    void setRawData(const std::vector<uint8_t>& data) override;
    std::string getDiagnostics() const override;

    bool isLocked() const { return (m_flags & FLAG_LOCKED) != 0; }
    // Header data a plain block image cannot hold (lost by convert)
    bool hasContainerData() const;

private:
    std::vector<uint8_t> m_file;  // whole file as loaded (or as created)
    size_t m_dataOffset = HEADER_SIZE;
    uint32_t m_flags = 0;
    uint32_t m_commentLength = 0;
    uint32_t m_creatorDataLength = 0;
    std::string m_creator;
};

} // namespace rde

#endif // RDEDISKTOOL_APPLE_PRODOS800MGIMAGE_H
