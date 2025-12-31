#ifndef RDEDISKTOOL_MSX_DSKIMAGE_H
#define RDEDISKTOOL_MSX_DSKIMAGE_H

#include "rdedisktool/msx/MSXDiskImage.h"

namespace rde {

/**
 * MSX DSK disk image (.dsk)
 *
 * Standard sector-based disk image format for MSX.
 * Simply stores raw sector data sequentially.
 *
 * Common sizes:
 * - 360KB: 40 tracks × 1 side × 9 sectors × 512 bytes
 * - 720KB: 80 tracks × 2 sides × 9 sectors × 512 bytes
 */
class MSXDSKImage : public MSXDiskImage {
public:
    MSXDSKImage();
    ~MSXDSKImage() override = default;

    //=========================================================================
    // DiskImage Interface
    //=========================================================================

    void load(const std::filesystem::path& path) override;
    void save(const std::filesystem::path& path = {}) override;
    void create(const DiskGeometry& geometry) override;

    DiskFormat getFormat() const override { return DiskFormat::MSXDSK; }

    SectorBuffer readSector(size_t track, size_t side, size_t sector) override;
    void writeSector(size_t track, size_t side, size_t sector,
                    const SectorBuffer& data) override;

    TrackBuffer readTrack(size_t track, size_t side) override;
    void writeTrack(size_t track, size_t side, const TrackBuffer& data) override;

    bool canConvertTo(DiskFormat format) const override;
    std::unique_ptr<DiskImage> convertTo(DiskFormat format) const override;

    bool validate() const override;
    std::string getDiagnostics() const override;

    //=========================================================================
    // DSK-Specific Methods
    //=========================================================================

    /**
     * Format the disk with MSX-DOS file system
     * @param volumeLabel Optional volume label (max 11 chars)
     * @param mediaDescriptor Media type byte
     */
    void formatMSXDOS(const std::string& volumeLabel = "",
                      uint8_t mediaDescriptor = 0xF9);
};

} // namespace rde

#endif // RDEDISKTOOL_MSX_DSKIMAGE_H
