#ifndef RDEDISKTOOL_FILESYSTEM_MSXDOSHANDLER_H
#define RDEDISKTOOL_FILESYSTEM_MSXDOSHANDLER_H

#include "rdedisktool/FileSystemHandler.h"
#include "rdedisktool/msx/MSXDiskImage.h"
#include <vector>
#include <string>

namespace rde {

/**
 * MSX-DOS / FAT12 File System Handler
 *
 * Supports MSX-DOS 1, MSX-DOS 2, and standard FAT12 file systems.
 *
 * Structure:
 * - Boot sector (contains BPB)
 * - FAT tables (typically 2 copies)
 * - Root directory
 * - Data area
 */
class MSXDOSHandler : public FileSystemHandler {
public:
    MSXDOSHandler();
    ~MSXDOSHandler() override = default;

    // FileSystemHandler interface
    FileSystemType getType() const override;
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

private:
    // BPB (BIOS Parameter Block) cached values
    uint16_t m_bytesPerSector = 512;
    uint8_t m_sectorsPerCluster = 2;
    uint16_t m_reservedSectors = 1;
    uint8_t m_numberOfFATs = 2;
    uint16_t m_rootEntryCount = 112;
    uint16_t m_totalSectors = 0;
    uint8_t m_mediaDescriptor = 0xF9;
    uint16_t m_sectorsPerFAT = 3;
    uint16_t m_sectorsPerTrack = 9;
    uint16_t m_numberOfHeads = 2;

    // Derived values
    uint16_t m_rootDirSectors = 0;
    uint16_t m_firstDataSector = 0;
    uint16_t m_totalClusters = 0;
    uint16_t m_dataSectors = 0;

    // Directory entry structure (32 bytes)
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
};

} // namespace rde

#endif // RDEDISKTOOL_FILESYSTEM_MSXDOSHANDLER_H
