#ifndef RDEDISKTOOL_APPLE_DISKIMAGE_H
#define RDEDISKTOOL_APPLE_DISKIMAGE_H

#include "rdedisktool/DiskImage.h"
#include "rdedisktool/Types.h"

namespace rde {

/**
 * Base class for Apple II disk image formats
 *
 * Provides common functionality for all Apple II disk formats:
 * - Standard geometry (35 tracks, 16 sectors per track, 256 bytes per sector)
 * - Sector order mapping (DOS order vs ProDOS order)
 * - File system detection
 */
class AppleDiskImage : public DiskImage {
public:
    // Standard Apple II disk parameters
    static constexpr size_t TRACKS_35 = 35;
    static constexpr size_t SECTORS_16 = 16;
    static constexpr size_t SECTORS_13 = 13;  // DOS 3.2
    static constexpr size_t BYTES_PER_SECTOR = 256;
    static constexpr size_t TRACK_SIZE = SECTORS_16 * BYTES_PER_SECTOR;  // 4096 bytes
    static constexpr size_t DISK_SIZE_140K = TRACKS_35 * TRACK_SIZE;     // 143360 bytes

    ~AppleDiskImage() override = default;

    //=========================================================================
    // DiskImage Interface Implementation
    //=========================================================================

    Platform getPlatform() const override { return Platform::AppleII; }
    FileSystemType getFileSystemType() const override;
    DiskGeometry getGeometry() const override { return m_geometry; }
    bool isWriteProtected() const override { return m_writeProtected; }
    void setWriteProtected(bool protect) override { m_writeProtected = protect; }
    bool isModified() const override { return m_modified; }
    std::filesystem::path getFilePath() const override { return m_filePath; }
    const std::vector<uint8_t>& getRawData() const override { return m_data; }
    void setRawData(const std::vector<uint8_t>& data) override;

    //=========================================================================
    // Sector Order Handling
    //=========================================================================

    /**
     * Get the sector order for this image
     */
    virtual SectorOrder getSectorOrder() const = 0;

    /**
     * Convert logical sector number to physical sector number
     * @param logical Logical sector number (0-15)
     * @return Physical sector number
     */
    virtual size_t logicalToPhysical(size_t logical) const;

    /**
     * Convert physical sector number to logical sector number
     * @param physical Physical sector number (0-15)
     * @return Logical sector number
     */
    virtual size_t physicalToLogical(size_t physical) const;

    //=========================================================================
    // ProDOS Block Access
    //=========================================================================

    /**
     * Read a ProDOS block (512 bytes = 2 sectors)
     * @param block Block number (0-279)
     * @return Block data
     */
    SectorBuffer readBlock(size_t block) override;

    /**
     * Write a ProDOS block
     * @param block Block number
     * @param data Block data (512 bytes)
     */
    void writeBlock(size_t block, const SectorBuffer& data) override;

    /**
     * Get total number of ProDOS blocks
     */
    size_t getTotalBlocks() const override { return 280; }

    //=========================================================================
    // File System Detection
    //=========================================================================

    /**
     * Detect the file system type from disk content
     */
    FileSystemType detectFileSystem() const;

    /**
     * Check if this is a DOS 3.3 formatted disk
     */
    bool isDOS33() const;

    /**
     * Check if this is a ProDOS formatted disk
     */
    bool isProDOS() const;

protected:
    AppleDiskImage();

    // Initialize standard geometry
    void initGeometry(size_t tracks = TRACKS_35, size_t sectors = SECTORS_16);

    // Calculate offset into raw data for a given track/sector
    virtual size_t calculateOffset(size_t track, size_t sector) const = 0;

    // Cached file system type
    mutable FileSystemType m_cachedFileSystem = FileSystemType::Unknown;
    mutable bool m_fileSystemDetected = false;
};

/**
 * Sector interleaving tables for Apple II
 */
namespace AppleInterleave {

// DOS 3.3 logical to physical sector mapping
// Physical sector = dos33Interleave[logical sector]
constexpr uint8_t DOS33_INTERLEAVE[16] = {
    0x0, 0xD, 0xB, 0x9, 0x7, 0x5, 0x3, 0x1,
    0xE, 0xC, 0xA, 0x8, 0x6, 0x4, 0x2, 0xF
};

// ProDOS logical to physical sector mapping
constexpr uint8_t PRODOS_INTERLEAVE[16] = {
    0x0, 0x2, 0x4, 0x6, 0x8, 0xA, 0xC, 0xE,
    0x1, 0x3, 0x5, 0x7, 0x9, 0xB, 0xD, 0xF
};

// Physical to DOS 3.3 logical sector mapping (inverse)
constexpr uint8_t DOS33_DEINTERLEAVE[16] = {
    0x0, 0x7, 0xE, 0x6, 0xD, 0x5, 0xC, 0x4,
    0xB, 0x3, 0xA, 0x2, 0x9, 0x1, 0x8, 0xF
};

// Physical to ProDOS logical sector mapping (inverse)
constexpr uint8_t PRODOS_DEINTERLEAVE[16] = {
    0x0, 0x8, 0x1, 0x9, 0x2, 0xA, 0x3, 0xB,
    0x4, 0xC, 0x5, 0xD, 0x6, 0xE, 0x7, 0xF
};

} // namespace AppleInterleave

} // namespace rde

#endif // RDEDISKTOOL_APPLE_DISKIMAGE_H
