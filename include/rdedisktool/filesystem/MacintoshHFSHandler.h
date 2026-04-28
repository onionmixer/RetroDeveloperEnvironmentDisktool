#ifndef RDEDISKTOOL_FILESYSTEM_MACINTOSHHFSHANDLER_H
#define RDEDISKTOOL_FILESYSTEM_MACINTOSHHFSHANDLER_H

#include "rdedisktool/FileSystemHandler.h"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace rde {

/**
 * Macintosh HFS read-only handler.
 *
 * Phase 1 scope: parse the Master Directory Block, walk the Catalog B-tree
 * leaf chain, and extract data / resource forks via initial extents plus the
 * Extents Overflow B-tree. Write paths (writeFile / deleteFile / renameFile /
 * format / createDirectory) all return false and emit no I/O.
 *
 * References (MacDiskcopy/document/SPEC_MACDISKIMAGE.md):
 *   §1019  HFS Volume Layout
 *   §1197  HFS B-tree Reader
 *   §1461  Catalog B-tree
 *   §1568  Extents Overflow B-tree
 *   §1621  Fork Extraction
 */
class MacintoshHFSHandler : public FileSystemHandler {
public:
    // SPEC §1019: Master Directory Block at file offset 0x400.
    struct Mdb {
        uint16_t signature = 0;       // drSigWord  ("BD")
        uint32_t createDate = 0;      // drCrDate
        uint32_t modifyDate = 0;      // drLsMod
        uint16_t numFiles = 0;        // drNmFls
        uint16_t bitmapStart = 0;     // drVBMSt   (first volume bitmap sector)
        uint16_t firstAllocBlock = 0; // drAlBlSt  (first allocation block in 512B sectors)
        uint16_t numAllocBlocks = 0;  // drNmAlBlks
        uint32_t allocBlockSize = 0;  // drAlBlkSiz
        uint32_t nextCNID = 0;        // drNxtCNID  (next unused catalog node ID)
        uint16_t freeAllocBlocks = 0; // drFreeBks
        std::string volumeName;       // drVN  (Pascal, max 27 bytes, MacRoman)
        uint32_t catalogFileSize = 0; // drCTFlSize
        uint32_t extentsFileSize = 0; // drXTFlSize
        std::array<uint16_t, 6> catalogExtents{}; // drCTExtRec — 3 extents (start,count) pairs
        std::array<uint16_t, 6> extentsExtents{}; // drXTExtRec
        uint32_t blessedFolderCNID = 0;           // drFndrInfo[0]
    };

    // SPEC §1118: HFS boot block layout (for bootability inspection — fully
    // read here so M5 can use the cached values without re-parsing).
    struct BootBlock {
        bool present = false;          // signature == "LK"
        std::string systemName;        // 0x0a, Pascal, MacRoman
        std::string finderName;        // 0x1a
    };

    MacintoshHFSHandler() = default;
    ~MacintoshHFSHandler() override = default;

    FileSystemType getType() const override { return FileSystemType::HFS; }
    bool initialize(DiskImage* disk) override;

    std::vector<FileEntry> listFiles(const std::string& path = "") override;
    std::vector<uint8_t> readFile(const std::string& filename) override;

    // Phase 1 = read-only. Mutate paths refuse explicitly.
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

    bool supportsDirectories() const override { return true; }
    bool isDirectory(const std::string& path) const override;

    // Public read access for diagnostics & later phases (M5 boot policy).
    const Mdb& mdb() const { return m_mdb; }
    const BootBlock& bootBlock() const { return m_bootBlock; }

    /**
     * Extract a fork by CNID + fork type. fork_type: 0 = data, 0xFF = resource.
     * Returns the fork bytes truncated to its logical length.
     */
    std::vector<uint8_t> extractFork(uint32_t fileCNID, uint8_t forkType) const;

    // Catalog leaf records keyed by parent CNID -> child entries. Public so
    // that CLI exporters (AppleDouble / MacBinary) can read the cached
    // metadata directly. Built once at initialize() and reused thereafter.
    struct CatalogChild {
        uint32_t cnid = 0;
        std::string name;          // UTF-8 (decoded from MacRoman)
        std::string macRomanName;  // raw MacRoman (for AppleDouble entry-id 3)
        bool isDirectory = false;
        uint32_t dataLogical = 0;
        uint32_t rsrcLogical = 0;
        uint32_t createDate = 0;
        uint32_t modifyDate = 0;
        std::array<uint16_t, 6> dataExtents{};   // 3 extents (start, count)
        std::array<uint16_t, 6> rsrcExtents{};
        uint8_t fileType[4]  = {0,0,0,0};
        uint8_t creator[4]   = {0,0,0,0};
        // Raw Finder info bytes (catalog file record offsets per SPEC §1648):
        //   FInfo  : data offset 0x04..0x13 (16 bytes)
        //   FXInfo : data offset 0x38..0x47 (16 bytes)
        uint8_t finfo[16]   = {0};
        uint8_t fxinfo[16]  = {0};
    };

    // Public lookup used by CLI exporters (AppleDouble / MacBinary). Returns
    // nullptr when the path does not resolve to a file or folder.
    const CatalogChild* lookupByPath(const std::string& path) const;

private:
    Mdb m_mdb{};
    BootBlock m_bootBlock{};

    std::unordered_map<uint32_t, std::vector<CatalogChild>> m_childrenByParent;

    // CNID -> CatalogChild lookup for fast path resolution / extract.
    // The HFS root directory CNID is fixed at 2.
    std::unordered_map<uint32_t, CatalogChild> m_byCNID;

    // Extents Overflow leaf records: (file_cnid, fork_type, start_block) -> 3 extents.
    struct ExtentsKey {
        uint32_t cnid;
        uint8_t  forkType;
        uint16_t startBlock;
        bool operator==(const ExtentsKey& o) const {
            return cnid == o.cnid && forkType == o.forkType && startBlock == o.startBlock;
        }
    };
    struct ExtentsKeyHash {
        size_t operator()(const ExtentsKey& k) const noexcept {
            return std::hash<uint64_t>{}(
                (static_cast<uint64_t>(k.cnid) << 32) |
                (static_cast<uint64_t>(k.forkType) << 24) |
                static_cast<uint64_t>(k.startBlock));
        }
    };
    std::unordered_map<ExtentsKey, std::array<uint16_t, 6>, ExtentsKeyHash> m_extentsOverflow;

    // Helpers (defined in the .cpp).
    bool parseMdb();
    bool parseBootBlock();
    bool walkCatalogLeaves();
    bool walkExtentsOverflowLeaves();
    bool walkBTreeLeaves(uint32_t btreeFileSize,
                         const std::array<uint16_t, 6>& fileExtents,
                         std::vector<uint8_t>& outBuffer,
                         uint16_t& outNodeSize);
    void parseCatalogLeafNode(const uint8_t* node, size_t nodeSize);
    void parseExtentsLeafNode(const uint8_t* node, size_t nodeSize);

    // Resolve a full path "/Folder/SubFolder/File" (or "Folder/SubFolder/File")
    // into the CatalogChild entry. Path uses '/' separator. Empty path → root.
    const CatalogChild* resolvePath(const std::string& path) const;

    // Read raw bytes from an HFS allocation-block run.
    std::vector<uint8_t> readAllocBlocks(uint16_t startBlock, uint16_t count) const;
};

} // namespace rde

#endif // RDEDISKTOOL_FILESYSTEM_MACINTOSHHFSHANDLER_H
