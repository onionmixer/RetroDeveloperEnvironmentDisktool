#ifndef RDEDISKTOOL_MACINTOSH_IMGIMAGE_H
#define RDEDISKTOOL_MACINTOSH_IMGIMAGE_H

#include "rdedisktool/macintosh/MacintoshDiskImage.h"

namespace rde {

/**
 * Raw 512-byte-sector Macintosh disk image (.img / .dsk).
 *
 * Contains no container header — the file is a byte-for-byte sector stream
 * starting at file offset 0. Validated by file_size % 512 == 0 and
 * by an HFS or MFS signature at offset 0x400.
 *
 * NOTE (M1 skeleton): registerFormat plumbing only. Load / detection
 * land in M2.
 */
class MacintoshIMGImage : public MacintoshDiskImage {
public:
    MacintoshIMGImage();
    ~MacintoshIMGImage() override = default;

    void load(const std::filesystem::path& path) override;
    void save(const std::filesystem::path& path = {}) override;
    void create(const DiskGeometry& geometry) override;

    DiskFormat getFormat() const override { return DiskFormat::MacIMG; }

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

#endif // RDEDISKTOOL_MACINTOSH_IMGIMAGE_H
