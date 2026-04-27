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

    // readSector / writeSector / readTrack / writeTrack inherited from
    // MacintoshDiskImage. m_data stores the raw sector stream after the
    // 0x54-byte container header has been stripped.

    bool canConvertTo(DiskFormat format) const override;
    std::unique_ptr<DiskImage> convertTo(DiskFormat format) const override;

    bool validate() const override;
    std::string getDiagnostics() const override;

    // Container metadata extracted from the 0x54-byte header.
    // Populated after a successful load(); zero-initialized otherwise.
    const std::string& imageName() const { return m_imageName; }
    uint32_t dataSize() const     { return m_dataSize; }
    uint32_t tagSize() const      { return m_tagSize; }
    uint32_t dataChecksum() const { return m_dataChecksum; }
    uint32_t tagChecksum() const  { return m_tagChecksum; }
    uint8_t  diskEncoding() const { return m_diskEncoding; }
    uint8_t  formatByte() const   { return m_formatByte; }

private:
    std::string m_imageName;
    uint32_t m_dataSize = 0;
    uint32_t m_tagSize = 0;
    uint32_t m_dataChecksum = 0;     // expected (from header)
    uint32_t m_tagChecksum = 0;      // expected (from header)
    uint8_t  m_diskEncoding = 0;
    uint8_t  m_formatByte = 0;
};

} // namespace rde

#endif // RDEDISKTOOL_MACINTOSH_DC42IMAGE_H
