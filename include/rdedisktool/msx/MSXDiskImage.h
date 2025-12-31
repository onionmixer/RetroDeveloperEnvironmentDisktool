#ifndef RDEDISKTOOL_MSX_DISKIMAGE_H
#define RDEDISKTOOL_MSX_DISKIMAGE_H

#include "rdedisktool/DiskImage.h"
#include "rdedisktool/Types.h"

namespace rde {

/**
 * Base class for MSX disk image formats
 *
 * MSX uses standard PC-compatible disk formats:
 * - 360KB: 1 side, 80 tracks, 9 sectors/track, 512 bytes/sector
 * - 720KB: 2 sides, 80 tracks, 9 sectors/track, 512 bytes/sector
 *
 * File systems: MSX-DOS 1, MSX-DOS 2 (FAT12/FAT16)
 */
class MSXDiskImage : public DiskImage {
public:
    // Common MSX disk parameters
    static constexpr size_t TRACKS_40 = 40;
    static constexpr size_t TRACKS_80 = 80;
    static constexpr size_t SIDES_1 = 1;
    static constexpr size_t SIDES_2 = 2;
    static constexpr size_t SECTORS_8 = 8;
    static constexpr size_t SECTORS_9 = 9;
    static constexpr size_t BYTES_PER_SECTOR = 512;

    // Common disk sizes
    static constexpr size_t DISK_320KB = 327680;   // 1S 40T 8S (actually 320KB)
    static constexpr size_t DISK_360KB = 368640;   // 1S 40T 9S
    static constexpr size_t DISK_640KB = 655360;   // 2S 40T 8S (actually 640KB)
    static constexpr size_t DISK_720KB = 737280;   // 2S 80T 9S

    ~MSXDiskImage() override = default;

    //=========================================================================
    // DiskImage Interface Implementation
    //=========================================================================

    Platform getPlatform() const override { return Platform::MSX; }
    FileSystemType getFileSystemType() const override;
    DiskGeometry getGeometry() const override { return m_geometry; }
    bool isWriteProtected() const override { return m_writeProtected; }
    void setWriteProtected(bool protect) override { m_writeProtected = protect; }
    bool isModified() const override { return m_modified; }
    std::filesystem::path getFilePath() const override { return m_filePath; }
    const std::vector<uint8_t>& getRawData() const override { return m_data; }
    void setRawData(const std::vector<uint8_t>& data) override;

    //=========================================================================
    // MSX-Specific Methods
    //=========================================================================

    /**
     * Get boot sector data
     */
    const uint8_t* getBootSector() const;

    /**
     * Get media descriptor byte from boot sector
     * F8 = Fixed disk
     * F9 = 720KB 3.5" 2-sided
     * FA = 320KB 5.25" 1-sided
     * FB = 640KB 3.5" 2-sided
     * FC = 180KB 5.25" 1-sided
     * FD = 360KB 5.25" 2-sided
     * FE = 160KB 5.25" 1-sided
     * FF = 320KB 5.25" 2-sided
     */
    uint8_t getMediaDescriptor() const;

    /**
     * Get OEM name from boot sector (8 bytes)
     */
    std::string getOEMName() const;

    /**
     * Detect geometry from boot sector or file size
     */
    void detectGeometry();

    /**
     * Detect file system from boot sector
     */
    FileSystemType detectFileSystem() const;

    /**
     * Check if this is an MSX-DOS formatted disk
     */
    bool isMSXDOS() const;

    /**
     * Get total clusters
     */
    uint16_t getTotalClusters() const;

    /**
     * Get bytes per cluster
     */
    size_t getBytesPerCluster() const;

    /**
     * Get sectors per cluster from boot sector
     */
    uint8_t getSectorsPerCluster() const;

    /**
     * Get reserved sectors count
     */
    uint16_t getReservedSectors() const;

    /**
     * Get number of FATs
     */
    uint8_t getNumberOfFATs() const;

    /**
     * Get root directory entries count
     */
    uint16_t getRootEntryCount() const;

    /**
     * Get sectors per FAT
     */
    uint16_t getSectorsPerFAT() const;

    /**
     * Get first data sector (after boot, FATs, root dir)
     */
    size_t getFirstDataSector() const;

protected:
    MSXDiskImage();

    // Initialize geometry from parameters
    void initGeometry(size_t tracks, size_t sides, size_t sectors);

    // Initialize geometry from file size
    void initGeometryFromSize(size_t fileSize);

    // Calculate offset into raw data for a given track/side/sector
    virtual size_t calculateOffset(size_t track, size_t side, size_t sector) const;

    // Cached file system type
    mutable FileSystemType m_cachedFileSystem = FileSystemType::Unknown;
    mutable bool m_fileSystemDetected = false;
};

/**
 * MSX Boot Sector structure (first 512 bytes of disk)
 */
#pragma pack(push, 1)
struct MSXBootSector {
    uint8_t  jumpInstruction[3];   // JMP instruction (0xEB xx 0x90 or 0xE9 xx xx)
    char     oemName[8];           // OEM name (e.g., "MSX2-DOS")
    uint16_t bytesPerSector;       // Usually 512
    uint8_t  sectorsPerCluster;    // Usually 2
    uint16_t reservedSectors;      // Usually 1
    uint8_t  numberOfFATs;         // Usually 2
    uint16_t rootEntryCount;       // Usually 112 or 224
    uint16_t totalSectors16;       // Total sectors (16-bit)
    uint8_t  mediaDescriptor;      // Media type
    uint16_t sectorsPerFAT;        // Sectors per FAT
    uint16_t sectorsPerTrack;      // Sectors per track
    uint16_t numberOfHeads;        // Number of heads (sides)
    uint32_t hiddenSectors;        // Hidden sectors
    uint32_t totalSectors32;       // Total sectors (32-bit, FAT16)
    // ... additional fields for FAT16/FAT32
};
#pragma pack(pop)

} // namespace rde

#endif // RDEDISKTOOL_MSX_DISKIMAGE_H
