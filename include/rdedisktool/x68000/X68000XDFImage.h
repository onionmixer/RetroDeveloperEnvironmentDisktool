#ifndef RDEDISKTOOL_X68000_XDFIMAGE_H
#define RDEDISKTOOL_X68000_XDFIMAGE_H

#include "rdedisktool/x68000/X68000DiskImage.h"

namespace rde {

/**
 * X68000 XDF (X68000 Disk Format) disk image
 *
 * XDF is a headerless raw sector dump format:
 * - File size: 1,261,568 bytes (fixed)
 * - Sector size: 1024 bytes
 * - Sectors per track: 8
 * - Total tracks: 154 (77 cylinders x 2 heads)
 * - Total capacity: ~1.2 MB
 *
 * The format is simple: sectors are stored sequentially with no metadata.
 */
class X68000XDFImage : public X68000DiskImage {
public:
    X68000XDFImage();
    ~X68000XDFImage() override = default;

    //=========================================================================
    // DiskImage Interface Implementation
    //=========================================================================

    void load(const std::filesystem::path& path) override;
    void save(const std::filesystem::path& path = {}) override;
    void create(const DiskGeometry& geometry) override;

    DiskFormat getFormat() const override { return DiskFormat::X68000XDF; }

    SectorBuffer readSector(size_t track, size_t side, size_t sector) override;
    void writeSector(size_t track, size_t side, size_t sector,
                    const SectorBuffer& data) override;

    TrackBuffer readTrack(size_t track, size_t side) override;
    void writeTrack(size_t track, size_t side, const TrackBuffer& data) override;

    bool canConvertTo(DiskFormat format) const override;
    std::unique_ptr<DiskImage> convertTo(DiskFormat format) const override;

    bool validate() const override;
    std::string getDiagnostics() const override;

private:
    /**
     * Calculate byte offset for a given track and sector
     * Track is linear (0-153), sector is 1-indexed (1-8)
     */
    size_t calculateOffset(size_t track, size_t sector) const override;

    /**
     * Validate track/sector parameters
     */
    void validateParameters(size_t track, size_t sector) const;
};

} // namespace rde

#endif // RDEDISKTOOL_X68000_XDFIMAGE_H
