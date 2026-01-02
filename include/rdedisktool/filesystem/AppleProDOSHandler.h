#ifndef RDEDISKTOOL_FILESYSTEM_APPLEPRODOSHANDLER_H
#define RDEDISKTOOL_FILESYSTEM_APPLEPRODOSHANDLER_H

#include "rdedisktool/FileSystemHandler.h"
#include "rdedisktool/apple/AppleDiskImage.h"
#include "rdedisktool/apple/AppleConstants.h"
#include <vector>
#include <string>
#include <map>
#include <optional>

namespace rde {

/**
 * Apple II ProDOS File System Handler
 *
 * Supports the ProDOS file system structure:
 * - Block-based (512 bytes per block)
 * - Volume directory at block 2
 * - Subdirectories supported
 * - Index blocks for large files (seedling, sapling, tree)
 *
 * Layout:
 * - Blocks 0-1: Boot blocks
 * - Block 2+: Volume directory (key block)
 * - Block 6: Volume bitmap (typically)
 * - Remaining blocks: Files and subdirectories
 */
class AppleProDOSHandler : public FileSystemHandler {
public:
    AppleProDOSHandler();
    ~AppleProDOSHandler() override = default;

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
    ValidationResult validateExtended() const override;

    // Directory operations (override from FileSystemHandler)
    bool supportsDirectories() const override { return true; }
    bool createDirectory(const std::string& path) override;
    bool deleteDirectory(const std::string& path) override;
    bool isDirectory(const std::string& path) const override;

private:
    // Constants from AppleConstants::ProDOS
    static constexpr size_t BLOCK_SIZE = AppleConstants::ProDOS::BLOCK_SIZE;
    static constexpr size_t BLOCKS_PER_TRACK = AppleConstants::ProDOS::BLOCKS_PER_TRACK;
    static constexpr size_t BOOT_BLOCK = AppleConstants::ProDOS::BOOT_BLOCK;
    static constexpr size_t VOLUME_DIR_BLOCK = AppleConstants::ProDOS::VOLUME_DIR_BLOCK;
    static constexpr size_t BITMAP_BLOCK = AppleConstants::ProDOS::BITMAP_BLOCK;
    static constexpr size_t DIR_ENTRY_SIZE = AppleConstants::ProDOS::DIR_ENTRY_SIZE;
    static constexpr size_t ENTRIES_PER_BLOCK = AppleConstants::ProDOS::ENTRIES_PER_BLOCK;
    static constexpr size_t MAX_FILENAME_LENGTH = AppleConstants::ProDOS::MAX_FILENAME_LENGTH;
    static constexpr size_t TOTAL_BLOCKS = AppleConstants::ProDOS::TOTAL_BLOCKS;

    // Storage types from AppleConstants::ProDOS
    static constexpr uint8_t STORAGE_DELETED = AppleConstants::ProDOS::STORAGE_DELETED;
    static constexpr uint8_t STORAGE_SEEDLING = AppleConstants::ProDOS::STORAGE_SEEDLING;
    static constexpr uint8_t STORAGE_SAPLING = AppleConstants::ProDOS::STORAGE_SAPLING;
    static constexpr uint8_t STORAGE_TREE = AppleConstants::ProDOS::STORAGE_TREE;
    static constexpr uint8_t STORAGE_PASCAL_AREA = AppleConstants::ProDOS::STORAGE_PASCAL_AREA;
    static constexpr uint8_t STORAGE_GSOS_FORK = AppleConstants::ProDOS::STORAGE_GSOS_FORK;
    static constexpr uint8_t STORAGE_SUBDIRECTORY = AppleConstants::ProDOS::STORAGE_SUBDIRECTORY;
    static constexpr uint8_t STORAGE_SUBDIR_HEADER = AppleConstants::ProDOS::STORAGE_SUBDIR_HEADER;
    static constexpr uint8_t STORAGE_VOLUME_HEADER = AppleConstants::ProDOS::STORAGE_VOLUME_HEADER;

    // File types from AppleConstants::ProDOS
    static constexpr uint8_t FILETYPE_UNK = AppleConstants::ProDOS::FILETYPE_UNK;
    static constexpr uint8_t FILETYPE_BAD = AppleConstants::ProDOS::FILETYPE_BAD;
    static constexpr uint8_t FILETYPE_PCD = AppleConstants::ProDOS::FILETYPE_PCD;
    static constexpr uint8_t FILETYPE_PTX = AppleConstants::ProDOS::FILETYPE_PTX;
    static constexpr uint8_t FILETYPE_TXT = AppleConstants::ProDOS::FILETYPE_TXT;
    static constexpr uint8_t FILETYPE_PDA = AppleConstants::ProDOS::FILETYPE_PDA;
    static constexpr uint8_t FILETYPE_BIN = AppleConstants::ProDOS::FILETYPE_BIN;
    static constexpr uint8_t FILETYPE_FNT = AppleConstants::ProDOS::FILETYPE_FNT;
    static constexpr uint8_t FILETYPE_FOT = AppleConstants::ProDOS::FILETYPE_FOT;
    static constexpr uint8_t FILETYPE_BA3 = AppleConstants::ProDOS::FILETYPE_BA3;
    static constexpr uint8_t FILETYPE_DA3 = AppleConstants::ProDOS::FILETYPE_DA3;
    static constexpr uint8_t FILETYPE_WPF = AppleConstants::ProDOS::FILETYPE_WPF;
    static constexpr uint8_t FILETYPE_SOS = AppleConstants::ProDOS::FILETYPE_SOS;
    static constexpr uint8_t FILETYPE_DIR = AppleConstants::ProDOS::FILETYPE_DIR;
    static constexpr uint8_t FILETYPE_RPD = AppleConstants::ProDOS::FILETYPE_RPD;
    static constexpr uint8_t FILETYPE_RPI = AppleConstants::ProDOS::FILETYPE_RPI;
    static constexpr uint8_t FILETYPE_AFD = AppleConstants::ProDOS::FILETYPE_AFD;
    static constexpr uint8_t FILETYPE_AFM = AppleConstants::ProDOS::FILETYPE_AFM;
    static constexpr uint8_t FILETYPE_AFR = AppleConstants::ProDOS::FILETYPE_AFR;
    static constexpr uint8_t FILETYPE_SCL = AppleConstants::ProDOS::FILETYPE_SCL;
    static constexpr uint8_t FILETYPE_PFS = AppleConstants::ProDOS::FILETYPE_PFS;
    static constexpr uint8_t FILETYPE_ADB = AppleConstants::ProDOS::FILETYPE_ADB;
    static constexpr uint8_t FILETYPE_AWP = AppleConstants::ProDOS::FILETYPE_AWP;
    static constexpr uint8_t FILETYPE_ASP = AppleConstants::ProDOS::FILETYPE_ASP;
    static constexpr uint8_t FILETYPE_CMD = AppleConstants::ProDOS::FILETYPE_CMD;
    static constexpr uint8_t FILETYPE_INT = AppleConstants::ProDOS::FILETYPE_INT;
    static constexpr uint8_t FILETYPE_IVR = AppleConstants::ProDOS::FILETYPE_IVR;
    static constexpr uint8_t FILETYPE_BAS = AppleConstants::ProDOS::FILETYPE_BAS;
    static constexpr uint8_t FILETYPE_VAR = AppleConstants::ProDOS::FILETYPE_VAR;
    static constexpr uint8_t FILETYPE_REL = AppleConstants::ProDOS::FILETYPE_REL;
    static constexpr uint8_t FILETYPE_SYS = AppleConstants::ProDOS::FILETYPE_SYS;

    // Access flags from AppleConstants::ProDOS
    static constexpr uint8_t ACCESS_READ = AppleConstants::ProDOS::ACCESS_READ;
    static constexpr uint8_t ACCESS_WRITE = AppleConstants::ProDOS::ACCESS_WRITE;
    static constexpr uint8_t ACCESS_BACKUP = AppleConstants::ProDOS::ACCESS_BACKUP;
    static constexpr uint8_t ACCESS_RENAME = AppleConstants::ProDOS::ACCESS_RENAME;
    static constexpr uint8_t ACCESS_DESTROY = AppleConstants::ProDOS::ACCESS_DESTROY;
    static constexpr uint8_t ACCESS_DEFAULT = AppleConstants::ProDOS::ACCESS_DEFAULT;

    // Directory entry structure
    struct DirectoryEntry {
        uint8_t storageType;          // Upper nibble
        uint8_t nameLength;           // Lower nibble
        char filename[MAX_FILENAME_LENGTH];
        uint8_t fileType;
        uint16_t keyPointer;          // Block number
        uint16_t blocksUsed;
        uint32_t eof;                 // 3 bytes, file size
        uint32_t creationDateTime;    // ProDOS date/time format
        uint8_t version;
        uint8_t minVersion;
        uint8_t access;
        uint16_t auxType;
        uint32_t lastModDateTime;
        uint16_t headerPointer;       // Block number of directory header

        bool isDeleted() const { return storageType == STORAGE_DELETED; }
        bool isDirectory() const { return storageType == STORAGE_SUBDIRECTORY; }
        bool isVolumeHeader() const { return storageType == STORAGE_VOLUME_HEADER; }
        bool isSubdirHeader() const { return storageType == STORAGE_SUBDIR_HEADER; }
    };

    // Volume/Directory header structure
    struct DirectoryHeader {
        uint8_t storageType;
        uint8_t nameLength;
        char name[MAX_FILENAME_LENGTH];
        uint32_t creationDateTime;
        uint8_t version;
        uint8_t minVersion;
        uint8_t access;
        uint8_t entryLength;          // Should be 0x27
        uint8_t entriesPerBlock;      // Should be 0x0D
        uint16_t fileCount;
        uint16_t bitmapPointer;       // Volume only
        uint16_t totalBlocks;         // Volume only
        uint16_t parentPointer;       // Subdir only
        uint8_t parentEntry;          // Subdir only
        uint8_t parentEntryLength;    // Subdir only
    };

    // Cached volume header
    DirectoryHeader m_volumeHeader;
    std::vector<bool> m_bitmap;       // Block allocation bitmap

    // Helper methods - Block I/O
    std::vector<uint8_t> readBlock(size_t block) const;
    void writeBlock(size_t block, const std::vector<uint8_t>& data);

    // Helper methods - Bitmap operations
    bool parseVolumeBitmap();
    void writeVolumeBitmap();
    bool isBlockFree(size_t block) const;
    void markBlockUsed(size_t block);
    void markBlockFree(size_t block);
    size_t allocateBlock();
    size_t countFreeBlocks() const;

    // Helper methods - Directory operations
    bool parseVolumeHeader();
    void writeVolumeHeader();
    std::vector<DirectoryEntry> readDirectory(uint16_t keyBlock) const;
    std::optional<DirectoryEntry> readDirectoryEntryAt(uint16_t dirKeyBlock, size_t physicalIndex) const;
    bool writeDirectoryEntry(uint16_t dirKeyBlock, size_t entryIndex, const DirectoryEntry& entry);
    int findDirectoryEntry(uint16_t dirKeyBlock, const std::string& filename) const;
    int findFreeDirectoryEntry(uint16_t dirKeyBlock) const;
    bool updateDirectoryFileCount(uint16_t dirKeyBlock, int delta);

    // Helper methods - File I/O
    std::vector<uint8_t> readFileData(const DirectoryEntry& entry) const;
    std::vector<uint16_t> getFileBlocks(const DirectoryEntry& entry) const;
    bool writeFileData(uint16_t keyBlock, uint8_t storageType, const std::vector<uint8_t>& data);
    void freeFileBlocks(const DirectoryEntry& entry);

    // Helper methods - Path handling
    std::pair<uint16_t, std::string> resolvePath(const std::string& path) const;
    std::string formatFilename(const char* name, uint8_t length) const;
    void parseFilename(const std::string& filename, char* name, uint8_t& length) const;
    bool isValidFilename(const std::string& filename) const;

    // Helper methods - Date/Time
    static uint32_t packDateTime(std::time_t time);
    static std::time_t unpackDateTime(uint32_t packed);

    // Helper methods - File type
    std::string fileTypeToString(uint8_t type) const;
    FileEntry directoryEntryToFileEntry(const DirectoryEntry& entry) const;

    // Calculate storage type needed for a file size
    uint8_t calculateStorageType(size_t size) const;
};

} // namespace rde

#endif // RDEDISKTOOL_FILESYSTEM_APPLEPRODOSHANDLER_H
