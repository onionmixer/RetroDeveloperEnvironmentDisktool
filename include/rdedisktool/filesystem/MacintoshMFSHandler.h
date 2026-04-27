#ifndef RDEDISKTOOL_FILESYSTEM_MACINTOSHMFSHANDLER_H
#define RDEDISKTOOL_FILESYSTEM_MACINTOSHMFSHANDLER_H

#include "rdedisktool/FileSystemHandler.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rde {

/**
 * Macintosh MFS read-only handler.
 *
 * MFS is the flat predecessor to HFS — there is no catalog tree, no folder
 * records, and no extents overflow. The directory is a sequence of variable-
 * length entries packed into directory blocks. The allocation map is 12-bit
 * big-endian entries describing block 2 onward.
 *
 * SPEC references (MacDiskcopy/document/SPEC_MACDISKIMAGE.md):
 *   §410   MFS Volume Identification (MDB layout, directory entry layout,
 *          12-bit allocation map encoding)
 *   §1097  Classic Macintosh Bootability — MFS branch
 */
class MacintoshMFSHandler : public FileSystemHandler {
public:
    struct Mdb {
        uint16_t signature = 0;        // drSigWord (0xD2D7)
        uint16_t numFiles = 0;          // drNmFls
        uint16_t directoryStart = 0;    // drDrSt   (first directory sector)
        uint16_t directoryLength = 0;   // drBlLen  (directory length in sectors)
        uint16_t numAllocBlocks = 0;    // drNmAlBlks
        uint32_t allocBlockSize = 0;    // drAlBlkSiz
        uint16_t firstAllocBlock = 0;   // drAlBlSt (first allocation block sector)
        uint32_t nextFileNumber = 0;    // drNxtFNum
        uint16_t freeAllocBlocks = 0;   // drFreeBks
        std::string volumeName;         // drVN (Pascal, MacRoman → UTF-8)
    };

    struct BootBlock {
        bool present = false;
        std::string systemName;
        std::string finderName;
    };

    struct DirEntry {
        bool used = false;
        bool locked = false;
        uint8_t fileType[4]  = {0,0,0,0};
        uint8_t creator[4]   = {0,0,0,0};
        uint32_t cnid = 0;
        uint16_t dataStartBlock = 0;
        uint32_t dataLogical = 0;
        uint16_t rsrcStartBlock = 0;
        uint32_t rsrcLogical = 0;
        uint32_t createDate = 0;
        uint32_t modifyDate = 0;
        std::string name;     // UTF-8
    };

    MacintoshMFSHandler() = default;
    ~MacintoshMFSHandler() override = default;

    FileSystemType getType() const override { return FileSystemType::MFS; }
    bool initialize(DiskImage* disk) override;

    std::vector<FileEntry> listFiles(const std::string& path = "") override;
    std::vector<uint8_t> readFile(const std::string& filename) override;

    bool writeFile(const std::string& filename,
                   const std::vector<uint8_t>& data,
                   const FileMetadata& metadata = {}) override;
    bool deleteFile(const std::string& filename) override;
    bool renameFile(const std::string& oldName, const std::string& newName) override;
    bool format(const std::string& volumeName = "") override;

    size_t getFreeSpace() const override;
    size_t getTotalSpace() const override;
    bool fileExists(const std::string& filename) const override;
    std::string getVolumeName() const override;

    bool supportsDirectories() const override { return false; }

    const Mdb& mdb() const { return m_mdb; }
    const BootBlock& bootBlock() const { return m_bootBlock; }
    const std::vector<DirEntry>& entries() const { return m_entries; }

    // Extract a fork by start-block / logical size, following the 12-bit map.
    std::vector<uint8_t> extractFork(uint16_t startBlock, uint32_t logical) const;

private:
    Mdb m_mdb{};
    BootBlock m_bootBlock{};
    std::vector<DirEntry> m_entries;     // active (used-bit set) entries

    bool parseMdb();
    bool parseBootBlock();
    bool parseDirectory();
    const DirEntry* findEntry(const std::string& name) const;

    // Read a 12-bit BE big-endian allocation map entry. Index 0 corresponds
    // to allocation block 2.
    uint16_t readAllocEntry(size_t index) const;
    // Convert allocation block N → byte offset in the raw stream.
    uint64_t blockOffset(uint16_t block) const;
};

} // namespace rde

#endif // RDEDISKTOOL_FILESYSTEM_MACINTOSHMFSHANDLER_H
