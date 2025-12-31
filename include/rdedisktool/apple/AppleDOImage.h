#ifndef RDEDISKTOOL_APPLE_DOIMAGE_H
#define RDEDISKTOOL_APPLE_DOIMAGE_H

#include "rdedisktool/apple/AppleDiskImage.h"

namespace rde {

/**
 * Apple II DOS Order disk image (.do, .dsk)
 *
 * This is the most common Apple II disk image format.
 * Sectors are stored in DOS 3.3 logical order.
 *
 * File structure:
 * - 35 tracks × 16 sectors × 256 bytes = 143,360 bytes
 * - Sectors stored sequentially per track
 * - DOS 3.3 sector interleave applied for physical order
 */
class AppleDOImage : public AppleDiskImage {
public:
    AppleDOImage();
    ~AppleDOImage() override = default;

    //=========================================================================
    // DiskImage Interface
    //=========================================================================

    void load(const std::filesystem::path& path) override;
    void save(const std::filesystem::path& path = {}) override;
    void create(const DiskGeometry& geometry) override;

    DiskFormat getFormat() const override { return DiskFormat::AppleDO; }

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
    // AppleDiskImage Interface
    //=========================================================================

    SectorOrder getSectorOrder() const override { return SectorOrder::DOS; }

protected:
    size_t calculateOffset(size_t track, size_t sector) const override;
};

} // namespace rde

#endif // RDEDISKTOOL_APPLE_DOIMAGE_H
