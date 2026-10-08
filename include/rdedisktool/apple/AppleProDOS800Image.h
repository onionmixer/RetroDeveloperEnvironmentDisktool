#ifndef RDEDISKTOOL_APPLE_PRODOS800IMAGE_H
#define RDEDISKTOOL_APPLE_PRODOS800IMAGE_H

#include "rdedisktool/DiskImage.h"
#include "rdedisktool/Types.h"

namespace rde {

/**
 * Apple II 3.5" 800K ProDOS-order block image (.po, -f 800po): 1600 blocks
 * of 512 bytes stored in block order (819,200 bytes). ProDOS file system
 * only. Not a Macintosh disk and not related to the 140K 5.25" formats, so
 * it does not derive from AppleDiskImage.
 *
 * Logical geometry 80 tracks x 2 sides x 10 sectors x 512 bytes, sectors
 * numbered from 0: (track, side, sector) is block (track*2+side)*10+sector.
 * This is only an addressing scheme for the sector commands (dump,
 * convert); it is not the physical 3.5" GCR layout (8-12 sectors per track).
 */
class AppleProDOS800Image : public DiskImage {
public:
    static constexpr size_t BLOCK_SIZE = 512;
    static constexpr size_t BLOCKS = 1600;
    static constexpr size_t IMAGE_SIZE = BLOCKS * BLOCK_SIZE;  // 819,200
    static constexpr size_t TRACKS = 80;
    static constexpr size_t SIDES = 2;
    static constexpr size_t SECTORS = 10;

    AppleProDOS800Image();
    ~AppleProDOS800Image() override = default;

    void load(const std::filesystem::path& path) override;
    void save(const std::filesystem::path& path = {}) override;
    void create(const DiskGeometry& geometry) override;

    Platform getPlatform() const override { return Platform::AppleII; }
    DiskFormat getFormat() const override { return DiskFormat::Apple800PO; }
    FileSystemType getFileSystemType() const override;
    DiskGeometry getGeometry() const override { return m_geometry; }
    bool isWriteProtected() const override { return m_writeProtected; }
    void setWriteProtected(bool protect) override { m_writeProtected = protect; }
    bool isModified() const override { return m_modified; }
    std::filesystem::path getFilePath() const override { return m_filePath; }

    SectorBuffer readSector(size_t track, size_t side, size_t sector) override;
    void writeSector(size_t track, size_t side, size_t sector,
                     const SectorBuffer& data) override;
    TrackBuffer readTrack(size_t track, size_t side) override;
    void writeTrack(size_t track, size_t side, const TrackBuffer& data) override;

    SectorBuffer readBlock(size_t block) override;
    void writeBlock(size_t block, const SectorBuffer& data) override;
    size_t getTotalBlocks() const override { return BLOCKS; }

    const std::vector<uint8_t>& getRawData() const override { return m_data; }
    void setRawData(const std::vector<uint8_t>& data) override;

    bool canConvertTo(DiskFormat format) const override;
    std::unique_ptr<DiskImage> convertTo(DiskFormat format) const override;

    bool validate() const override;
    std::string getDiagnostics() const override;

private:
    size_t blockOf(size_t track, size_t side, size_t sector) const;
};

} // namespace rde

#endif // RDEDISKTOOL_APPLE_PRODOS800IMAGE_H
