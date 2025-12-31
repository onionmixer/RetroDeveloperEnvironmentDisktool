#ifndef RDEDISKTOOL_APPLE_POIMAGE_H
#define RDEDISKTOOL_APPLE_POIMAGE_H

#include "rdedisktool/apple/AppleDiskImage.h"

namespace rde {

/**
 * Apple II ProDOS Order disk image (.po)
 *
 * Similar to DOS Order but with ProDOS sector interleaving.
 * Better suited for ProDOS file system access.
 *
 * File structure:
 * - 35 tracks × 16 sectors × 256 bytes = 143,360 bytes
 * - Sectors stored in ProDOS logical order
 * - 512-byte block access maps directly to pairs of sectors
 */
class ApplePOImage : public AppleDiskImage {
public:
    ApplePOImage();
    ~ApplePOImage() override = default;

    //=========================================================================
    // DiskImage Interface
    //=========================================================================

    void load(const std::filesystem::path& path) override;
    void save(const std::filesystem::path& path = {}) override;
    void create(const DiskGeometry& geometry) override;

    DiskFormat getFormat() const override { return DiskFormat::ApplePO; }

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

    SectorOrder getSectorOrder() const override { return SectorOrder::ProDOS; }

    //=========================================================================
    // ProDOS-Specific Block Access (optimized)
    //=========================================================================

    /**
     * Read a ProDOS block - optimized for PO format
     * In PO format, blocks are stored contiguously
     */
    SectorBuffer readBlock(size_t block) override;

    /**
     * Write a ProDOS block - optimized for PO format
     */
    void writeBlock(size_t block, const SectorBuffer& data) override;

protected:
    size_t calculateOffset(size_t track, size_t sector) const override;
};

} // namespace rde

#endif // RDEDISKTOOL_APPLE_POIMAGE_H
