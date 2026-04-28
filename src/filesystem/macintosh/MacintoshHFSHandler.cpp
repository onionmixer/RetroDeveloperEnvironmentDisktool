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
    m_mdb.numFiles         = be16(p + 0x0c);          // drNmFls
    m_mdb.bitmapStart      = be16(p + 0x0e);          // drVBMSt
    m_mdb.numAllocBlocks   = be16(p + 0x12);
    m_mdb.allocBlockSize   = be32(p + 0x14);
    m_mdb.firstAllocBlock  = be16(p + 0x1c);
    m_mdb.nextCNID         = be32(p + 0x1e);          // drNxtCNID
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
        child.macRomanName = macRomanName;
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
            // Inside Mac File Manager: HFS file record dates live at offsets
            // 0x2c (create), 0x30 (modify), 0x34 (backup). Verified against
            // Python reference macdiskimage.py:5075.
            child.createDate = be32(d + 0x2c);
            child.modifyDate = be32(d + 0x30);
            // SPEC §1648: AppleDouble entry-id 9 takes 16-byte FInfo from
            // catalog data offset 0x04 followed by 16-byte FXInfo from 0x38.
            std::memcpy(child.finfo,  d + 0x04, 16);
            std::memcpy(child.fxinfo, d + 0x38, 16);
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

const MacintoshHFSHandler::CatalogChild*
MacintoshHFSHandler::lookupByPath(const std::string& path) const {
    return resolvePath(path);
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

// HFS volume bitmap helpers. Bitmap occupies sectors starting at drVBMSt;
// inside each byte the MSB (bit 7) maps to the first allocation block in that
// byte (Inside Mac File Manager). bit==1 means used, 0 means free.
namespace {

inline bool bitmapBit(const std::vector<uint8_t>& raw,
                       uint64_t bitmapByteBase, uint16_t allocBlock) {
    const size_t off = bitmapByteBase + (allocBlock / 8);
    if (off >= raw.size()) return true;  // out of range = treat as used
    const uint8_t mask = static_cast<uint8_t>(1u << (7 - (allocBlock & 7)));
    return (raw[off] & mask) != 0;
}

inline void setBitmapBit(std::vector<uint8_t>& raw,
                          uint64_t bitmapByteBase, uint16_t allocBlock,
                          bool used) {
    const size_t off = bitmapByteBase + (allocBlock / 8);
    if (off >= raw.size()) return;
    const uint8_t mask = static_cast<uint8_t>(1u << (7 - (allocBlock & 7)));
    if (used) raw[off] |= mask;
    else      raw[off] &= static_cast<uint8_t>(~mask);
}

// Compare HFS catalog keys: parent CNID first (BE u32), then case-folded
// (Mac-Roman simple) name byte-wise. Returns <0 / 0 / >0 like memcmp.
int compareCatalogKey(uint32_t parentA, const std::string& nameA,
                       uint32_t parentB, const std::string& nameB) {
    if (parentA != parentB) return (parentA < parentB) ? -1 : 1;
    // Inside Mac uses a Mac-Roman case-insensitive comparison; for the
    // M7 minimal scope (single new file at the root) plain byte comparison
    // is sufficient — we never produce records that conflict with existing
    // ones.
    const size_t n = std::min(nameA.size(), nameB.size());
    for (size_t i = 0; i < n; ++i) {
        unsigned char a = static_cast<unsigned char>(nameA[i]);
        unsigned char b = static_cast<unsigned char>(nameB[i]);
        if (a >= 'a' && a <= 'z') a = static_cast<unsigned char>(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = static_cast<unsigned char>(b - 'a' + 'A');
        if (a != b) return (a < b) ? -1 : 1;
    }
    if (nameA.size() != nameB.size()) {
        return (nameA.size() < nameB.size()) ? -1 : 1;
    }
    return 0;
}

inline void putBE16(std::vector<uint8_t>& raw, size_t off, uint16_t v) {
    raw[off]     = static_cast<uint8_t>((v >> 8) & 0xFF);
    raw[off + 1] = static_cast<uint8_t>(v & 0xFF);
}
inline void putBE32(std::vector<uint8_t>& raw, size_t off, uint32_t v) {
    raw[off]     = static_cast<uint8_t>((v >> 24) & 0xFF);
    raw[off + 1] = static_cast<uint8_t>((v >> 16) & 0xFF);
    raw[off + 2] = static_cast<uint8_t>((v >> 8) & 0xFF);
    raw[off + 3] = static_cast<uint8_t>(v & 0xFF);
}

} // namespace

bool MacintoshHFSHandler::writeFile(const std::string& filename,
                                     const std::vector<uint8_t>& data,
                                     const FileMetadata& metadata) {
    if (!m_disk) return false;
    if (m_disk->isWriteProtected()) {
        throw WriteProtectedException();
    }
    // M7 scope: parent dir = root only. Reject paths with separators.
    std::string leaf = filename;
    while (!leaf.empty() && leaf.front() == '/') leaf.erase(leaf.begin());
    if (leaf.find('/') != std::string::npos) {
        throw NotImplementedException("Macintosh HFS write currently only supports root-level files (no subdirectories)");
    }
    if (leaf.empty() || leaf.size() > 31) {
        return false;
    }
    if (lookupByPath(leaf) != nullptr) {
        return false;  // already exists; caller does delete-then-add
    }
    if (m_mdb.allocBlockSize == 0) return false;

    // Compute needed allocation blocks. Out-of-scope: forks larger than
    // 3 initial extents (= no Extents Overflow B-tree update yet).
    const uint32_t blockSize = m_mdb.allocBlockSize;
    const uint32_t needed = data.empty() ? 0u :
        static_cast<uint32_t>((data.size() + blockSize - 1) / blockSize);
    if (needed > 0xFFFFu) {
        throw NotImplementedException("Macintosh HFS write: data fork too large for a single extent");
    }

    // Snapshot the disk image. All mutations happen in this buffer; we commit
    // atomically via setRawData() at the end.
    std::vector<uint8_t> raw = m_disk->getRawData();

    // 1. Find a contiguous run of free allocation blocks in the volume bitmap.
    const uint64_t bitmapByteBase =
        static_cast<uint64_t>(m_mdb.bitmapStart) * 512ULL;
    uint16_t firstFree = 0;
    uint16_t runStart = 0;
    uint16_t runLen = 0;
    if (needed > 0) {
        uint16_t bestLen = 0;
        for (uint16_t b = 0; b < m_mdb.numAllocBlocks; ++b) {
            if (!bitmapBit(raw, bitmapByteBase, b)) {
                if (bestLen == 0) firstFree = b;
                ++bestLen;
                if (bestLen >= needed) {
                    runStart = firstFree;
                    runLen = bestLen;
                    break;
                }
            } else {
                bestLen = 0;
            }
        }
        if (runLen < needed) {
            throw NotImplementedException(
                "Macintosh HFS write: no contiguous free run for " +
                std::to_string(needed) + " allocation blocks (fragmented free space; "
                "Extents Overflow B-tree updates not implemented yet)");
        }
    }

    // 2. Write the data fork.
    const uint64_t firstAllocByte =
        static_cast<uint64_t>(m_mdb.firstAllocBlock) * 512ULL;
    if (needed > 0) {
        const uint64_t startOff =
            firstAllocByte + static_cast<uint64_t>(runStart) * blockSize;
        if (startOff + static_cast<uint64_t>(runLen) * blockSize > raw.size()) {
            return false;
        }
        for (uint32_t i = 0; i < needed; ++i) {
            const uint64_t off = firstAllocByte +
                static_cast<uint64_t>(runStart + i) * blockSize;
            const size_t srcOff = static_cast<size_t>(i) * blockSize;
            const size_t take = std::min<size_t>(blockSize, data.size() - srcOff);
            std::memcpy(raw.data() + off, data.data() + srcOff, take);
            if (take < blockSize) {
                std::memset(raw.data() + off + take, 0, blockSize - take);
            }
        }
        // Mark the run as used in the volume bitmap.
        for (uint32_t i = 0; i < needed; ++i) {
            setBitmapBit(raw, bitmapByteBase, runStart + i, true);
        }
    }

    // 3. Build the new catalog file record. SPEC §1463 layout.
    const uint32_t newCNID = m_mdb.nextCNID;
    std::vector<uint8_t> recordData(102, 0);  // catalog file record body length
    recordData[0x00] = 0x02;  // recType = file
    // Optional: fileType + creator from metadata are not exposed by the
    // FileSystemHandler interface; defaults to four-zero bytes.
    putBE32(recordData, 0x14, newCNID);
    if (needed > 0) {
        // dataExtents at 0x4a — 3 extents × (start, count). We only fill
        // the first.
        putBE16(recordData, 0x4a, runStart);
        putBE16(recordData, 0x4c, static_cast<uint16_t>(needed));
    }
    putBE32(recordData, 0x1a, static_cast<uint32_t>(data.size()));   // dataLogical
    putBE32(recordData, 0x1e, static_cast<uint32_t>(needed) * blockSize); // dataPhysical
    putBE32(recordData, 0x2c, metadata.timestamp.value_or(0) ?
        static_cast<uint32_t>(*metadata.timestamp + 2082844800LL) : 0);  // create
    putBE32(recordData, 0x30, recordData[0x2c] | (recordData[0x2c+1] << 8));  // modify (same)
    // Restore modify offset properly:
    {
        uint32_t cdate = (static_cast<uint32_t>(recordData[0x2c]) << 24) |
                          (static_cast<uint32_t>(recordData[0x2c + 1]) << 16) |
                          (static_cast<uint32_t>(recordData[0x2c + 2]) << 8) |
                           static_cast<uint32_t>(recordData[0x2c + 3]);
        putBE32(recordData, 0x30, cdate);
    }

    // 4. Build the catalog key: keyLen + reserved + parentCNID + name(p-string).
    constexpr uint32_t HFS_ROOT_CNID = 2;
    const uint32_t parentCNID = HFS_ROOT_CNID;
    const size_t nameLen = leaf.size();
    // keyLen excludes itself: reserved(1) + parentCNID(4) + nameLen(1) + name(N)
    const uint8_t keyLen = static_cast<uint8_t>(6 + nameLen);
    std::vector<uint8_t> recordKey;
    recordKey.reserve(1 + keyLen);
    recordKey.push_back(keyLen);
    recordKey.push_back(0);  // reserved
    recordKey.push_back(static_cast<uint8_t>((parentCNID >> 24) & 0xFF));
    recordKey.push_back(static_cast<uint8_t>((parentCNID >> 16) & 0xFF));
    recordKey.push_back(static_cast<uint8_t>((parentCNID >> 8) & 0xFF));
    recordKey.push_back(static_cast<uint8_t>(parentCNID & 0xFF));
    recordKey.push_back(static_cast<uint8_t>(nameLen));
    recordKey.insert(recordKey.end(), leaf.begin(), leaf.end());

    // 5. Concatenate key + (padding to even) + data.
    std::vector<uint8_t> fullRecord = recordKey;
    if (fullRecord.size() & 1U) fullRecord.push_back(0);
    fullRecord.insert(fullRecord.end(), recordData.begin(), recordData.end());

    // 6. Locate the catalog file's bytes in the disk image. M7 scope:
    // catalog must fit entirely in its initial 3 extents (no overflow).
    if (m_mdb.catalogExtents[1] == 0) {
        throw NotImplementedException("Macintosh HFS write: catalog file is empty");
    }
    // We re-use the same buffer logic as walkBTreeLeaves but rebuild against
    // the post-write state. For minimum-impact approach we only mutate the
    // single leaf node that should host this key.
    std::vector<uint8_t> catalogBytes;
    for (size_t i = 0; i < 3; ++i) {
        const uint16_t start = m_mdb.catalogExtents[i * 2];
        const uint16_t count = m_mdb.catalogExtents[i * 2 + 1];
        if (count == 0) continue;
        const uint64_t off = firstAllocByte +
            static_cast<uint64_t>(start) * blockSize;
        const uint64_t len = static_cast<uint64_t>(count) * blockSize;
        if (off + len > raw.size()) {
            throw NotImplementedException("Macintosh HFS write: catalog extents past EOF");
        }
        catalogBytes.insert(catalogBytes.end(),
            raw.begin() + off, raw.begin() + off + len);
    }
    if (catalogBytes.size() < 14 + 2 * 0x20) {
        throw NotImplementedException("Macintosh HFS write: catalog too small to parse");
    }
    const uint16_t nodeSize = (static_cast<uint16_t>(catalogBytes[0x20]) << 8) |
                                catalogBytes[0x21];
    if (nodeSize == 0 || nodeSize > 16384 ||
        catalogBytes.size() % nodeSize != 0) {
        throw NotImplementedException("Macintosh HFS write: catalog node size not divisible");
    }

    // 7. Walk leaves to find the right leaf node for our key. M7 scope:
    // we only insert when the chosen leaf has enough free space; otherwise
    // we explicitly fail so disk content remains unchanged.
    const uint32_t firstLeaf = (static_cast<uint32_t>(catalogBytes[0x18]) << 24) |
                                (static_cast<uint32_t>(catalogBytes[0x19]) << 16) |
                                (static_cast<uint32_t>(catalogBytes[0x1a]) << 8) |
                                 static_cast<uint32_t>(catalogBytes[0x1b]);
    uint32_t targetNode = firstLeaf;
    while (true) {
        const size_t nodeOff = static_cast<size_t>(targetNode) * nodeSize;
        if (nodeOff + nodeSize > catalogBytes.size()) {
            throw NotImplementedException("Macintosh HFS write: leaf walk out of bounds");
        }
        const uint8_t* p = catalogBytes.data() + nodeOff;
        const uint16_t numRecs = (static_cast<uint16_t>(p[0x0a]) << 8) | p[0x0b];
        if (numRecs == 0) break;
        // Last record's key in this node:
        const uint16_t lastOff =
            (static_cast<uint16_t>(p[nodeSize - 2 * (numRecs - 1) - 1]) |
             (static_cast<uint16_t>(p[nodeSize - 2 * (numRecs - 1) - 2]) << 8));
        const uint8_t lastKeyLen = p[lastOff];
        if (lastOff + 1U + 6U > nodeSize) break;
        const uint32_t lastParent = (static_cast<uint32_t>(p[lastOff + 0x02]) << 24) |
                                     (static_cast<uint32_t>(p[lastOff + 0x03]) << 16) |
                                     (static_cast<uint32_t>(p[lastOff + 0x04]) << 8) |
                                      static_cast<uint32_t>(p[lastOff + 0x05]);
        const uint8_t lastNameLen = p[lastOff + 0x06];
        const std::string lastName(reinterpret_cast<const char*>(p + lastOff + 0x07),
                                    std::min<size_t>(lastNameLen,
                                        static_cast<size_t>(lastKeyLen >= 6 ? lastKeyLen - 6 : 0)));
        if (compareCatalogKey(parentCNID, leaf, lastParent, lastName) <= 0) {
            break;  // our key fits in this node (≤ last key)
        }
        const uint32_t fLink = (static_cast<uint32_t>(p[0x00]) << 24) |
                                (static_cast<uint32_t>(p[0x01]) << 16) |
                                (static_cast<uint32_t>(p[0x02]) << 8) |
                                 static_cast<uint32_t>(p[0x03]);
        if (fLink == 0) break;
        targetNode = fLink;
    }

    // 8. Insert into the chosen leaf node. Key-sorted insertion.
    const size_t nodeOff = static_cast<size_t>(targetNode) * nodeSize;
    uint8_t* p = catalogBytes.data() + nodeOff;
    const uint16_t numRecs = (static_cast<uint16_t>(p[0x0a]) << 8) | p[0x0b];

    // Free space in the node = node end − offset table − last record end.
    const size_t offsetTableBytes = static_cast<size_t>(numRecs + 1) * 2U;
    auto recOff = [&](uint16_t idx) -> uint16_t {
        const size_t pos = nodeSize - 2 * (idx + 1);
        return (static_cast<uint16_t>(p[pos]) << 8) | p[pos + 1];
    };
    const uint16_t freePtr = recOff(numRecs);  // start of free space
    const size_t freeSpace = (nodeSize - offsetTableBytes) - freePtr;
    if (fullRecord.size() + 2 > freeSpace) {
        throw NotImplementedException("Macintosh HFS write: target leaf node full (split not implemented)");
    }

    // Find the insertion index.
    size_t insertIdx = numRecs;
    for (uint16_t i = 0; i < numRecs; ++i) {
        const uint16_t ko = recOff(i);
        const uint8_t kl = p[ko];
        if (ko + 1U + 6U > nodeSize) break;
        const uint32_t pCnid = (static_cast<uint32_t>(p[ko + 0x02]) << 24) |
                                 (static_cast<uint32_t>(p[ko + 0x03]) << 16) |
                                 (static_cast<uint32_t>(p[ko + 0x04]) << 8) |
                                  static_cast<uint32_t>(p[ko + 0x05]);
        const uint8_t nl = p[ko + 0x06];
        const std::string nm(reinterpret_cast<const char*>(p + ko + 0x07),
                              std::min<size_t>(nl,
                                static_cast<size_t>(kl >= 6 ? kl - 6 : 0)));
        if (compareCatalogKey(parentCNID, leaf, pCnid, nm) < 0) {
            insertIdx = i;
            break;
        }
    }

    // Where the new record will start, and what offset records after it have.
    const uint16_t newRecStart = (insertIdx == numRecs) ? freePtr : recOff(static_cast<uint16_t>(insertIdx));
    const size_t shiftLen = freePtr - newRecStart;

    // Move trailing record bytes forward.
    if (shiftLen > 0) {
        std::memmove(p + newRecStart + fullRecord.size(),
                     p + newRecStart, shiftLen);
    }
    // Write the new record.
    std::memcpy(p + newRecStart, fullRecord.data(), fullRecord.size());

    // Rebuild the offset table with the inserted record.
    std::vector<uint16_t> offsets;
    offsets.reserve(numRecs + 2);
    for (uint16_t i = 0; i < numRecs; ++i) offsets.push_back(recOff(i));
    offsets.insert(offsets.begin() + insertIdx,
                   static_cast<uint16_t>(newRecStart));
    // Bump every offset that was after the insertion point.
    for (size_t i = insertIdx + 1; i < offsets.size(); ++i) {
        offsets[i] = static_cast<uint16_t>(offsets[i] + fullRecord.size());
    }
    offsets.push_back(static_cast<uint16_t>(freePtr + fullRecord.size()));  // new free-ptr
    // Write offsets back: the first entry (closest to node end) is record 0.
    for (size_t i = 0; i < offsets.size(); ++i) {
        const size_t pos = nodeSize - 2 * (i + 1);
        p[pos]     = static_cast<uint8_t>((offsets[i] >> 8) & 0xFF);
        p[pos + 1] = static_cast<uint8_t>(offsets[i] & 0xFF);
    }
    // Bump record count.
    putBE16(catalogBytes, nodeOff + 0x0a, static_cast<uint16_t>(numRecs + 1));

    // 9. Spread catalog bytes back into the disk image (extents).
    {
        size_t cursor = 0;
        for (size_t i = 0; i < 3 && cursor < catalogBytes.size(); ++i) {
            const uint16_t start = m_mdb.catalogExtents[i * 2];
            const uint16_t count = m_mdb.catalogExtents[i * 2 + 1];
            if (count == 0) continue;
            const uint64_t off = firstAllocByte +
                static_cast<uint64_t>(start) * blockSize;
            const size_t len = std::min<size_t>(
                static_cast<size_t>(count) * blockSize,
                catalogBytes.size() - cursor);
            std::memcpy(raw.data() + off,
                        catalogBytes.data() + cursor, len);
            cursor += len;
        }
    }

    // 10. Update MDB scalars.
    putBE16(raw, 0x400 + 0x0c, static_cast<uint16_t>(m_mdb.numFiles + 1));
    putBE32(raw, 0x400 + 0x1e, m_mdb.nextCNID + 1);
    putBE16(raw, 0x400 + 0x22,
            static_cast<uint16_t>(m_mdb.freeAllocBlocks - needed));

    // 11. Commit.
    m_disk->setRawData(raw);

    // Refresh caches.
    m_childrenByParent.clear();
    m_byCNID.clear();
    m_extentsOverflow.clear();
    parseMdb();
    walkExtentsOverflowLeaves();
    walkCatalogLeaves();
    return true;
}

bool MacintoshHFSHandler::deleteFile(const std::string& filename) {
    if (!m_disk) return false;
    if (m_disk->isWriteProtected()) {
        throw WriteProtectedException();
    }
    const CatalogChild* victim = lookupByPath(filename);
    if (!victim) return false;
    if (victim->isDirectory) return false;

    // M10 safety: only handle files whose forks fit entirely in their initial
    // 3 catalog extents (no Extents Overflow B-tree updates). Files larger
    // than that surface as NotImplementedException so disk content is never
    // partially mutated.
    const uint32_t blockSize = m_mdb.allocBlockSize;
    if (blockSize == 0) return false;
    auto neededBlocks = [&](uint32_t logical) -> uint32_t {
        return logical == 0 ? 0u :
            static_cast<uint32_t>((logical + blockSize - 1) / blockSize);
    };
    auto extentsCover = [&](const std::array<uint16_t, 6>& ext) -> uint32_t {
        return static_cast<uint32_t>(ext[1]) +
               static_cast<uint32_t>(ext[3]) +
               static_cast<uint32_t>(ext[5]);
    };
    const uint32_t needData = neededBlocks(victim->dataLogical);
    const uint32_t needRsrc = neededBlocks(victim->rsrcLogical);
    if (extentsCover(victim->dataExtents) < needData ||
        extentsCover(victim->rsrcExtents) < needRsrc) {
        throw NotImplementedException(
            "Macintosh HFS delete: file uses Extents Overflow B-tree "
            "(out of M10 scope — fork wider than 3 initial extents)");
    }

    std::vector<uint8_t> raw = m_disk->getRawData();

    // 1. Free both forks in the volume bitmap.
    const uint64_t bitmapByteBase =
        static_cast<uint64_t>(m_mdb.bitmapStart) * 512ULL;
    uint16_t freedBlocks = 0;
    auto freeFork = [&](const std::array<uint16_t, 6>& ext) {
        for (size_t i = 0; i < 3; ++i) {
            const uint16_t start = ext[i * 2];
            const uint16_t count = ext[i * 2 + 1];
            for (uint16_t b = 0; b < count; ++b) {
                setBitmapBit(raw, bitmapByteBase, start + b, false);
                ++freedBlocks;
            }
        }
    };
    freeFork(victim->dataExtents);
    freeFork(victim->rsrcExtents);

    // 2. Locate the catalog leaf node + record by walking the leaf chain.
    const uint64_t firstAllocByte =
        static_cast<uint64_t>(m_mdb.firstAllocBlock) * 512ULL;

    std::vector<uint8_t> catalogBytes;
    for (size_t i = 0; i < 3; ++i) {
        const uint16_t start = m_mdb.catalogExtents[i * 2];
        const uint16_t count = m_mdb.catalogExtents[i * 2 + 1];
        if (count == 0) continue;
        const uint64_t off = firstAllocByte +
            static_cast<uint64_t>(start) * blockSize;
        const uint64_t len = static_cast<uint64_t>(count) * blockSize;
        if (off + len > raw.size()) return false;
        catalogBytes.insert(catalogBytes.end(),
            raw.begin() + off, raw.begin() + off + len);
    }
    if (catalogBytes.size() < 14 + 2 * 0x20) return false;
    const uint16_t nodeSize = (static_cast<uint16_t>(catalogBytes[0x20]) << 8) |
                                catalogBytes[0x21];
    if (nodeSize == 0 || nodeSize > 16384 ||
        catalogBytes.size() % nodeSize != 0) return false;

    const uint32_t firstLeaf = (static_cast<uint32_t>(catalogBytes[0x18]) << 24) |
                                (static_cast<uint32_t>(catalogBytes[0x19]) << 16) |
                                (static_cast<uint32_t>(catalogBytes[0x1a]) << 8) |
                                 static_cast<uint32_t>(catalogBytes[0x1b]);

    bool removed = false;
    uint32_t node = firstLeaf;
    while (node != 0 && !removed) {
        const size_t nodeOff = static_cast<size_t>(node) * nodeSize;
        if (nodeOff + nodeSize > catalogBytes.size()) break;
        uint8_t* p = catalogBytes.data() + nodeOff;
        const uint16_t numRecs = (static_cast<uint16_t>(p[0x0a]) << 8) | p[0x0b];

        auto recOff = [&](uint16_t idx) -> uint16_t {
            const size_t pos = nodeSize - 2 * (idx + 1);
            return (static_cast<uint16_t>(p[pos]) << 8) | p[pos + 1];
        };

        for (uint16_t i = 0; i < numRecs; ++i) {
            const uint16_t off = recOff(i);
            const uint16_t end = recOff(static_cast<uint16_t>(i + 1));
            if (off >= nodeSize || end > nodeSize || off >= end) continue;
            const size_t recLen = static_cast<size_t>(end) - off;
            const uint8_t kl = p[off];
            if (1U + kl > recLen) continue;
            size_t dataOff = 1U + kl;
            if (dataOff & 1U) dataOff += 1;
            if (dataOff >= recLen) continue;
            const uint8_t recType = p[off + dataOff];
            if (recType != 0x02) continue;  // file record only
            const uint32_t cnid =
                (static_cast<uint32_t>(p[off + dataOff + 0x14]) << 24) |
                (static_cast<uint32_t>(p[off + dataOff + 0x15]) << 16) |
                (static_cast<uint32_t>(p[off + dataOff + 0x16]) << 8)  |
                 static_cast<uint32_t>(p[off + dataOff + 0x17]);
            if (cnid != victim->cnid) continue;

            // Found it — drop this record from the node.
            const uint16_t freePtr = recOff(numRecs);
            const size_t shiftLen = freePtr - end;
            if (shiftLen > 0) {
                std::memmove(p + off, p + end, shiftLen);
            }
            // Zero the trailing record bytes (best-effort; not required by HFS).
            std::memset(p + freePtr - recLen, 0, recLen);

            // Rebuild offset table.
            std::vector<uint16_t> offsets;
            offsets.reserve(numRecs);
            for (uint16_t k = 0; k < numRecs; ++k) offsets.push_back(recOff(k));
            offsets.erase(offsets.begin() + i);
            for (size_t k = i; k < offsets.size(); ++k) {
                offsets[k] = static_cast<uint16_t>(offsets[k] - recLen);
            }
            offsets.push_back(static_cast<uint16_t>(freePtr - recLen));  // free ptr
            // Wipe the old offset table region first.
            for (size_t k = 0; k <= numRecs; ++k) {
                const size_t pos = nodeSize - 2 * (k + 1);
                p[pos] = 0;
                p[pos + 1] = 0;
            }
            for (size_t k = 0; k < offsets.size(); ++k) {
                const size_t pos = nodeSize - 2 * (k + 1);
                p[pos]     = static_cast<uint8_t>((offsets[k] >> 8) & 0xFF);
                p[pos + 1] = static_cast<uint8_t>(offsets[k] & 0xFF);
            }
            // Decrement record count.
            putBE16(catalogBytes, nodeOff + 0x0a, static_cast<uint16_t>(numRecs - 1));
            removed = true;
            break;
        }
        if (removed) break;
        node = (static_cast<uint32_t>(p[0x00]) << 24) |
               (static_cast<uint32_t>(p[0x01]) << 16) |
               (static_cast<uint32_t>(p[0x02]) << 8) |
                static_cast<uint32_t>(p[0x03]);
    }
    if (!removed) return false;

    // 3. Write catalog bytes back into the disk image.
    {
        size_t cursor = 0;
        for (size_t i = 0; i < 3 && cursor < catalogBytes.size(); ++i) {
            const uint16_t start = m_mdb.catalogExtents[i * 2];
            const uint16_t count = m_mdb.catalogExtents[i * 2 + 1];
            if (count == 0) continue;
            const uint64_t off = firstAllocByte +
                static_cast<uint64_t>(start) * blockSize;
            const size_t len = std::min<size_t>(
                static_cast<size_t>(count) * blockSize,
                catalogBytes.size() - cursor);
            std::memcpy(raw.data() + off,
                        catalogBytes.data() + cursor, len);
            cursor += len;
        }
    }

    // 4. MDB scalars.
    putBE16(raw, 0x400 + 0x0c, static_cast<uint16_t>(m_mdb.numFiles - 1));
    putBE16(raw, 0x400 + 0x22,
            static_cast<uint16_t>(m_mdb.freeAllocBlocks + freedBlocks));

    m_disk->setRawData(raw);
    m_childrenByParent.clear();
    m_byCNID.clear();
    m_extentsOverflow.clear();
    parseMdb();
    walkExtentsOverflowLeaves();
    walkCatalogLeaves();
    return true;
}

bool MacintoshHFSHandler::renameFile(const std::string& oldName,
                                       const std::string& newName) {
    if (!m_disk) return false;
    if (m_disk->isWriteProtected()) {
        throw WriteProtectedException();
    }
    const CatalogChild* target = lookupByPath(oldName);
    if (!target) return false;
    if (target->isDirectory) {
        throw NotImplementedException("Macintosh HFS rename of folders is out of M10 scope");
    }

    std::string newLeaf = newName;
    while (!newLeaf.empty() && newLeaf.front() == '/') newLeaf.erase(newLeaf.begin());
    if (newLeaf.find('/') != std::string::npos) {
        throw NotImplementedException("Macintosh HFS rename across directories is out of M10 scope");
    }
    if (newLeaf.empty() || newLeaf.size() > 31) return false;
    if (lookupByPath(newLeaf) != nullptr) return false;

    // Rename in HFS = key change. The new key length differs whenever the
    // new name length differs, which means the record size changes and
    // potentially crosses node boundaries. M10 minimal scope: do it as a
    // delete-then-add. This works as long as the leaf still has room and
    // the new name doesn't overflow the catalog. Both branches are already
    // protected by the existing M7/M10 guards.
    //
    // Snapshot the file's data fork before deletion so we can re-create it
    // with the new name. Resource fork support is intentionally limited to
    // empty rsrc forks here (target files added via M7 writeFile have no
    // rsrc fork; existing files with rsrc forks fall through to the
    // out-of-scope branch in deleteFile).
    if (target->rsrcLogical != 0) {
        throw NotImplementedException("Macintosh HFS rename of files with resource forks is out of M10 scope");
    }
    std::vector<uint8_t> dataFork = extractFork(target->cnid, 0x00);
    if (!deleteFile(oldName)) return false;
    FileMetadata md;
    md.targetName = newLeaf;
    return writeFile(newLeaf, dataFork, md);
}
bool MacintoshHFSHandler::format(const std::string&) {
    // HFS format would require materializing every B-tree from scratch
    // (boot blocks + MDB + volume bitmap + Catalog B-tree header & first
    // leaf with the root folder + thread records + Extents Overflow B-tree
    // header). Inside Mac File Manager defines the layout, but no Python
    // reference exists for cross-tool byte-for-byte parity, so a native
    // implementation would have no safety net. Use an external tool to
    // create the blank volume and let rdedisktool read/write it.
    throw NotImplementedException(
        "Macintosh HFS format: not implemented — no reference tool exists "
        "for cross-tool byte parity verification. Create a blank HFS volume "
        "with an emulator (Mini vMac, BasiliskII) or hfsutils (mkfs.hfs) "
        "and rdedisktool will then read/write it.");
}

bool MacintoshHFSHandler::createDirectory(const std::string& /*path*/) {
    // mkdir on HFS requires inserting BOTH a folder record (type 0x01) and
    // its corresponding thread record (type 0x03) into the catalog B-tree.
    // The two records have keys with different parent CNIDs and may land
    // in different leaf nodes — atomic insertion across two leaves with
    // potential split is out of the safe-scope guards M7/M10 carry. Inside
    // Mac File Manager defines the thread record layout but the Python
    // reference (macdiskimage.py) intentionally ignores thread records on
    // read, so cross-tool byte parity cannot be verified for the write side.
    throw NotImplementedException(
        "Macintosh HFS createDirectory: not implemented — requires atomic "
        "insertion of folder + thread records across multiple catalog leaves "
        "with no cross-tool verification path. Out of safe-scope.");
}

bool MacintoshHFSHandler::deleteDirectory(const std::string& /*path*/) {
    // rmdir is the inverse of createDirectory plus a recursive emptiness
    // check. Same rationale as createDirectory — atomic two-leaf mutation
    // with no cross-tool verification, deferred for safety.
    throw NotImplementedException(
        "Macintosh HFS deleteDirectory: not implemented — same constraint "
        "as createDirectory (two-leaf mutation, no cross-tool verification).");
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
