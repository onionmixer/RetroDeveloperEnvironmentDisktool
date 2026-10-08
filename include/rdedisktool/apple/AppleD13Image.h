#ifndef RDEDISKTOOL_APPLE_D13IMAGE_H
#define RDEDISKTOOL_APPLE_D13IMAGE_H

#include "rdedisktool/apple/AppleDOImage.h"

namespace rde {

/**
 * Apple DOS 3.2 sector image (.d13): 35 tracks x 13 sectors x 256 bytes
 * (116,480 bytes), sectors in physical order (DOS 3.2 has no software
 * interleave). Read with the DOS 3.2 file system (read-only); written only
 * as the output of `convert` from 13-sector NIB/WOZ images.
 */
class AppleD13Image : public AppleDOImage {
public:
    AppleD13Image() = default;
    ~AppleD13Image() override = default;

    void load(const std::filesystem::path& path) override;
    void create(const DiskGeometry& geometry) override;

    DiskFormat getFormat() const override { return DiskFormat::AppleD13; }
    SectorOrder getSectorOrder() const override { return SectorOrder::Physical; }

    bool canConvertTo(DiskFormat format) const override;
    std::string getDiagnostics() const override;
};

} // namespace rde

#endif // RDEDISKTOOL_APPLE_D13IMAGE_H
