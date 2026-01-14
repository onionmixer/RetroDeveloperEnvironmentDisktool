#ifndef RDEDISKTOOL_X68000_DISKIMAGE_H
#define RDEDISKTOOL_X68000_DISKIMAGE_H

#include "rdedisktool/DiskImage.h"
#include "rdedisktool/Types.h"

namespace rde {

/**
 * Base class for X68000 disk image formats
 *
 * X68000 uses high-density floppy disk formats:
 * - XDF: 1024 bytes/sector, 8 sectors/track, 154 tracks (1.2MB)
 * - DIM: Multiple format types (2HD, 2HS, 2HC, 2HDE, 2HQ)
 *
 * File systems: Human68k (primary X68000 OS file system)
 */
class X68000DiskImage : public DiskImage {
public:
    // X68000 standard disk parameters
    static constexpr size_t SECTOR_SIZE_1024 = 1024;
    static constexpr size_t SECTOR_SIZE_512 = 512;

    // XDF format constants
    static constexpr size_t XDF_FILE_SIZE = 1261568;
    static constexpr size_t XDF_SECTOR_SIZE = 1024;
    static constexpr size_t XDF_SECTORS_PER_TRACK = 8;
    static constexpr size_t XDF_TOTAL_TRACKS = 154;
    static constexpr size_t XDF_CYLINDERS = 77;
    static constexpr size_t XDF_HEADS = 2;

    // DIM format constants
    static constexpr size_t DIM_HEADER_SIZE = 256;
    static constexpr size_t DIM_MAX_TRACKS = 170;

    ~X68000DiskImage() override = default;

    //=========================================================================
    // DiskImage Interface Implementation
    //=========================================================================

    Platform getPlatform() const override { return Platform::X68000; }
    FileSystemType getFileSystemType() const override;
    DiskGeometry getGeometry() const override { return m_geometry; }
    bool isWriteProtected() const override { return m_writeProtected; }
    void setWriteProtected(bool protect) override { m_writeProtected = protect; }
    bool isModified() const override { return m_modified; }
    std::filesystem::path getFilePath() const override { return m_filePath; }
    const std::vector<uint8_t>& getRawData() const override { return m_data; }
    void setRawData(const std::vector<uint8_t>& data) override;

    //=========================================================================
    // X68000-Specific Types
    //=========================================================================

    /**
     * FDC ID structure for sector addressing (C/H/R/N)
     */
    struct FDCID {
        uint8_t c;  // Cylinder (0-99)
        uint8_t h;  // Head (0-1)
        uint8_t r;  // Record/Sector number (1-indexed)
        uint8_t n;  // Sector size code (2=512, 3=1024)

        FDCID() : c(0), h(0), r(1), n(3) {}
        FDCID(uint8_t cyl, uint8_t head, uint8_t rec, uint8_t size)
            : c(cyl), h(head), r(rec), n(size) {}
    };

    //=========================================================================
    // X68000-Specific Methods
    //=========================================================================

    /**
     * Calculate sector size from N field
     * N=2: 512 bytes, N=3: 1024 bytes
     */
    static size_t sectorSizeFromN(uint8_t n) {
        return 128u << n;  // n=2: 512, n=3: 1024
    }

    /**
     * Calculate N field from sector size
     */
    static uint8_t nFromSectorSize(size_t size) {
        if (size == 512) return 2;
        if (size == 1024) return 3;
        return 3;  // Default to 1024
    }

    /**
     * Convert linear track number to cylinder/head
     */
    static void trackToCH(size_t track, uint8_t& cylinder, uint8_t& head) {
        cylinder = static_cast<uint8_t>(track >> 1);
        head = static_cast<uint8_t>(track & 1);
    }

    /**
     * Convert cylinder/head to linear track number
     */
    static size_t chToTrack(uint8_t cylinder, uint8_t head) {
        return (static_cast<size_t>(cylinder) << 1) | head;
    }

    /**
     * Check if this disk has Human68k file system
     */
    bool isHuman68k() const;

protected:
    X68000DiskImage();

    // Initialize geometry
    void initGeometry(size_t tracks, size_t sides, size_t sectorsPerTrack, size_t bytesPerSector);

    // Calculate offset into raw data for a given track/sector
    virtual size_t calculateOffset(size_t track, size_t sector) const;

    // Cached file system type
    mutable FileSystemType m_cachedFileSystem = FileSystemType::Unknown;
    mutable bool m_fileSystemDetected = false;
};

} // namespace rde

#endif // RDEDISKTOOL_X68000_DISKIMAGE_H
