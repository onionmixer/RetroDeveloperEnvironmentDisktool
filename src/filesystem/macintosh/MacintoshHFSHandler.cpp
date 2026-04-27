#include "rdedisktool/filesystem/MacintoshHFSHandler.h"
#include "rdedisktool/Exceptions.h"
#include "rdedisktool/utils/MacEpoch.h"
#include "rdedisktool/utils/MacRoman.h"
#include "rdedisktool/utils/PascalString.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <sstream>

namespace rde {

namespace {

inline uint16_t be16(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}
inline uint32_t be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
            static_cast<uint32_t>(p[3]);
}

// HFS root volume folder CNID (per SPEC §1461 "Path Reconstruction").
constexpr uint32_t HFS_ROOT_CNID = 2;

// HFS extent types stored in the Extents Overflow B-tree key.
constexpr uint8_t HFS_FORK_DATA     = 0x00;
constexpr uint8_t HFS_FORK_RESOURCE = 0xFF;

// Catalog/extents record types.
constexpr uint8_t REC_FOLDER = 0x01;
constexpr uint8_t REC_FILE   = 0x02;
// Thread records (0x03 / 0x04) are intentionally ignored — paths are
// reconstructed via parent CNID relationships.

} // namespace

bool MacintoshHFSHandler::initialize(DiskImage* disk) {
    m_disk = disk;
    if (!disk) return false;

    if (!parseMdb()) return false;
    parseBootBlock();          // optional — non-bootable disks have all zeros
    if (!walkExtentsOverflowLeaves()) {
        // Extents Overflow B-tree may be empty for many small volumes.
        // We still continue; extractFork() will fall back to the catalog's
        // initial 3 extents and report incomplete reads if necessary.
        m_extentsOverflow.clear();
    }
    if (!walkCatalogLeaves()) return false;
    return true;
}

bool MacintoshHFSHandler::parseMdb() {
    const auto& raw = m_disk->getRawData();
    if (raw.size() < 0x400 + 0xa2) return false;
    const uint8_t* p = raw.data() + 0x400;

    m_mdb.signature = be16(p + 0x00);
    if (m_mdb.signature != 0x4244) return false;  // "BD"

    m_mdb.createDate       = be32(p + 0x02);
    m_mdb.modifyDate       = be32(p + 0x06);
    m_mdb.numFiles         = be16(p + 0x06 + 0x06);  // drNmFls @ 0x0c
    m_mdb.numAllocBlocks   = be16(p + 0x12);
    m_mdb.allocBlockSize   = be32(p + 0x14);
    m_mdb.firstAllocBlock  = be16(p + 0x1c);
    m_mdb.freeAllocBlocks  = be16(p + 0x22);

    // Volume name: Pascal string, length byte at MDB offset 0x24, up to 27 chars
    // of MacRoman-encoded payload.
    {
        const size_t avail = raw.size() - (0x400 + 0x24);
        const std::string raw_pname = readPascalBounded(p + 0x24, avail, 27);
        m_mdb.volumeName = macRomanToUtf8(raw_pname);
    }

    // drFndrInfo[0] = blessed System Folder CNID
    m_mdb.blessedFolderCNID = be32(p + 0x5c);

    m_mdb.extentsFileSize  = be32(p + 0x82);
    for (size_t i = 0; i < 6; ++i) {
        m_mdb.extentsExtents[i] = be16(p + 0x86 + i * 2);
    }
    m_mdb.catalogFileSize  = be32(p + 0x92);
    for (size_t i = 0; i < 6; ++i) {
        m_mdb.catalogExtents[i] = be16(p + 0x96 + i * 2);
    }

    if (m_mdb.allocBlockSize == 0 || m_mdb.numAllocBlocks == 0) return false;
    return true;
}

bool MacintoshHFSHandler::parseBootBlock() {
    const auto& raw = m_disk->getRawData();
    if (raw.size() < 0x80) return false;
    const uint8_t* p = raw.data();
    if (p[0] != 'L' || p[1] != 'K') {
        m_bootBlock.present = false;
        return false;
    }
    m_bootBlock.present = true;

    // SPEC §1118: system_name @ 0x0a (Pascal, 16-byte field), finder_name @ 0x1a.
    m_bootBlock.systemName = macRomanToUtf8(readPascalBounded(p + 0x0a, 16, 15));
    m_bootBlock.finderName = macRomanToUtf8(readPascalBounded(p + 0x1a, 16, 15));
    return true;
}

std::vector<uint8_t> MacintoshHFSHandler::readAllocBlocks(uint16_t startBlock,
                                                            uint16_t count) const {
    std::vector<uint8_t> out;
    if (count == 0) return out;
    const auto& raw = m_disk->getRawData();
    const uint64_t base = static_cast<uint64_t>(m_mdb.firstAllocBlock) * 512ULL;
    const uint64_t blockSize = static_cast<uint64_t>(m_mdb.allocBlockSize);
    const uint64_t startOffset = base + static_cast<uint64_t>(startBlock) * blockSize;
    const uint64_t totalBytes = static_cast<uint64_t>(count) * blockSize;
    if (startOffset + totalBytes > raw.size()) {
        // Truncate — caller will re-truncate to logical size anyway.
        if (startOffset >= raw.size()) return out;
        const uint64_t avail = raw.size() - startOffset;
        out.assign(raw.begin() + startOffset, raw.begin() + startOffset + avail);
        return out;
    }
    out.assign(raw.begin() + startOffset, raw.begin() + startOffset + totalBytes);
    return out;
}

// Walk the leaf chain of a B-tree file. The B-tree file is itself stored as
// extents in an HFS allocation-block run (the catalog-file or extents-overflow
// fileExtents from the MDB). Reconstruct the entire B-tree file into outBuffer
// and report the node size from the header record.
bool MacintoshHFSHandler::walkBTreeLeaves(uint32_t /*btreeFileSize*/,
                                            const std::array<uint16_t, 6>& fileExtents,
                                            std::vector<uint8_t>& outBuffer,
                                            uint16_t& outNodeSize) {
    outBuffer.clear();
    outNodeSize = 0;

    // Concatenate the (up to 3) initial extents.
    for (size_t i = 0; i < 3; ++i) {
        const uint16_t start = fileExtents[i * 2];
        const uint16_t count = fileExtents[i * 2 + 1];
        if (count == 0) continue;
        auto chunk = readAllocBlocks(start, count);
        outBuffer.insert(outBuffer.end(), chunk.begin(), chunk.end());
    }
    if (outBuffer.size() < 14 + 8) return false;  // need a node header + record

    // SPEC §1197: B-tree header record begins inside node 0 right after the
    // 14-byte node descriptor. node_size lives at file offset 0x20.
    outNodeSize = be16(outBuffer.data() + 0x20);
    if (outNodeSize == 0 || outNodeSize > 16384) return false;
    if (outBuffer.size() < outNodeSize) return false;
    return true;
}

bool MacintoshHFSHandler::walkCatalogLeaves() {
    std::vector<uint8_t> tree;
    uint16_t nodeSize = 0;
    if (!walkBTreeLeaves(m_mdb.catalogFileSize, m_mdb.catalogExtents, tree, nodeSize)) {
        return false;
    }

    // first leaf node from header
    const uint8_t* hdr = tree.data();
    const uint32_t firstLeaf = be32(hdr + 0x18);
    const uint32_t totalNodes = be32(hdr + 0x24);

    uint32_t node = firstLeaf;
    std::set<uint32_t> visited;
    while (node != 0) {
        if (visited.count(node)) break;            // loop guard
        if (node >= totalNodes) break;
        const size_t nodeOff = static_cast<size_t>(node) * nodeSize;
        if (nodeOff + nodeSize > tree.size()) break;
        const uint8_t* p = tree.data() + nodeOff;
        const int8_t kind = static_cast<int8_t>(p[0x08]);
        if (kind != -1) break;                     // not a leaf
        visited.insert(node);
        parseCatalogLeafNode(p, nodeSize);
        node = be32(p + 0x00);                     // forward link
    }
    return true;
}

bool MacintoshHFSHandler::walkExtentsOverflowLeaves() {
    std::vector<uint8_t> tree;
    uint16_t nodeSize = 0;
    if (!walkBTreeLeaves(m_mdb.extentsFileSize, m_mdb.extentsExtents, tree, nodeSize)) {
        return false;
    }
    const uint8_t* hdr = tree.data();
    const uint32_t firstLeaf = be32(hdr + 0x18);
    const uint32_t totalNodes = be32(hdr + 0x24);

    uint32_t node = firstLeaf;
    std::set<uint32_t> visited;
    while (node != 0) {
        if (visited.count(node)) break;
        if (node >= totalNodes) break;
        const size_t nodeOff = static_cast<size_t>(node) * nodeSize;
        if (nodeOff + nodeSize > tree.size()) break;
        const uint8_t* p = tree.data() + nodeOff;
        const int8_t kind = static_cast<int8_t>(p[0x08]);
        if (kind != -1) break;
        visited.insert(node);
        parseExtentsLeafNode(p, nodeSize);
        node = be32(p + 0x00);
    }
    return true;
}

void MacintoshHFSHandler::parseCatalogLeafNode(const uint8_t* node, size_t nodeSize) {
    const uint16_t numRecords = be16(node + 0x0a);

    // SPEC §1197: record offset table sits at the very end of the node, with
    // numRecords+1 entries (each BE u16). The first entry (closest to the end)
    // is the offset of record 0, the last is the free-space pointer.
    auto recOffset = [&](uint16_t idx) -> uint16_t {
        const size_t pos = nodeSize - 2 * (idx + 1);
        return be16(node + pos);
    };

    for (uint16_t i = 0; i < numRecords; ++i) {
        const uint16_t off = recOffset(i);
        const uint16_t end = recOffset(i + 1);  // start of next record (or free)
        if (off >= nodeSize || end > nodeSize || off >= end) continue;
        const size_t recLen = static_cast<size_t>(end) - off;
        const uint8_t* rec = node + off;

        // Catalog key: keyLen(1) reserved(1) parent_cnid(4) nameLen(1) name(...)
        if (recLen < 8) continue;
        const uint8_t keyLen = rec[0x00];
        if (keyLen + 1U > recLen) continue;
        const uint32_t parentCNID = be32(rec + 0x02);
        const uint8_t nameLen = rec[0x06];
        const size_t nameMax = (keyLen >= 6) ? (keyLen - 6U) : 0;
        const size_t actualNameLen = std::min<size_t>(nameLen, std::min<size_t>(nameMax, 31));
        std::string macRomanName(reinterpret_cast<const char*>(rec + 0x07), actualNameLen);
        const std::string name = macRomanToUtf8(macRomanName);

        // Data offset = 1 + keyLen, padded to even.
        size_t dataOff = 1U + keyLen;
        if ((dataOff & 1U) != 0) dataOff += 1;
        if (dataOff >= recLen) continue;
        const uint8_t* d = rec + dataOff;
        const size_t dataLen = recLen - dataOff;
        if (dataLen < 1) continue;
        const uint8_t recType = d[0];

        CatalogChild child;
        child.name = name;
        child.isDirectory = (recType == REC_FOLDER);

        if (recType == REC_FOLDER) {
            if (dataLen < 0x0a) continue;
            child.cnid = be32(d + 0x06);
            // Folder dates: SPEC does not strictly require but we include
            // create/modify dates at standard HFS folder offsets if present.
            if (dataLen >= 0x12) child.createDate = be32(d + 0x0a);
            if (dataLen >= 0x16) child.modifyDate = be32(d + 0x0e);
        } else if (recType == REC_FILE) {
            if (dataLen < 0x62) continue;
            std::memcpy(child.fileType, d + 0x04, 4);
            std::memcpy(child.creator,  d + 0x08, 4);
            child.cnid         = be32(d + 0x14);
            child.dataLogical  = be32(d + 0x1a);
            child.rsrcLogical  = be32(d + 0x24);
            for (size_t k = 0; k < 6; ++k) child.dataExtents[k] = be16(d + 0x4a + k * 2);
            for (size_t k = 0; k < 6; ++k) child.rsrcExtents[k] = be16(d + 0x56 + k * 2);
            // Standard HFS file record dates live at offsets 0x2e (create) and
            // 0x32 (modify).
            child.createDate = be32(d + 0x2e);
            child.modifyDate = be32(d + 0x32);
        } else {
            continue;  // thread record — skip
        }

        m_childrenByParent[parentCNID].push_back(child);
        m_byCNID[child.cnid] = child;
    }
}

void MacintoshHFSHandler::parseExtentsLeafNode(const uint8_t* node, size_t nodeSize) {
    const uint16_t numRecords = be16(node + 0x0a);
    auto recOffset = [&](uint16_t idx) -> uint16_t {
        const size_t pos = nodeSize - 2 * (idx + 1);
        return be16(node + pos);
    };

    for (uint16_t i = 0; i < numRecords; ++i) {
        const uint16_t off = recOffset(i);
        const uint16_t end = recOffset(i + 1);
        if (off >= nodeSize || end > nodeSize || off >= end) continue;
        const size_t recLen = static_cast<size_t>(end) - off;
        const uint8_t* rec = node + off;

        // Extents key: keyLen(1) forkType(1) cnid(4) startBlock(2)  → 8 bytes total.
        if (recLen < 8) continue;
        const uint8_t keyLen = rec[0x00];
        if (keyLen + 1U > recLen) continue;
        const uint8_t forkType = rec[0x01];
        const uint32_t cnid    = be32(rec + 0x02);
        const uint16_t startBlock = be16(rec + 0x06);

        size_t dataOff = 1U + keyLen;
        if ((dataOff & 1U) != 0) dataOff += 1;
        if (dataOff + 12 > recLen) continue;

        std::array<uint16_t, 6> extents{};
        for (size_t k = 0; k < 6; ++k) {
            extents[k] = be16(rec + dataOff + k * 2);
        }
        ExtentsKey key{cnid, forkType, startBlock};
        m_extentsOverflow[key] = extents;
    }
}

std::vector<uint8_t> MacintoshHFSHandler::extractFork(uint32_t fileCNID,
                                                        uint8_t forkType) const {
    auto it = m_byCNID.find(fileCNID);
    if (it == m_byCNID.end()) return {};
    const CatalogChild& f = it->second;

    const std::array<uint16_t, 6>& initial =
        (forkType == HFS_FORK_DATA) ? f.dataExtents : f.rsrcExtents;
    const uint32_t logical =
        (forkType == HFS_FORK_DATA) ? f.dataLogical : f.rsrcLogical;
    if (logical == 0) return {};
    if (m_mdb.allocBlockSize == 0) return {};

    std::vector<uint8_t> out;
    uint16_t covered = 0;

    auto append = [&](const std::array<uint16_t, 6>& exts) {
        for (size_t i = 0; i < 3; ++i) {
            const uint16_t start = exts[i * 2];
            const uint16_t count = exts[i * 2 + 1];
            if (count == 0) continue;
            auto chunk = readAllocBlocks(start, count);
            out.insert(out.end(), chunk.begin(), chunk.end());
            covered = static_cast<uint16_t>(covered + count);
        }
    };

    append(initial);

    // Fetch overflow extents while logical bytes still missing.
    const uint16_t neededBlocks = static_cast<uint16_t>(
        (logical + m_mdb.allocBlockSize - 1) / m_mdb.allocBlockSize);

    std::set<uint16_t> seen;
    while (covered < neededBlocks) {
        if (seen.count(covered)) break;            // loop guard
        seen.insert(covered);
        ExtentsKey key{fileCNID, forkType, covered};
        auto e = m_extentsOverflow.find(key);
        if (e == m_extentsOverflow.end()) break;
        const uint16_t before = covered;
        append(e->second);
        if (covered == before) break;              // empty record — stop
    }

    if (out.size() > logical) out.resize(logical);
    return out;
}

const MacintoshHFSHandler::CatalogChild*
MacintoshHFSHandler::resolvePath(const std::string& path) const {
    // Normalize: strip leading '/', split on '/'.
    std::string p = path;
    while (!p.empty() && p.front() == '/') p.erase(p.begin());
    if (p.empty()) {
        // Root — synthesize a CatalogChild for the volume folder.
        auto it = m_byCNID.find(HFS_ROOT_CNID);
        return (it != m_byCNID.end()) ? &it->second : nullptr;
    }

    std::vector<std::string> parts;
    {
        std::string cur;
        for (char c : p) {
            if (c == '/') {
                if (!cur.empty()) parts.push_back(cur);
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        if (!cur.empty()) parts.push_back(cur);
    }

    uint32_t parent = HFS_ROOT_CNID;
    const CatalogChild* found = nullptr;
    for (size_t i = 0; i < parts.size(); ++i) {
        auto pit = m_childrenByParent.find(parent);
        if (pit == m_childrenByParent.end()) return nullptr;
        const auto& children = pit->second;
        const CatalogChild* match = nullptr;
        for (const auto& c : children) {
            if (c.name == parts[i]) { match = &c; break; }
        }
        if (!match) return nullptr;
        if (i + 1 < parts.size() && !match->isDirectory) return nullptr;
        parent = match->cnid;
        found = match;
    }
    return found;
}

std::vector<FileEntry> MacintoshHFSHandler::listFiles(const std::string& path) {
    std::vector<FileEntry> out;
    uint32_t parent = HFS_ROOT_CNID;
    if (!path.empty() && path != "/") {
        const CatalogChild* node = resolvePath(path);
        if (!node || !node->isDirectory) return out;
        parent = node->cnid;
    }
    auto it = m_childrenByParent.find(parent);
    if (it == m_childrenByParent.end()) return out;
    for (const auto& c : it->second) {
        FileEntry e;
        e.name = c.name;
        e.size = static_cast<size_t>(c.dataLogical);
        e.isDirectory = c.isDirectory;
        if (c.createDate) e.createdTime  = fromMacEpoch(c.createDate);
        if (c.modifyDate) e.modifiedTime = fromMacEpoch(c.modifyDate);
        out.push_back(e);
    }
    return out;
}

std::vector<uint8_t> MacintoshHFSHandler::readFile(const std::string& filename) {
    const CatalogChild* f = resolvePath(filename);
    if (!f) {
        throw FileNotFoundException(filename);
    }
    if (f->isDirectory) {
        throw InvalidFormatException("HFS readFile: '" + filename + "' is a directory");
    }
    return extractFork(f->cnid, HFS_FORK_DATA);
}

bool MacintoshHFSHandler::writeFile(const std::string&, const std::vector<uint8_t>&,
                                     const FileMetadata&) {
    throw NotImplementedException("Macintosh HFS write support is not yet implemented (Phase 2)");
}
bool MacintoshHFSHandler::deleteFile(const std::string&) {
    throw NotImplementedException("Macintosh HFS delete support is not yet implemented (Phase 2)");
}
bool MacintoshHFSHandler::renameFile(const std::string&, const std::string&) {
    throw NotImplementedException("Macintosh HFS rename support is not yet implemented (Phase 2)");
}
bool MacintoshHFSHandler::format(const std::string&) {
    throw NotImplementedException("Macintosh HFS format support is not yet implemented (Phase 2)");
}

size_t MacintoshHFSHandler::getFreeSpace() const {
    return static_cast<size_t>(m_mdb.freeAllocBlocks) *
           static_cast<size_t>(m_mdb.allocBlockSize);
}

size_t MacintoshHFSHandler::getTotalSpace() const {
    return static_cast<size_t>(m_mdb.numAllocBlocks) *
           static_cast<size_t>(m_mdb.allocBlockSize);
}

bool MacintoshHFSHandler::fileExists(const std::string& filename) const {
    const CatalogChild* f = resolvePath(filename);
    return f != nullptr && !f->isDirectory;
}

std::string MacintoshHFSHandler::getVolumeName() const {
    return m_mdb.volumeName;
}

bool MacintoshHFSHandler::isDirectory(const std::string& path) const {
    const CatalogChild* f = resolvePath(path);
    return f != nullptr && f->isDirectory;
}

} // namespace rde
