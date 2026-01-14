#ifndef RDEDISKTOOL_FILESYSTEM_HUMAN68KHANDLER_H
#define RDEDISKTOOL_FILESYSTEM_HUMAN68KHANDLER_H

#include "rdedisktool/FileSystemHandler.h"
#include "rdedisktool/x68000/X68000DiskImage.h"
#include <vector>
#include <string>

namespace rde {

/**
 * Human68k (X68000) File System Handler
 *
 * Human68k uses a FAT12-based file system similar to MS-DOS, but with
 * X68000-specific characteristics:
 * - Sector size: typically 1024 bytes (for 2HD disks)
 * - Boot sector contains BPB (BIOS Parameter Block)
 * - Supports both short (8.3) and long filenames (Human68k v3+)
 *
 * Structure:
 * - Boot sector (sector 1) - Contains BPB and IPL
 * - FAT tables (typically 2 copies)
 * - Root directory
 * - Data area
 */
class Human68kHandler : public FileSystemHandler {
public:
    Human68kHandler();
    ~Human68kHandler() override = default;

    // FileSystemHandler interface
    FileSystemType getType() const override { return FileSystemType::Human68k; }
    bool initialize(DiskImage* disk) override;
    std::vector<FileEntry> listFiles(const std::string& path = "") override;
    std::vector<uint8_t> readFile(const std::string& filename) override;
    bool writeFile(const std::string& filename,
                  const std::vector<uint8_t>& data,
                  const FileMetadata& metadata = {}) override;
    bool deleteFile(const std::string& filename) override;
    bool renameFile(const std::string& oldName,
                   const std::string& newName) override;
    size_t getFreeSpace() const override;
    size_t getTotalSpace() const override;
    bool fileExists(const std::string& filename) const override;
    bool format(const std::string& volumeName = "") override;
    std::string getVolumeName() const override;

    // Directory operations
    bool supportsDirectories() const override { return true; }
    bool createDirectory(const std::string& path) override;
    bool deleteDirectory(const std::string& path) override;
    bool isDirectory(const std::string& path) const override;

    // Cluster information
    struct ClusterInfo {
        uint16_t totalClusters;
        uint16_t freeClusters;
        uint16_t usedClusters;
        uint16_t badClusters;
        uint16_t reservedClusters;
    };

    ClusterInfo getClusterInfo() const;

    // Accessors for BPB values
    uint16_t getSectorsPerCluster() const { return m_sectorsPerCluster; }
    uint16_t getBytesPerSector() const { return m_bytesPerSector; }
    uint16_t getTotalClusters() const { return m_totalClusters; }

private:
    // BPB (BIOS Parameter Block) cached values
    uint16_t m_bytesPerSector = 1024;     // X68000 typically uses 1024
    uint8_t m_sectorsPerCluster = 1;
    uint16_t m_reservedSectors = 1;
    uint8_t m_numberOfFATs = 2;
    uint16_t m_rootEntryCount = 192;      // X68000 standard
    uint16_t m_totalSectors = 0;
    uint8_t m_mediaDescriptor = 0xFE;     // X68000 2HD
    uint16_t m_sectorsPerFAT = 2;
    uint16_t m_sectorsPerTrack = 8;
    uint16_t m_numberOfHeads = 2;

    // Derived values
    uint16_t m_rootDirSectors = 0;
    uint16_t m_firstDataSector = 0;
    uint16_t m_totalClusters = 0;
    uint16_t m_dataSectors = 0;

    // Directory entry structure (32 bytes) - same as FAT
    #pragma pack(push, 1)
    struct DirEntry {
        char name[8];
        char ext[3];
        uint8_t attr;
        uint8_t reserved[10];
        uint16_t time;
        uint16_t date;
        uint16_t startCluster;
        uint32_t fileSize;
    };
    #pragma pack(pop)

    static_assert(sizeof(DirEntry) == 32, "DirEntry must be 32 bytes");

    // File attributes
    static constexpr uint8_t ATTR_READ_ONLY = 0x01;
    static constexpr uint8_t ATTR_HIDDEN = 0x02;
    static constexpr uint8_t ATTR_SYSTEM = 0x04;
    static constexpr uint8_t ATTR_VOLUME_ID = 0x08;
    static constexpr uint8_t ATTR_DIRECTORY = 0x10;
    static constexpr uint8_t ATTR_ARCHIVE = 0x20;

    // Special filename bytes
    static constexpr uint8_t DIR_FREE = 0xE5;
    static constexpr uint8_t DIR_END = 0x00;

    // FAT12 cluster values
    static constexpr uint16_t FAT12_FREE = 0x000;
    static constexpr uint16_t FAT12_RESERVED = 0xFF0;
    static constexpr uint16_t FAT12_BAD = 0xFF7;
    static constexpr uint16_t FAT12_EOF = 0xFF8;

    // Helper methods
    bool parseBPB();
    std::vector<uint8_t> readFAT() const;
    void writeFAT(const std::vector<uint8_t>& fat);
    uint16_t getFATEntry(const std::vector<uint8_t>& fat, uint16_t cluster) const;
    void setFATEntry(std::vector<uint8_t>& fat, uint16_t cluster, uint16_t value);

    std::vector<uint16_t> getClusterChain(uint16_t startCluster) const;
    uint16_t allocateCluster(std::vector<uint8_t>& fat);
    void freeClusterChain(std::vector<uint8_t>& fat, uint16_t startCluster);

    std::vector<uint8_t> readCluster(uint16_t cluster) const;
    void writeCluster(uint16_t cluster, const std::vector<uint8_t>& data);

    std::vector<DirEntry> readRootDirectory() const;
    void writeRootDirectory(const std::vector<DirEntry>& entries);
    int findDirectoryEntry(const std::vector<DirEntry>& entries,
                          const std::string& filename) const;

    std::string formatFilename(const char* name, const char* ext) const;
    void parseFilename(const std::string& filename, char* name, char* ext) const;

    FileEntry dirEntryToFileEntry(const DirEntry& entry) const;
    uint16_t countFreeClusters() const;

    // Sector addressing helpers for X68000
    // X68000 uses 1-indexed sectors within each track
    void logicalToPhysical(uint32_t logicalSector, size_t& track, size_t& head, size_t& sector) const;
    std::vector<uint8_t> readLogicalSector(uint32_t logicalSector) const;
    void writeLogicalSector(uint32_t logicalSector, const std::vector<uint8_t>& data);

    // Subdirectory support
    std::pair<uint16_t, std::string> resolvePath(const std::string& path) const;
    std::vector<DirEntry> readDirectoryCluster(uint16_t cluster) const;
    void writeDirectoryCluster(uint16_t cluster, const std::vector<DirEntry>& entries);
    int findEntryInDirectory(uint16_t cluster, const std::string& name) const;
    std::vector<DirEntry> getDirectoryEntries(uint16_t cluster) const;
    void setDirectoryEntries(uint16_t cluster, const std::vector<DirEntry>& entries);
};

} // namespace rde

#endif // RDEDISKTOOL_FILESYSTEM_HUMAN68KHANDLER_H
