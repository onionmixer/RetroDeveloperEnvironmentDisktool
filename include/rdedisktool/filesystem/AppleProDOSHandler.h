#ifndef RDEDISKTOOL_FILESYSTEM_APPLEPRODOSHANDLER_H
#define RDEDISKTOOL_FILESYSTEM_APPLEPRODOSHANDLER_H

#include "rdedisktool/FileSystemHandler.h"
#include "rdedisktool/apple/AppleDiskImage.h"
#include <vector>
#include <string>
#include <map>

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

private:
    // Constants
    static constexpr size_t BLOCK_SIZE = 512;
    static constexpr size_t BLOCKS_PER_TRACK = 8;
    static constexpr size_t BOOT_BLOCK = 0;
    static constexpr size_t VOLUME_DIR_BLOCK = 2;
    static constexpr size_t BITMAP_BLOCK = 6;
    static constexpr size_t DIR_ENTRY_SIZE = 0x27;       // 39 bytes
    static constexpr size_t ENTRIES_PER_BLOCK = 13;
    static constexpr size_t MAX_FILENAME_LENGTH = 15;
    static constexpr size_t TOTAL_BLOCKS = 280;          // 140K disk

    // Storage types (upper nibble of storage_type_and_name_length)
    static constexpr uint8_t STORAGE_DELETED = 0x00;
    static constexpr uint8_t STORAGE_SEEDLING = 0x01;    // 1 data block
    static constexpr uint8_t STORAGE_SAPLING = 0x02;     // 1 index + up to 256 data blocks
    static constexpr uint8_t STORAGE_TREE = 0x03;        // 1 master + up to 256 index blocks
    static constexpr uint8_t STORAGE_PASCAL_AREA = 0x04;
    static constexpr uint8_t STORAGE_GSOS_FORK = 0x05;
    static constexpr uint8_t STORAGE_SUBDIRECTORY = 0x0D;
    static constexpr uint8_t STORAGE_SUBDIR_HEADER = 0x0E;
    static constexpr uint8_t STORAGE_VOLUME_HEADER = 0x0F;

    // File types
    static constexpr uint8_t FILETYPE_UNK = 0x00;
    static constexpr uint8_t FILETYPE_BAD = 0x01;
    static constexpr uint8_t FILETYPE_PCD = 0x02;  // Pascal code
    static constexpr uint8_t FILETYPE_PTX = 0x03;  // Pascal text
    static constexpr uint8_t FILETYPE_TXT = 0x04;
    static constexpr uint8_t FILETYPE_PDA = 0x05;  // Pascal data
    static constexpr uint8_t FILETYPE_BIN = 0x06;
    static constexpr uint8_t FILETYPE_FNT = 0x07;  // Font
    static constexpr uint8_t FILETYPE_FOT = 0x08;  // Graphics screen
    static constexpr uint8_t FILETYPE_BA3 = 0x09;  // Business BASIC program
    static constexpr uint8_t FILETYPE_DA3 = 0x0A;  // Business BASIC data
    static constexpr uint8_t FILETYPE_WPF = 0x0B;  // Word processor
    static constexpr uint8_t FILETYPE_SOS = 0x0C;  // SOS system
    static constexpr uint8_t FILETYPE_DIR = 0x0F;
    static constexpr uint8_t FILETYPE_RPD = 0x10;  // RPS data
    static constexpr uint8_t FILETYPE_RPI = 0x11;  // RPS index
    static constexpr uint8_t FILETYPE_AFD = 0x12;  // AppleFile discard
    static constexpr uint8_t FILETYPE_AFM = 0x13;  // AppleFile model
    static constexpr uint8_t FILETYPE_AFR = 0x14;  // AppleFile report
    static constexpr uint8_t FILETYPE_SCL = 0x15;  // Screen library
    static constexpr uint8_t FILETYPE_PFS = 0x16;  // PFS document
    static constexpr uint8_t FILETYPE_ADB = 0x19;  // AppleWorks database
    static constexpr uint8_t FILETYPE_AWP = 0x1A;  // AppleWorks word proc
    static constexpr uint8_t FILETYPE_ASP = 0x1B;  // AppleWorks spreadsheet
    static constexpr uint8_t FILETYPE_CMD = 0xF0;  // ProDOS added command
    static constexpr uint8_t FILETYPE_INT = 0xFA;  // Integer BASIC
    static constexpr uint8_t FILETYPE_IVR = 0xFB;  // Integer BASIC variables
    static constexpr uint8_t FILETYPE_BAS = 0xFC;  // Applesoft BASIC
    static constexpr uint8_t FILETYPE_VAR = 0xFD;  // Applesoft variables
    static constexpr uint8_t FILETYPE_REL = 0xFE;  // Relocatable code
    static constexpr uint8_t FILETYPE_SYS = 0xFF;  // ProDOS system

    // Access flags
    static constexpr uint8_t ACCESS_READ = 0x01;
    static constexpr uint8_t ACCESS_WRITE = 0x02;
    static constexpr uint8_t ACCESS_BACKUP = 0x20;
    static constexpr uint8_t ACCESS_RENAME = 0x40;
    static constexpr uint8_t ACCESS_DESTROY = 0x80;
    static constexpr uint8_t ACCESS_DEFAULT = 0xC3;  // Read, write, rename, destroy

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
    bool writeDirectoryEntry(uint16_t dirKeyBlock, size_t entryIndex, const DirectoryEntry& entry);
    int findDirectoryEntry(uint16_t dirKeyBlock, const std::string& filename) const;
    int findFreeDirectoryEntry(uint16_t dirKeyBlock) const;

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
