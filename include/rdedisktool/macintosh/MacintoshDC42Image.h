#ifndef RDEDISKTOOL_MACINTOSH_DC42IMAGE_H
#define RDEDISKTOOL_MACINTOSH_DC42IMAGE_H

#include "rdedisktool/macintosh/MacintoshDiskImage.h"

namespace rde {

/**
 * Apple Disk Copy 4.2 container (.image / .dc42).
 *
 * 0x54-byte header + raw sector data + optional tag bytes:
 *   0x00   1   image name length (Pascal, <= 63)
 *   0x01  63   image name bytes (MacRoman)
 *   0x40   4   data_size       (BE u32, > 0, %512 == 0)
 *   0x44   4   tag_size        (BE u32, 0 or data_size/512 * 12)
 *   0x48   4   data_checksum   (BE u32, sum of BE u16 + ROR32)
 *   0x4c   4   tag_checksum    (BE u32, same algorithm)
 *   0x50   1   disk_encoding
 *   0x51   1   format_byte
 *   0x52   2   magic           (BE u16 == 0x0100)
 *   0x54  data_size            image data (raw 512B sectors)
 *   ...   tag_size              optional tag data
 *
 * NOTE (M1 skeleton): registerFormat plumbing only. Header parsing +
 * checksum validation land in M2.
 */
class MacintoshDC42Image : public MacintoshDiskImage {
public:
    static constexpr size_t HEADER_SIZE = 0x54;

    MacintoshDC42Image();
    ~MacintoshDC42Image() override = default;

    void load(const std::filesystem::path& path) override;
    void save(const std::filesystem::path& path = {}) override;
    void create(const DiskGeometry& geometry) override;

    DiskFormat getFormat() const override { return DiskFormat::MacDC42; }

    SectorBuffer readSector(size_t track, size_t side, size_t sector) override;
    void writeSector(size_t track, size_t side, size_t sector,
                     const SectorBuffer& data) override;

    TrackBuffer readTrack(size_t track, size_t side) override;
    void writeTrack(size_t track, size_t side, const TrackBuffer& data) override;

    bool canConvertTo(DiskFormat format) const override;
    std::unique_ptr<DiskImage> convertTo(DiskFormat format) const override;

    bool validate() const override;
    std::string getDiagnostics() const override;
};

} // namespace rde

#endif // RDEDISKTOOL_MACINTOSH_DC42IMAGE_H
