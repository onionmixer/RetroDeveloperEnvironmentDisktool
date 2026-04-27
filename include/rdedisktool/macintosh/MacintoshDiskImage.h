#ifndef RDEDISKTOOL_MACINTOSH_DISKIMAGE_H
#define RDEDISKTOOL_MACINTOSH_DISKIMAGE_H

#include "rdedisktool/DiskImage.h"
#include "rdedisktool/Types.h"

namespace rde {

/**
 * Base class for classic Macintosh disk image formats.
 *
 * Targets System 6 / 7-era 3.5" floppy images that have already been
 * unwrapped to a logical 512-byte sector stream. Physical track-rate
 * GCR / MFM modeling is intentionally out of scope.
 *
 * Standard sizes:
 *   400K  GCR  (single-sided)
 *   800K  GCR  (double-sided)
 *   720K  MFM
 *   1440K MFM
 *
 * File systems detected at offset 0x400:
 *   "BD"   (0x42 0x44)  -> HFS
 *   0xD2D7              -> MFS
 *
 * NOTE (M1 skeleton): only the platform identifier is wired up so far.
 * IMG / DC42 container parsing and HFS / MFS handlers land in M2~M4.
 */
class MacintoshDiskImage : public DiskImage {
public:
    // Logical sector layout for all classic Mac floppies in this scope.
    static constexpr size_t SECTOR_SIZE = 512;

    // Known logical sizes (bytes). Used as warning / strict-mode hints.
    static constexpr size_t SIZE_400K  = 400  * 1024;
    static constexpr size_t SIZE_720K  = 720  * 1024;
    static constexpr size_t SIZE_800K  = 800  * 1024;
    static constexpr size_t SIZE_1440K = 1440 * 1024;

    // MDB signatures at file offset 0x400 (big-endian u16).
    static constexpr uint16_t HFS_MDB_SIGNATURE = 0x4244;  // "BD"
    static constexpr uint16_t MFS_MDB_SIGNATURE = 0xD2D7;

    ~MacintoshDiskImage() override = default;

    Platform getPlatform() const override { return Platform::Macintosh; }
    FileSystemType getFileSystemType() const override;
    DiskGeometry getGeometry() const override { return m_geometry; }
    bool isWriteProtected() const override { return m_writeProtected; }
    void setWriteProtected(bool protect) override { m_writeProtected = protect; }
    bool isModified() const override { return m_modified; }
    std::filesystem::path getFilePath() const override { return m_filePath; }
    const std::vector<uint8_t>& getRawData() const override { return m_data; }
    void setRawData(const std::vector<uint8_t>& data) override;

    // Common 512B-sector I/O. Concrete subclasses must guarantee that m_data
    // contains the raw sector stream with the container header (if any)
    // already stripped, so that linear addressing
    //   offset = (track * sides + side) * sectorsPerTrack * 512 + sector * 512
    // resolves directly into m_data.
    SectorBuffer readSector(size_t track, size_t side, size_t sector) override;
    void writeSector(size_t track, size_t side, size_t sector,
                     const SectorBuffer& data) override;
    TrackBuffer readTrack(size_t track, size_t side) override;
    void writeTrack(size_t track, size_t side, const TrackBuffer& data) override;

protected:
    MacintoshDiskImage();

    // Configure geometry assuming a 512B logical sector layout.
    void initGeometryFromSize(size_t totalBytes);

    // Cached FS type, populated lazily from the raw sector stream.
    mutable FileSystemType m_cachedFileSystem = FileSystemType::Unknown;
    mutable bool m_fileSystemDetected = false;
};

} // namespace rde

#endif // RDEDISKTOOL_MACINTOSH_DISKIMAGE_H
