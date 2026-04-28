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

MacintoshHFSHandler::ParentResolved
MacintoshHFSHandler::resolveParentForMutation(const std::string& path) const {
    std::string p = path;
    while (!p.empty() && p.front() == '/') p.erase(p.begin());
    while (!p.empty() && p.back()  == '/') p.pop_back();

    const auto slash = p.rfind('/');
    if (slash == std::string::npos) {
        return {HFS_ROOT_CNID, p};
    }
    const std::string parentPath = p.substr(0, slash);
    const std::string leaf       = p.substr(slash + 1);

    const CatalogChild* parent = resolvePath(parentPath);
    if (!parent || !parent->isDirectory) {
        throw NotImplementedException(
            "Macintosh HFS mutation: parent path '" + parentPath +
            "' does not resolve to an existing folder");
    }
    return {parent->cnid, leaf};
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

// Cleanup helper: keep the BT header rec's `leafRecords` field in sync
// with the actual count of records across all leaves. Pre-cleanup, all
// write paths (M7 / B2 / B3 / C1..C4) silently left this stale; Python
// read walks the leaf chain and ignored it, so the gap was invisible.
// BasiliskII / hfsutils validate the field, so keeping it accurate
// matters for cross-emulator compatibility.
inline void bumpLeafRecordsCount(std::vector<uint8_t>& catalogBytes,
                                   int32_t delta) {
    if (catalogBytes.size() < 14 + 0x0a) return;
    const size_t off = 14 + 0x06;  // BT header rec leafRecords field (u32 BE)
    const uint32_t v =
        (static_cast<uint32_t>(catalogBytes[off    ]) << 24) |
        (static_cast<uint32_t>(catalogBytes[off + 1]) << 16) |
        (static_cast<uint32_t>(catalogBytes[off + 2]) <<  8) |
         static_cast<uint32_t>(catalogBytes[off + 3]);
    int64_t v2 = static_cast<int64_t>(v) + delta;
    if (v2 < 0) v2 = 0;
    if (v2 > 0xFFFFFFFFLL) v2 = 0xFFFFFFFFLL;
    const uint32_t out = static_cast<uint32_t>(v2);
    catalogBytes[off    ] = static_cast<uint8_t>((out >> 24) & 0xFF);
    catalogBytes[off + 1] = static_cast<uint8_t>((out >> 16) & 0xFF);
    catalogBytes[off + 2] = static_cast<uint8_t>((out >>  8) & 0xFF);
    catalogBytes[off + 3] = static_cast<uint8_t>(out & 0xFF);
}

// C4 helper: allocate a free B-tree node by scanning the header node's
// map record (offset 248..nodeSize-8). On success, sets the bit and
// decrements freeNodes in the BT header rec; returns the new node index.
// Returns 0 if no free nodes remain.
inline uint32_t allocateBTreeNodeFromMap(
        std::vector<uint8_t>& catalogBytes,
        uint16_t nodeSize) {
    if (catalogBytes.size() < nodeSize) return 0;
    const size_t mapStart = 248;
    const size_t mapEnd   = nodeSize - 8;  // before offset table

    const uint32_t totalNodes =
        (static_cast<uint32_t>(catalogBytes[14 + 0x16]) << 24) |
        (static_cast<uint32_t>(catalogBytes[14 + 0x17]) << 16) |
        (static_cast<uint32_t>(catalogBytes[14 + 0x18]) <<  8) |
         static_cast<uint32_t>(catalogBytes[14 + 0x19]);
    const uint32_t totalCovered =
        std::min<uint32_t>(totalNodes,
                            static_cast<uint32_t>((mapEnd - mapStart) * 8));

    for (uint32_t b = 1; b < totalCovered; ++b) {  // skip node 0 (header)
        const size_t off = mapStart + (b / 8);
        const uint8_t mask = static_cast<uint8_t>(1u << (7 - (b & 7)));
        if ((catalogBytes[off] & mask) == 0) {
            catalogBytes[off] |= mask;
            // Decrement freeNodes (BT header rec offset 0x1a, u32 BE).
            const uint32_t fn =
                (static_cast<uint32_t>(catalogBytes[14 + 0x1a]) << 24) |
                (static_cast<uint32_t>(catalogBytes[14 + 0x1b]) << 16) |
                (static_cast<uint32_t>(catalogBytes[14 + 0x1c]) <<  8) |
                 static_cast<uint32_t>(catalogBytes[14 + 0x1d]);
            const uint32_t fn2 = fn > 0 ? fn - 1 : 0;
            catalogBytes[14 + 0x1a] = static_cast<uint8_t>((fn2 >> 24) & 0xFF);
            catalogBytes[14 + 0x1b] = static_cast<uint8_t>((fn2 >> 16) & 0xFF);
            catalogBytes[14 + 0x1c] = static_cast<uint8_t>((fn2 >>  8) & 0xFF);
            catalogBytes[14 + 0x1d] = static_cast<uint8_t>(fn2 & 0xFF);
            return b;
        }
    }
    return 0;
}

// C4 helper: build an index-node record `key + 4 byte data (node number)`
// from a leaf record (whose key we copy verbatim). Pad to even total size
// per HFS B-tree convention.
inline std::vector<uint8_t> buildIndexRecord(
        const std::vector<uint8_t>& leafRecord,
        uint32_t childNodeIdx) {
    if (leafRecord.empty()) return {};
    const uint8_t kl = leafRecord[0];
    if (1U + kl > leafRecord.size()) return {};
    std::vector<uint8_t> rec;
    rec.reserve(1 + kl + 1 + 4);
    rec.insert(rec.end(), leafRecord.begin(), leafRecord.begin() + 1 + kl);
    if (rec.size() & 1U) rec.push_back(0);
    rec.push_back(static_cast<uint8_t>((childNodeIdx >> 24) & 0xFF));
    rec.push_back(static_cast<uint8_t>((childNodeIdx >> 16) & 0xFF));
    rec.push_back(static_cast<uint8_t>((childNodeIdx >>  8) & 0xFF));
    rec.push_back(static_cast<uint8_t>(childNodeIdx & 0xFF));
    return rec;
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

// MDB write-side bookkeeping per Inside Mac File Manager:
//   drLsMod   (0x06, u32) — last-modify Mac epoch
//   drWrCnt   (0x46, u32) — write count, +1 each volume update
//   drFilCnt  (0x54, u32) — total files (recursive)
//   drDirCnt  (0x58, u32) — total dirs  (recursive)
//   drNmRtDirs(0x52, u16) — direct root dirs
// These are silently ignored by the current Python read path, so the M7/M10
// initial implementation skipped them. BasiliskII / Mini vMac / hfsutils do
// validate, so we now keep them coherent.
inline void bumpMdbWriteMetadata(std::vector<uint8_t>& raw,
                                  int32_t fileCountDelta,
                                  int32_t dirCountDelta,
                                  int32_t rootDirsDelta,
                                  uint32_t macTimestamp) {
    if (raw.size() < 0x400 + 0x80) return;  // MDB block missing — bail out
    const size_t mdbBase = 0x400;

    auto getBE16 = [&](size_t off) -> uint16_t {
        return static_cast<uint16_t>((raw[off] << 8) | raw[off + 1]);
    };
    auto getBE32 = [&](size_t off) -> uint32_t {
        return (static_cast<uint32_t>(raw[off])     << 24) |
               (static_cast<uint32_t>(raw[off + 1]) << 16) |
               (static_cast<uint32_t>(raw[off + 2]) <<  8) |
                static_cast<uint32_t>(raw[off + 3]);
    };

    putBE32(raw, mdbBase + 0x06, macTimestamp);

    const uint32_t wr = getBE32(mdbBase + 0x46);
    putBE32(raw, mdbBase + 0x46, wr + 1u);

    if (fileCountDelta != 0) {
        const int64_t v = static_cast<int64_t>(getBE32(mdbBase + 0x54)) + fileCountDelta;
        putBE32(raw, mdbBase + 0x54, v < 0 ? 0u : static_cast<uint32_t>(v));
    }
    if (dirCountDelta != 0) {
        const int64_t v = static_cast<int64_t>(getBE32(mdbBase + 0x58)) + dirCountDelta;
        putBE32(raw, mdbBase + 0x58, v < 0 ? 0u : static_cast<uint32_t>(v));
    }
    if (rootDirsDelta != 0) {
        const int32_t v = static_cast<int32_t>(getBE16(mdbBase + 0x52)) + rootDirsDelta;
        putBE16(raw, mdbBase + 0x52, v < 0 ? 0u : static_cast<uint16_t>(v));
    }
}

// Walk catalog leaves searching for the folder record whose body.cnid
// matches `targetCNID` (recType = 0x01). When found, adjust its valence
// (body offset 0x04, BE u16) by `delta`, clamped at [0, 0xFFFF]. Returns
// true if updated, false when no such record exists (some volumes lack an
// explicit folder record — only the thread). Mutates `raw` in place.
//
// For the root folder (CNID=2) the matching record's KEY has parent=1.
// For other folders, the KEY uses the actual parent CNID; we don't filter
// on the key here because body.cnid is unique across the catalog tree.
//
// Body layout per Inside Mac (HFS folder record):
//   0x00 recType (0x01) | 0x01 reserved | 0x02 flags(u16) | 0x04 valence(u16)
//   0x06 cnid(u32)      | 0x0a crDate    | 0x0e mdDate     | 0x12 backup
inline bool applyFolderValenceByCNID(
        std::vector<uint8_t>& raw,
        uint16_t firstAllocBlock,
        uint32_t allocBlockSize,
        const std::array<uint16_t, 6>& catalogExtents,
        uint32_t targetCNID,
        int32_t delta) {
    if (delta == 0) return true;  // nothing to do — treat as success
    if (allocBlockSize == 0) return false;

    // Reconstruct catalog file bytes from its 3 initial extents.
    const uint64_t firstAllocByte =
        static_cast<uint64_t>(firstAllocBlock) * 512ULL;
    std::vector<uint8_t> catalogBytes;
    for (size_t i = 0; i < 3; ++i) {
        const uint16_t start = catalogExtents[i * 2];
        const uint16_t count = catalogExtents[i * 2 + 1];
        if (count == 0) continue;
        const uint64_t off = firstAllocByte +
            static_cast<uint64_t>(start) * allocBlockSize;
        const uint64_t len = static_cast<uint64_t>(count) * allocBlockSize;
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

    uint32_t node = firstLeaf;
    std::set<uint32_t> visited;
    while (node != 0) {
        if (visited.count(node)) break;
        visited.insert(node);
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
            if (kl < 6) continue;
            size_t dataOff = 1U + kl;
            if (dataOff & 1U) dataOff += 1;
            if (dataOff + 0x0a > recLen) continue;  // need recType + valence + cnid
            const uint8_t recType = p[off + dataOff];
            if (recType != 0x01) continue;  // not a folder record
            const uint32_t bodyCNID =
                (static_cast<uint32_t>(p[off + dataOff + 0x06]) << 24) |
                (static_cast<uint32_t>(p[off + dataOff + 0x07]) << 16) |
                (static_cast<uint32_t>(p[off + dataOff + 0x08]) <<  8) |
                 static_cast<uint32_t>(p[off + dataOff + 0x09]);
            if (bodyCNID != targetCNID) continue;

            // Found the folder record. Adjust valence at body+0x04.
            const size_t valenceOff = off + dataOff + 0x04;
            if (valenceOff + 1 >= nodeSize) continue;
            const uint16_t cur =
                (static_cast<uint16_t>(p[valenceOff]) << 8) | p[valenceOff + 1];
            int32_t newVal = static_cast<int32_t>(cur) + delta;
            if (newVal < 0) newVal = 0;
            if (newVal > 0xFFFF) newVal = 0xFFFF;
            p[valenceOff]     = static_cast<uint8_t>((newVal >> 8) & 0xFF);
            p[valenceOff + 1] = static_cast<uint8_t>(newVal & 0xFF);

            // Spread catalog bytes back into the disk image.
            size_t cursor = 0;
            for (size_t e = 0; e < 3 && cursor < catalogBytes.size(); ++e) {
                const uint16_t start = catalogExtents[e * 2];
                const uint16_t count = catalogExtents[e * 2 + 1];
                if (count == 0) continue;
                const uint64_t writeOff = firstAllocByte +
                    static_cast<uint64_t>(start) * allocBlockSize;
                const size_t len = std::min<size_t>(
                    static_cast<size_t>(count) * allocBlockSize,
                    catalogBytes.size() - cursor);
                std::memcpy(raw.data() + writeOff,
                            catalogBytes.data() + cursor, len);
                cursor += len;
            }
            return true;
        }
        node = (static_cast<uint32_t>(p[0x00]) << 24) |
               (static_cast<uint32_t>(p[0x01]) << 16) |
               (static_cast<uint32_t>(p[0x02]) << 8) |
                static_cast<uint32_t>(p[0x03]);
    }
    return false;
}

// C2 helpers — read a folder record's body bytes (70 B) and patch the name
// field of a folder thread record (recType=0x03) in place. Both walk the
// catalog leaf chain by reassembling the catalog file from its initial
// extents, navigate by key, and either copy out body bytes or mutate.

inline bool readFolderRecordBody(
        const std::vector<uint8_t>& raw,
        uint16_t firstAllocBlock,
        uint32_t allocBlockSize,
        const std::array<uint16_t, 6>& catalogExtents,
        uint32_t parentCNID,
        const std::string& name,
        std::vector<uint8_t>& outBody) {
    outBody.clear();
    if (allocBlockSize == 0) return false;

    const uint64_t firstAllocByte =
        static_cast<uint64_t>(firstAllocBlock) * 512ULL;
    std::vector<uint8_t> catalogBytes;
    for (size_t i = 0; i < 3; ++i) {
        const uint16_t start = catalogExtents[i * 2];
        const uint16_t count = catalogExtents[i * 2 + 1];
        if (count == 0) continue;
        const uint64_t off = firstAllocByte +
            static_cast<uint64_t>(start) * allocBlockSize;
        const uint64_t len = static_cast<uint64_t>(count) * allocBlockSize;
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

    uint32_t node = firstLeaf;
    std::set<uint32_t> visited;
    while (node != 0) {
        if (visited.count(node)) break;
        visited.insert(node);
        const size_t nodeOff = static_cast<size_t>(node) * nodeSize;
        if (nodeOff + nodeSize > catalogBytes.size()) break;
        const uint8_t* p = catalogBytes.data() + nodeOff;
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
            if (kl < 6) continue;
            const uint32_t pc =
                (static_cast<uint32_t>(p[off + 0x02]) << 24) |
                (static_cast<uint32_t>(p[off + 0x03]) << 16) |
                (static_cast<uint32_t>(p[off + 0x04]) << 8) |
                 static_cast<uint32_t>(p[off + 0x05]);
            if (pc != parentCNID) continue;
            const uint8_t nl = p[off + 0x06];
            const std::string nm(reinterpret_cast<const char*>(p + off + 0x07),
                                  std::min<size_t>(nl,
                                    static_cast<size_t>(kl >= 6 ? kl - 6 : 0)));
            if (nm != name) continue;

            size_t dataOff = 1U + kl;
            if (dataOff & 1U) dataOff += 1;
            if (dataOff + 70 > recLen) return false;
            const uint8_t recType = p[off + dataOff];
            if (recType != 0x01) return false;  // not a folder record

            outBody.assign(p + off + dataOff, p + off + dataOff + 70);
            return true;
        }
        node = (static_cast<uint32_t>(p[0x00]) << 24) |
               (static_cast<uint32_t>(p[0x01]) << 16) |
               (static_cast<uint32_t>(p[0x02]) << 8) |
                static_cast<uint32_t>(p[0x03]);
    }
    return false;
}

// C3: read a file record's 102-byte body so a rename can preserve FInfo,
// FXInfo, filFlags, backupDate, clpSize across the delete-then-add cycle.
inline bool readFileRecordBody(
        const std::vector<uint8_t>& raw,
        uint16_t firstAllocBlock,
        uint32_t allocBlockSize,
        const std::array<uint16_t, 6>& catalogExtents,
        uint32_t parentCNID,
        const std::string& name,
        std::vector<uint8_t>& outBody) {
    outBody.clear();
    if (allocBlockSize == 0) return false;

    const uint64_t firstAllocByte =
        static_cast<uint64_t>(firstAllocBlock) * 512ULL;
    std::vector<uint8_t> catalogBytes;
    for (size_t i = 0; i < 3; ++i) {
        const uint16_t start = catalogExtents[i * 2];
        const uint16_t count = catalogExtents[i * 2 + 1];
        if (count == 0) continue;
        const uint64_t off = firstAllocByte +
            static_cast<uint64_t>(start) * allocBlockSize;
        const uint64_t len = static_cast<uint64_t>(count) * allocBlockSize;
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

    uint32_t node = firstLeaf;
    std::set<uint32_t> visited;
    while (node != 0) {
        if (visited.count(node)) break;
        visited.insert(node);
        const size_t nodeOff = static_cast<size_t>(node) * nodeSize;
        if (nodeOff + nodeSize > catalogBytes.size()) break;
        const uint8_t* p = catalogBytes.data() + nodeOff;
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
            if (kl < 6) continue;
            const uint32_t pc =
                (static_cast<uint32_t>(p[off + 0x02]) << 24) |
                (static_cast<uint32_t>(p[off + 0x03]) << 16) |
                (static_cast<uint32_t>(p[off + 0x04]) << 8) |
                 static_cast<uint32_t>(p[off + 0x05]);
            if (pc != parentCNID) continue;
            const uint8_t nl = p[off + 0x06];
            const std::string nm(reinterpret_cast<const char*>(p + off + 0x07),
                                  std::min<size_t>(nl,
                                    static_cast<size_t>(kl >= 6 ? kl - 6 : 0)));
            if (nm != name) continue;

            size_t dataOff = 1U + kl;
            if (dataOff & 1U) dataOff += 1;
            if (dataOff + 102 > recLen) return false;
            const uint8_t recType = p[off + dataOff];
            if (recType != 0x02) return false;  // not a file record

            outBody.assign(p + off + dataOff, p + off + dataOff + 102);
            return true;
        }
        node = (static_cast<uint32_t>(p[0x00]) << 24) |
               (static_cast<uint32_t>(p[0x01]) << 16) |
               (static_cast<uint32_t>(p[0x02]) << 8) |
                static_cast<uint32_t>(p[0x03]);
    }
    return false;
}

inline bool patchFolderThreadName(
        std::vector<uint8_t>& raw,
        uint16_t firstAllocBlock,
        uint32_t allocBlockSize,
        const std::array<uint16_t, 6>& catalogExtents,
        uint32_t folderCNID,
        const std::string& newName) {
    if (allocBlockSize == 0) return false;
    if (newName.size() > 31) return false;

    const uint64_t firstAllocByte =
        static_cast<uint64_t>(firstAllocBlock) * 512ULL;
    std::vector<uint8_t> catalogBytes;
    for (size_t i = 0; i < 3; ++i) {
        const uint16_t start = catalogExtents[i * 2];
        const uint16_t count = catalogExtents[i * 2 + 1];
        if (count == 0) continue;
        const uint64_t off = firstAllocByte +
            static_cast<uint64_t>(start) * allocBlockSize;
        const uint64_t len = static_cast<uint64_t>(count) * allocBlockSize;
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

    uint32_t node = firstLeaf;
    std::set<uint32_t> visited;
    while (node != 0) {
        if (visited.count(node)) break;
        visited.insert(node);
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
            if (kl < 6) continue;
            const uint32_t pc =
                (static_cast<uint32_t>(p[off + 0x02]) << 24) |
                (static_cast<uint32_t>(p[off + 0x03]) << 16) |
                (static_cast<uint32_t>(p[off + 0x04]) << 8) |
                 static_cast<uint32_t>(p[off + 0x05]);
            if (pc != folderCNID) continue;
            const uint8_t nameLenInKey = p[off + 0x06];
            if (nameLenInKey != 0) continue;  // need empty name (thread key)

            size_t dataOff = 1U + kl;
            if (dataOff & 1U) dataOff += 1;
            if (dataOff + 46 > recLen) continue;
            const uint8_t recType = p[off + dataOff];
            if (recType != 0x03) continue;  // folder thread only

            // Patch the body name field at body+0x0e..+0x2e (1 length byte +
            // 31 name bytes). Zero-pad past the new name length.
            const size_t nameOff = off + dataOff + 0x0e;
            p[nameOff] = static_cast<uint8_t>(newName.size());
            std::memcpy(p + nameOff + 1, newName.data(), newName.size());
            std::memset(p + nameOff + 1 + newName.size(), 0,
                        31 - newName.size());

            // Spread catalog bytes back into raw.
            size_t cursor = 0;
            for (size_t e = 0; e < 3 && cursor < catalogBytes.size(); ++e) {
                const uint16_t start = catalogExtents[e * 2];
                const uint16_t count = catalogExtents[e * 2 + 1];
                if (count == 0) continue;
                const uint64_t writeOff = firstAllocByte +
                    static_cast<uint64_t>(start) * allocBlockSize;
                const size_t len = std::min<size_t>(
                    static_cast<size_t>(count) * allocBlockSize,
                    catalogBytes.size() - cursor);
                std::memcpy(raw.data() + writeOff,
                            catalogBytes.data() + cursor, len);
                cursor += len;
            }
            return true;
        }
        node = (static_cast<uint32_t>(p[0x00]) << 24) |
               (static_cast<uint32_t>(p[0x01]) << 16) |
               (static_cast<uint32_t>(p[0x02]) << 8) |
                static_cast<uint32_t>(p[0x03]);
    }
    return false;
}

} // namespace

bool MacintoshHFSHandler::writeFile(const std::string& filename,
                                     const std::vector<uint8_t>& data,
                                     const FileMetadata& metadata) {
    if (!m_disk) return false;
    if (m_disk->isWriteProtected()) {
        throw WriteProtectedException();
    }
    // C1: resolve parent path → parent CNID. For pure leaf "Hello.txt"
    // this returns root; for "Sub/Hello.txt" it walks the catalog.
    ParentResolved pr = resolveParentForMutation(filename);
    const uint32_t parentCNID = pr.parentCNID;
    std::string leaf = pr.leafName;
    if (leaf.empty() || leaf.size() > 31) {
        return false;
    }
    if (lookupByPath(filename) != nullptr) {
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
    //    parentCNID was resolved at function entry (C1: nested write).
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
    bool didSplit = false;
    if (fullRecord.size() + 2 > freeSpace) {
        // C4: split the leaf and insert via the dedicated path. Mutates
        // catalogBytes; throws on tree shapes the partial implementation
        // doesn't support.
        if (!splitLeafAndInsertRecord(catalogBytes, nodeSize, targetNode,
                                        parentCNID, leaf, fullRecord)) {
            return false;
        }
        didSplit = true;
    }
    if (!didSplit) {

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

    // C4: if the insertion landed at position 0 of the leaf, sync the
    // parent index entry so its key reflects the new first record.
    if (insertIdx == 0) {
        syncIndexEntryForLeaf(catalogBytes, nodeSize, targetNode);
    }

    } // if (!didSplit)

    // Cleanup: keep BT header rec leafRecords consistent (covers both the
    // direct-insert and split-insert paths above).
    bumpLeafRecordsCount(catalogBytes, +1);

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

    // 10. Update MDB scalars + write-side bookkeeping. Inside Mac File
    //      Manager: drNmFls (0x0c) is the *root-direct* file count, so only
    //      bumps when parentCNID == root; drFilCnt (recursive total) bumps
    //      regardless. The parent folder's valence increments in either
    //      case (root or otherwise).
    if (parentCNID == HFS_ROOT_CNID) {
        putBE16(raw, 0x400 + 0x0c,
                static_cast<uint16_t>(m_mdb.numFiles + 1));
    }
    putBE32(raw, 0x400 + 0x1e, m_mdb.nextCNID + 1);
    putBE16(raw, 0x400 + 0x22,
            static_cast<uint16_t>(m_mdb.freeAllocBlocks - needed));

    const std::time_t unixNow = metadata.timestamp.value_or(std::time(nullptr));
    bumpMdbWriteMetadata(raw, +1, 0, 0, toMacEpoch(unixNow));
    applyFolderValenceByCNID(raw, m_mdb.firstAllocBlock,
                              m_mdb.allocBlockSize,
                              m_mdb.catalogExtents,
                              parentCNID, +1);

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

    // C1: capture the victim's actual parent so MDB / valence updates target
    // the right folder for nested files.
    const ParentResolved pr = resolveParentForMutation(filename);
    const uint32_t victimParent = pr.parentCNID;

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
            // Cleanup: BT header rec leafRecords -1, and if we removed
            // record 0 the leaf's first key changed → sync index entry.
            bumpLeafRecordsCount(catalogBytes, -1);
            if (i == 0) {
                syncIndexEntryForLeaf(catalogBytes, nodeSize, node);
            }
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

    // 4. MDB scalars + write-side bookkeeping. drNmFls (root-direct file
    //    count) only changes when the deleted file lived directly at root.
    if (victimParent == HFS_ROOT_CNID) {
        putBE16(raw, 0x400 + 0x0c,
                static_cast<uint16_t>(m_mdb.numFiles - 1));
    }
    putBE16(raw, 0x400 + 0x22,
            static_cast<uint16_t>(m_mdb.freeAllocBlocks + freedBlocks));

    bumpMdbWriteMetadata(raw, -1, 0, 0, toMacEpoch(std::time(nullptr)));
    applyFolderValenceByCNID(raw, m_mdb.firstAllocBlock,
                              m_mdb.allocBlockSize,
                              m_mdb.catalogExtents,
                              victimParent, -1);

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

    // C2: folders go through the dedicated renameFolder path which preserves
    // the folder's CNID + body bytes (DInfo / DXInfo / valence) so children
    // stay attached.
    if (target->isDirectory) {
        return renameFolder(oldName, newName);
    }

    std::string newLeaf = newName;
    while (!newLeaf.empty() && newLeaf.front() == '/') newLeaf.erase(newLeaf.begin());
    while (!newLeaf.empty() && newLeaf.back()  == '/') newLeaf.pop_back();
    if (newLeaf.find('/') != std::string::npos) {
        throw NotImplementedException("Macintosh HFS rename across directories is out of scope");
    }
    if (newLeaf.empty() || newLeaf.size() > 31) return false;

    // C1: rename preserves the file's parent. Reconstruct the full target
    // path under the same parent as oldName, then delete-then-add.
    ParentResolved oldPR = resolveParentForMutation(oldName);
    std::string newPath;
    if (oldPR.parentCNID == HFS_ROOT_CNID) {
        newPath = newLeaf;
    } else {
        // Strip oldLeaf from oldName and append newLeaf instead.
        std::string trimmed = oldName;
        while (!trimmed.empty() && trimmed.front() == '/') trimmed.erase(trimmed.begin());
        while (!trimmed.empty() && trimmed.back()  == '/') trimmed.pop_back();
        const auto slash = trimmed.rfind('/');
        const std::string parentPath = (slash == std::string::npos)
            ? std::string() : trimmed.substr(0, slash);
        newPath = parentPath.empty() ? newLeaf : (parentPath + "/" + newLeaf);
    }
    if (lookupByPath(newPath) != nullptr) return false;

    // C3: snapshot data + rsrc forks AND the original 102-byte file record
    // body before any mutation, so we can preserve fileType / creator /
    // FInfo / FXInfo / backupDate / clpSize across the delete-then-add cycle.
    std::vector<uint8_t> dataFork = extractFork(target->cnid, 0x00);
    std::vector<uint8_t> rsrcFork;
    if (target->rsrcLogical != 0) {
        rsrcFork = extractFork(target->cnid, 0xFF);
        if (rsrcFork.size() != target->rsrcLogical) {
            throw NotImplementedException(
                "Macintosh HFS rename: rsrc fork extraction incomplete "
                "(file uses Extents Overflow B-tree)");
        }
    }

    std::vector<uint8_t> oldBody;
    {
        const std::vector<uint8_t>& raw0 = m_disk->getRawData();
        if (!readFileRecordBody(raw0, m_mdb.firstAllocBlock,
                                  m_mdb.allocBlockSize, m_mdb.catalogExtents,
                                  oldPR.parentCNID, oldPR.leafName, oldBody)) {
            return false;
        }
    }

    if (!deleteFile(oldName)) return false;
    FileMetadata md;
    md.targetName = newLeaf;
    if (!writeFile(newPath, dataFork, md)) return false;

    // After writeFile, patch the new record's body to restore preserved
    // metadata + (if any) attach the rsrc fork. Mutates a fresh raw
    // snapshot and commits separately. If the rsrc fork doesn't fit in a
    // contiguous run, throws — disk is left with the data-only file.
    if (!rsrcFork.empty() ||
        oldBody[0x02] != 0 || oldBody[0x03] != 0 ||
        std::any_of(oldBody.begin() + 0x04, oldBody.begin() + 0x14,
                     [](uint8_t b){ return b != 0; }) ||
        std::any_of(oldBody.begin() + 0x34, oldBody.begin() + 0x38,
                     [](uint8_t b){ return b != 0; }) ||
        std::any_of(oldBody.begin() + 0x38, oldBody.begin() + 0x4a,
                     [](uint8_t b){ return b != 0; })) {
        std::vector<uint8_t> raw = m_disk->getRawData();
        if (!applyRsrcForkAndMetadataPatch(raw, oldPR.parentCNID, newLeaf,
                                             oldBody, rsrcFork)) {
            return false;
        }
        m_disk->setRawData(raw);
        m_childrenByParent.clear();
        m_byCNID.clear();
        m_extentsOverflow.clear();
        parseMdb();
        walkExtentsOverflowLeaves();
        walkCatalogLeaves();
    }
    return true;
}

bool MacintoshHFSHandler::renameFolder(const std::string& oldName,
                                         const std::string& newName) {
    if (!m_disk) return false;
    if (m_disk->isWriteProtected()) {
        throw WriteProtectedException();
    }

    const CatalogChild* target = lookupByPath(oldName);
    if (!target || !target->isDirectory) return false;
    if (target->cnid == HFS_ROOT_CNID) {
        throw NotImplementedException(
            "Macintosh HFS folder rename: cannot rename the root volume "
            "folder via this path. Re-format with the desired volume name.");
    }

    std::string newLeaf = newName;
    while (!newLeaf.empty() && newLeaf.front() == '/') newLeaf.erase(newLeaf.begin());
    while (!newLeaf.empty() && newLeaf.back()  == '/') newLeaf.pop_back();
    if (newLeaf.find('/') != std::string::npos) {
        throw NotImplementedException(
            "Macintosh HFS folder rename across directories is out of scope");
    }
    if (newLeaf.empty() || newLeaf.size() > 31) return false;
    if (m_mdb.allocBlockSize == 0) return false;

    // Resolve the OLD path's parent + leaf; rename happens within the same
    // parent (no move).
    ParentResolved oldPR = resolveParentForMutation(oldName);
    if (oldPR.leafName == newLeaf) return true;  // no-op rename

    // Ensure the new name doesn't already exist under the same parent.
    std::string newPath;
    {
        std::string trimmed = oldName;
        while (!trimmed.empty() && trimmed.front() == '/') trimmed.erase(trimmed.begin());
        while (!trimmed.empty() && trimmed.back()  == '/') trimmed.pop_back();
        const auto slash = trimmed.rfind('/');
        const std::string parentPath = (slash == std::string::npos)
            ? std::string() : trimmed.substr(0, slash);
        newPath = parentPath.empty() ? newLeaf : (parentPath + "/" + newLeaf);
    }
    if (lookupByPath(newPath) != nullptr) return false;

    std::vector<uint8_t> raw = m_disk->getRawData();

    // 1. Read the old folder record's body bytes (70 B) so we can preserve
    //    cnid, valence, dates, DInfo, DXInfo, reserved on re-insert.
    std::vector<uint8_t> folderBody;
    if (!readFolderRecordBody(raw, m_mdb.firstAllocBlock,
                                m_mdb.allocBlockSize, m_mdb.catalogExtents,
                                oldPR.parentCNID, oldPR.leafName, folderBody)) {
        return false;
    }
    if (folderBody.size() != 70) return false;

    // 2. Build the new folder record (key with new name, body verbatim).
    std::vector<uint8_t> newFolderRecord;
    {
        const uint8_t kl = static_cast<uint8_t>(6 + newLeaf.size());
        newFolderRecord.push_back(kl);
        newFolderRecord.push_back(0);  // reserved
        newFolderRecord.push_back(static_cast<uint8_t>((oldPR.parentCNID >> 24) & 0xFF));
        newFolderRecord.push_back(static_cast<uint8_t>((oldPR.parentCNID >> 16) & 0xFF));
        newFolderRecord.push_back(static_cast<uint8_t>((oldPR.parentCNID >> 8) & 0xFF));
        newFolderRecord.push_back(static_cast<uint8_t>(oldPR.parentCNID & 0xFF));
        newFolderRecord.push_back(static_cast<uint8_t>(newLeaf.size()));
        newFolderRecord.insert(newFolderRecord.end(),
                                newLeaf.begin(), newLeaf.end());
        if (newFolderRecord.size() & 1U) newFolderRecord.push_back(0);
        newFolderRecord.insert(newFolderRecord.end(),
                                folderBody.begin(), folderBody.end());
    }

    // 3. Remove the old folder record.
    if (!removeCatalogLeafRecord(raw, oldPR.parentCNID, oldPR.leafName)) {
        return false;
    }

    // 4. Insert the new folder record. If this throws (leaf full), `raw` is
    //    discarded — disk untouched.
    if (!insertCatalogLeafRecord(raw, oldPR.parentCNID, newLeaf, newFolderRecord)) {
        return false;
    }

    // 5. Patch the folder's thread record body.name in place. The thread
    //    record's KEY (parent=folder.cnid, name="") is invariant during a
    //    rename, so this is size-neutral and can never push the leaf over.
    if (!patchFolderThreadName(raw, m_mdb.firstAllocBlock,
                                 m_mdb.allocBlockSize, m_mdb.catalogExtents,
                                 target->cnid, newLeaf)) {
        throw NotImplementedException(
            "Macintosh HFS folder rename: thread record missing — catalog "
            "inconsistent (volume may have been written by a tool that "
            "omits thread records).");
    }

    // 6. MDB write-side bookkeeping (no count change — rename is an in-place
    //    rewrite). Just bump drLsMod / drWrCnt.
    bumpMdbWriteMetadata(raw, 0, 0, 0, toMacEpoch(std::time(nullptr)));

    // 7. Commit + refresh.
    m_disk->setRawData(raw);
    m_childrenByParent.clear();
    m_byCNID.clear();
    m_extentsOverflow.clear();
    parseMdb();
    walkExtentsOverflowLeaves();
    walkCatalogLeaves();
    return true;
}

// C3: after writeFile() has created a fresh file record (data fork only,
// zeros for FInfo / FXInfo / etc.), patch the new record so it carries
//   (a) the rsrc fork bytes from the renamed file
//   (b) the preserved metadata fields from the old file's body
// Throws NotImplementedException when the rsrc fork doesn't fit in a
// contiguous run of free allocation blocks (Extents Overflow B-tree write
// remains deferred).
bool MacintoshHFSHandler::applyRsrcForkAndMetadataPatch(
        std::vector<uint8_t>& raw,
        uint32_t parentCNID,
        const std::string& leaf,
        const std::vector<uint8_t>& oldBody,
        const std::vector<uint8_t>& rsrcFork) {
    if (m_mdb.allocBlockSize == 0) return false;
    if (oldBody.size() != 102) return false;

    const uint32_t blockSize = m_mdb.allocBlockSize;
    const uint64_t firstAllocByte =
        static_cast<uint64_t>(m_mdb.firstAllocBlock) * 512ULL;
    const uint64_t bitmapByteBase =
        static_cast<uint64_t>(m_mdb.bitmapStart) * 512ULL;

    // 1. Allocate a contiguous run for the rsrc fork (if non-empty).
    uint16_t rsrcStart = 0;
    uint32_t rsrcBlocks = 0;
    if (!rsrcFork.empty()) {
        rsrcBlocks = static_cast<uint32_t>(
            (rsrcFork.size() + blockSize - 1) / blockSize);
        if (rsrcBlocks > 0xFFFFu) {
            throw NotImplementedException(
                "Macintosh HFS rename: rsrc fork too large for a single extent");
        }
        uint16_t firstFree = 0;
        uint16_t bestLen = 0;
        bool found = false;
        for (uint16_t b = 0; b < m_mdb.numAllocBlocks; ++b) {
            if (!bitmapBit(raw, bitmapByteBase, b)) {
                if (bestLen == 0) firstFree = b;
                ++bestLen;
                if (bestLen >= rsrcBlocks) {
                    rsrcStart = firstFree;
                    found = true;
                    break;
                }
            } else {
                bestLen = 0;
            }
        }
        if (!found) {
            throw NotImplementedException(
                "Macintosh HFS rename: no contiguous free run for the "
                "rsrc fork (Extents Overflow B-tree update not implemented)");
        }
        // Write rsrc bytes + mark bitmap.
        for (uint32_t i = 0; i < rsrcBlocks; ++i) {
            const uint64_t off = firstAllocByte +
                static_cast<uint64_t>(rsrcStart + i) * blockSize;
            const size_t srcOff = static_cast<size_t>(i) * blockSize;
            const size_t take = std::min<size_t>(blockSize,
                                                  rsrcFork.size() - srcOff);
            std::memcpy(raw.data() + off, rsrcFork.data() + srcOff, take);
            if (take < blockSize) {
                std::memset(raw.data() + off + take, 0, blockSize - take);
            }
        }
        for (uint32_t i = 0; i < rsrcBlocks; ++i) {
            setBitmapBit(raw, bitmapByteBase, rsrcStart + i, true);
        }
    }

    // 2. Locate the new record in the catalog and patch its body bytes
    //    in place. The record was just inserted by writeFile so it lives
    //    in the same parent leaf; key (parentCNID, leaf) finds it.
    std::vector<uint8_t> catalogBytes;
    for (size_t i = 0; i < 3; ++i) {
        const uint16_t s = m_mdb.catalogExtents[i * 2];
        const uint16_t c = m_mdb.catalogExtents[i * 2 + 1];
        if (c == 0) continue;
        const uint64_t off = firstAllocByte +
            static_cast<uint64_t>(s) * blockSize;
        const uint64_t len = static_cast<uint64_t>(c) * blockSize;
        if (off + len > raw.size()) return false;
        catalogBytes.insert(catalogBytes.end(),
            raw.begin() + off, raw.begin() + off + len);
    }
    if (catalogBytes.size() < 14 + 2 * 0x20) return false;
    const uint16_t nodeSize =
        (static_cast<uint16_t>(catalogBytes[0x20]) << 8) | catalogBytes[0x21];
    if (nodeSize == 0 || nodeSize > 16384 ||
        catalogBytes.size() % nodeSize != 0) return false;

    const uint32_t firstLeaf = (static_cast<uint32_t>(catalogBytes[0x18]) << 24) |
                                (static_cast<uint32_t>(catalogBytes[0x19]) << 16) |
                                (static_cast<uint32_t>(catalogBytes[0x1a]) << 8) |
                                 static_cast<uint32_t>(catalogBytes[0x1b]);

    bool patched = false;
    uint32_t node = firstLeaf;
    std::set<uint32_t> visited;
    while (node != 0 && !patched) {
        if (visited.count(node)) break;
        visited.insert(node);
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
            if (kl < 6) continue;
            const uint32_t pc =
                (static_cast<uint32_t>(p[off + 0x02]) << 24) |
                (static_cast<uint32_t>(p[off + 0x03]) << 16) |
                (static_cast<uint32_t>(p[off + 0x04]) << 8) |
                 static_cast<uint32_t>(p[off + 0x05]);
            if (pc != parentCNID) continue;
            const uint8_t nl = p[off + 0x06];
            const std::string nm(reinterpret_cast<const char*>(p + off + 0x07),
                                  std::min<size_t>(nl,
                                    static_cast<size_t>(kl >= 6 ? kl - 6 : 0)));
            if (nm != leaf) continue;
            size_t dataOff = 1U + kl;
            if (dataOff & 1U) dataOff += 1;
            if (dataOff + 102 > recLen) continue;
            const uint8_t recType = p[off + dataOff];
            if (recType != 0x02) continue;

            uint8_t* body = p + off + dataOff;

            // Restore preserved metadata from oldBody.
            //   0x02..0x03  filFlags + filTyp
            //   0x04..0x13  FInfo (fileType, creator, finder flags, etc.)
            //   0x34..0x37  filBkDat (backup date)
            //   0x38..0x47  FXInfo
            //   0x48..0x49  filClpSize
            std::memcpy(body + 0x02, oldBody.data() + 0x02, 0x12);
            std::memcpy(body + 0x34, oldBody.data() + 0x34, 0x04);
            std::memcpy(body + 0x38, oldBody.data() + 0x38, 0x12);

            // Set the rsrc fork fields.
            body[0x22] = static_cast<uint8_t>((rsrcStart >> 8) & 0xFF);
            body[0x23] = static_cast<uint8_t>(rsrcStart & 0xFF);
            const uint32_t rsrcLogical =
                static_cast<uint32_t>(rsrcFork.size());
            const uint32_t rsrcPhysical = rsrcBlocks * blockSize;
            body[0x24] = static_cast<uint8_t>((rsrcLogical >> 24) & 0xFF);
            body[0x25] = static_cast<uint8_t>((rsrcLogical >> 16) & 0xFF);
            body[0x26] = static_cast<uint8_t>((rsrcLogical >> 8) & 0xFF);
            body[0x27] = static_cast<uint8_t>(rsrcLogical & 0xFF);
            body[0x28] = static_cast<uint8_t>((rsrcPhysical >> 24) & 0xFF);
            body[0x29] = static_cast<uint8_t>((rsrcPhysical >> 16) & 0xFF);
            body[0x2a] = static_cast<uint8_t>((rsrcPhysical >> 8) & 0xFF);
            body[0x2b] = static_cast<uint8_t>(rsrcPhysical & 0xFF);
            // rsrcExtents[0] at body+0x56
            body[0x56] = body[0x22];
            body[0x57] = body[0x23];
            body[0x58] = static_cast<uint8_t>((rsrcBlocks >> 8) & 0xFF);
            body[0x59] = static_cast<uint8_t>(rsrcBlocks & 0xFF);
            // rsrcExtents[1..2] stay zero (allocated by writeFile).

            patched = true;
            break;
        }
        if (patched) break;
        node = (static_cast<uint32_t>(p[0x00]) << 24) |
               (static_cast<uint32_t>(p[0x01]) << 16) |
               (static_cast<uint32_t>(p[0x02]) << 8) |
                static_cast<uint32_t>(p[0x03]);
    }
    if (!patched) return false;

    // Spread catalog bytes back into raw.
    size_t cursor = 0;
    for (size_t i = 0; i < 3 && cursor < catalogBytes.size(); ++i) {
        const uint16_t s = m_mdb.catalogExtents[i * 2];
        const uint16_t c = m_mdb.catalogExtents[i * 2 + 1];
        if (c == 0) continue;
        const uint64_t off = firstAllocByte +
            static_cast<uint64_t>(s) * blockSize;
        const size_t len = std::min<size_t>(
            static_cast<size_t>(c) * blockSize,
            catalogBytes.size() - cursor);
        std::memcpy(raw.data() + off,
                    catalogBytes.data() + cursor, len);
        cursor += len;
    }

    // 3. MDB drFreeBks decrement for rsrc fork blocks consumed.
    if (rsrcBlocks > 0) {
        // Re-read the live drFreeBks (writeFile already decremented for
        // data fork, so we cannot use m_mdb.freeAllocBlocks which is still
        // pre-write).
        const uint16_t live =
            (static_cast<uint16_t>(raw[0x400 + 0x22]) << 8) | raw[0x400 + 0x23];
        const uint16_t newFree =
            static_cast<uint16_t>(live - rsrcBlocks);
        raw[0x400 + 0x22] = static_cast<uint8_t>((newFree >> 8) & 0xFF);
        raw[0x400 + 0x23] = static_cast<uint8_t>(newFree & 0xFF);
        bumpMdbWriteMetadata(raw, 0, 0, 0, toMacEpoch(std::time(nullptr)));
    }
    return true;
}

bool MacintoshHFSHandler::format(const std::string& volumeName) {
    if (!m_disk) return false;
    if (m_disk->isWriteProtected()) {
        throw WriteProtectedException();
    }

    std::vector<uint8_t> raw = m_disk->getRawData();
    const size_t totalBytes = raw.size();
    if (totalBytes == 0 || (totalBytes % 512) != 0) {
        throw InvalidFormatException(
            "Macintosh HFS format: disk image size must be a non-zero "
            "multiple of 512 bytes");
    }
    const size_t numSectors = totalBytes / 512;

    // rdedisktool only formats classic Mac floppy geometries: 800K
    // (1600 sectors) and 1440K (2880 sectors). Larger HFS volumes are out
    // of scope by design — they need wider bitmaps and/or a non-512
    // drAlBlkSiz, and the legitimate use cases (hard disks, partitions)
    // are covered by emulator-native tools (BasiliskII / Mini vMac /
    // hfsutils mkfs.hfs).
    if (numSectors != 1600 && numSectors != 2880) {
        throw NotImplementedException(
            "Macintosh HFS format: only 800K (1600 sectors) and 1440K "
            "(2880 sectors) Mac floppy geometries are supported. Larger "
            "volumes are out of scope — use BasiliskII / Mini vMac / "
            "hfsutils to create them.");
    }

    // Volume name (clamp to 27 MacRoman bytes per Inside Mac drVN limit).
    std::string name = volumeName;
    if (name.empty()) name = "Untitled";
    if (name.size() > 27) name.resize(27);

    // Layout constants (verified against stuffit_expander_5.5.img).
    constexpr uint32_t allocBlkSiz       = 512;
    constexpr uint16_t kBitmapStart      = 3;
    constexpr uint16_t kAlBlSt           = 4;
    constexpr uint16_t kBTreeBlocks      = 22;
    constexpr uint32_t kBTreeFileSize    = kBTreeBlocks * allocBlkSiz;  // 11264
    constexpr uint16_t kNodeSize         = 512;
    constexpr uint16_t kAlternateMdbTail = 2;
    constexpr uint32_t kRootCNID         = 2;
    constexpr uint32_t kFirstUserCNID    = 16;

    const uint16_t numAllocBlocks =
        static_cast<uint16_t>(numSectors - kAlBlSt - kAlternateMdbTail);
    const uint16_t freeAllocBlocks =
        static_cast<uint16_t>(numAllocBlocks - 2 * kBTreeBlocks);

    // Wipe everything — we own the entire image.
    std::fill(raw.begin(), raw.end(), 0);

    // --- 0. Boot block scaffolding (sectors 0..1 = 1024 bytes) -------------
    // Write the standard System 6/7 boot block header layout observed in
    // 608_SystemTools.img: LK signature, BRA.W to entry 0x08a, sample-default
    // version/flags/heap counts, and the canonical Pascal name fields with
    // 0x20 (space) padding. The boot loader area itself stores only a
    // BRA.S * (`60 fe`) at the entry — a halt loop. This keeps the volume
    // "boot-shaped" so the boot disk policy will activate once System and
    // Finder files are added, without shipping a real Mac boot loader.
    {
        uint8_t* bb = raw.data();
        // bbID — magic
        bb[0x000] = 'L'; bb[0x001] = 'K';
        // bbEntry — BRA.W (60 00) with displacement 0x0086 → entry at 0x08a
        bb[0x002] = 0x60; bb[0x003] = 0x00;
        bb[0x004] = 0x00; bb[0x005] = 0x86;
        // bbVersion (sample value)
        bb[0x006] = 0x00; bb[0x007] = 0x17;
        // bbPageFlags = 0 (already zero)

        auto writePascal16 = [&](size_t off, const char* s) {
            const size_t n = std::strlen(s);
            bb[off] = static_cast<uint8_t>(n);
            std::memcpy(bb + off + 1, s, n);
            // Pad the remainder of the 16-byte field with spaces (0x20),
            // matching Apple sample boot blocks.
            std::memset(bb + off + 1 + n, 0x20, 15 - n);
        };
        writePascal16(0x00a, "System");
        writePascal16(0x01a, "Finder");
        writePascal16(0x02a, "Macsbug");
        writePascal16(0x03a, "Disassembler");
        writePascal16(0x04a, "StartUpScreen");
        writePascal16(0x05a, "Finder");
        writePascal16(0x06a, "Clipboard File");

        // bbCntFCBs / bbCntEvts (sample defaults).
        bb[0x07a] = 0x00; bb[0x07b] = 0x0a;   // 10
        bb[0x07c] = 0x00; bb[0x07d] = 0x14;   // 20

        // Halt loader at entry point: BRA.S * (branches to itself) so a Mac
        // ROM that reads & jumps here just spins instead of executing
        // garbage.
        bb[0x08a] = 0x60; bb[0x08b] = 0xfe;
    }

    const uint32_t macNow = toMacEpoch(std::time(nullptr));

    // --- 1. MDB at sector 2 (file offset 0x400) -----------------------------
    putBE16(raw, 0x400 + 0x00, 0x4244);                  // drSigWord "BD"
    putBE32(raw, 0x400 + 0x02, macNow);                  // drCrDate
    putBE32(raw, 0x400 + 0x06, macNow);                  // drLsMod
    putBE16(raw, 0x400 + 0x0a, 0x0100);                  // drAtrb (cleanly unmounted)
    putBE16(raw, 0x400 + 0x0c, 0);                       // drNmFls
    putBE16(raw, 0x400 + 0x0e, kBitmapStart);            // drVBMSt
    putBE16(raw, 0x400 + 0x10, 0);                       // drAllocPtr
    putBE16(raw, 0x400 + 0x12, numAllocBlocks);          // drNmAlBlks
    putBE32(raw, 0x400 + 0x14, allocBlkSiz);             // drAlBlkSiz
    putBE32(raw, 0x400 + 0x18, 2048);                    // drClpSiz (file clump)
    putBE16(raw, 0x400 + 0x1c, kAlBlSt);                 // drAlBlSt
    putBE32(raw, 0x400 + 0x1e, kFirstUserCNID);          // drNxtCNID
    putBE16(raw, 0x400 + 0x22, freeAllocBlocks);         // drFreeBks

    // drVN @ 0x24: Pascal volume name (length + chars).
    raw[0x400 + 0x24] = static_cast<uint8_t>(name.size());
    std::memcpy(raw.data() + 0x400 + 0x25, name.data(), name.size());

    // drVolBkUp / drVSeqNum already zero.
    putBE32(raw, 0x400 + 0x46, 1);                       // drWrCnt
    putBE32(raw, 0x400 + 0x4a, kBTreeFileSize);          // drXTClpSiz
    putBE32(raw, 0x400 + 0x4e, kBTreeFileSize);          // drCTClpSiz
    // drNmRtDirs / drFilCnt / drDirCnt / drFndrInfo / drVCSize / drVBMCSize
    // / drCtlCSize all zero.
    putBE32(raw, 0x400 + 0x82, kBTreeFileSize);          // drXTFlSize
    putBE16(raw, 0x400 + 0x86, 0);                       // drXTExtRec[0].start
    putBE16(raw, 0x400 + 0x88, kBTreeBlocks);            // drXTExtRec[0].count
    putBE32(raw, 0x400 + 0x92, kBTreeFileSize);          // drCTFlSize
    putBE16(raw, 0x400 + 0x96, kBTreeBlocks);            // drCTExtRec[0].start
    putBE16(raw, 0x400 + 0x98, kBTreeBlocks);            // drCTExtRec[0].count

    // --- 2. Volume bitmap at sector 3 (1 sector) ----------------------------
    // First 44 alloc blocks (extents file 0..21 + catalog file 22..43) are
    // "used"; rest free. Bit 7 of byte 0 = block 0 (MSB-first, Inside Mac).
    {
        const size_t bitmapBase = static_cast<size_t>(kBitmapStart) * 512;
        const uint16_t usedBlocks = 2 * kBTreeBlocks;  // 44
        for (uint16_t b = 0; b < usedBlocks; ++b) {
            const size_t off = bitmapBase + (b / 8);
            const uint8_t mask = static_cast<uint8_t>(1u << (7 - (b & 7)));
            raw[off] |= mask;
        }
    }

    // --- 3. Helper: write a B-tree header node ------------------------------
    auto writeBTreeHeaderNode = [&](size_t nodeOff,
                                       uint16_t treeDepth,
                                       uint32_t rootNode,
                                       uint32_t leafRecords,
                                       uint32_t firstLeaf,
                                       uint32_t lastLeaf,
                                       uint16_t maxKeyLen,
                                       uint32_t freeNodes,
                                       uint16_t mapByte0) {
        // BTNodeDescriptor.
        // fLink, bLink already zero.
        raw[nodeOff + 0x08] = 0x01;                          // kind = header
        // height already zero.
        putBE16(raw, nodeOff + 0x0a, 3);                     // numRecs = 3
        // BTHeaderRec at offset 14.
        putBE16(raw, nodeOff + 14 + 0x00, treeDepth);
        putBE32(raw, nodeOff + 14 + 0x02, rootNode);
        putBE32(raw, nodeOff + 14 + 0x06, leafRecords);
        putBE32(raw, nodeOff + 14 + 0x0a, firstLeaf);
        putBE32(raw, nodeOff + 14 + 0x0e, lastLeaf);
        putBE16(raw, nodeOff + 14 + 0x12, kNodeSize);
        putBE16(raw, nodeOff + 14 + 0x14, maxKeyLen);
        putBE32(raw, nodeOff + 14 + 0x16, kBTreeBlocks);     // totalNodes
        putBE32(raw, nodeOff + 14 + 0x1a, freeNodes);
        // reserved1, clumpSize, btreeType, reserved2, attributes, reserved3
        // all zero (matches sample stuffit_expander_5.5.img).

        // Map record: byte 0 has bits set for nodes 0..k that are in use.
        raw[nodeOff + 248] = static_cast<uint8_t>(mapByte0);

        // Offset table at end (4 entries: 3 records + free ptr).
        putBE16(raw, nodeOff + kNodeSize - 2, 14);            // rec[0]
        putBE16(raw, nodeOff + kNodeSize - 4, 120);           // rec[1] (user data)
        putBE16(raw, nodeOff + kNodeSize - 6, 248);           // rec[2] (map)
        putBE16(raw, nodeOff + kNodeSize - 8, 504);           // free ptr
    };

    // --- 4. Extents Overflow B-tree (alloc blocks 0..21) --------------------
    const size_t extentsFileOff =
        static_cast<size_t>(kAlBlSt) * 512;  // 0x800
    writeBTreeHeaderNode(extentsFileOff,
                          /*treeDepth*/    0,
                          /*rootNode*/     0,
                          /*leafRecords*/  0,
                          /*firstLeaf*/    0,
                          /*lastLeaf*/     0,
                          /*maxKeyLen*/    7,
                          /*freeNodes*/    kBTreeBlocks - 1,
                          /*mapByte0*/     0x80);

    // --- 5. Catalog B-tree (alloc blocks 22..43) ----------------------------
    const size_t catalogFileOff =
        extentsFileOff + static_cast<size_t>(kBTreeBlocks) * allocBlkSiz;
    writeBTreeHeaderNode(catalogFileOff,
                          /*treeDepth*/    1,
                          /*rootNode*/     1,
                          /*leafRecords*/  2,
                          /*firstLeaf*/    1,
                          /*lastLeaf*/     1,
                          /*maxKeyLen*/    37,
                          /*freeNodes*/    kBTreeBlocks - 2,
                          /*mapByte0*/     0xc0);

    // Catalog leaf node (node 1): root folder record + root thread record.
    {
        const size_t leafOff = catalogFileOff + kNodeSize;
        // BTNodeDescriptor: leaf, height 1, numRecs 2.
        raw[leafOff + 0x08] = 0xff;          // kind = leaf (-1)
        raw[leafOff + 0x09] = 0x01;          // height = 1
        putBE16(raw, leafOff + 0x0a, 2);

        // Root folder record at offset 14:
        //   key:  keyLen(1) reserved(1) parent=1(4) nameLen(1) name(N)
        //   body: 70-byte folder record (recType=0x01).
        const uint8_t folderKeyLen =
            static_cast<uint8_t>(6 + name.size());
        const size_t folderRecStart = leafOff + 14;
        raw[folderRecStart] = folderKeyLen;
        // raw[folderRecStart+1] = 0;         // reserved
        putBE32(raw, folderRecStart + 2, 1); // parent = 1 (drives root key)
        raw[folderRecStart + 6] =
            static_cast<uint8_t>(name.size());
        std::memcpy(raw.data() + folderRecStart + 7,
                    name.data(), name.size());

        size_t folderBodyOff = folderRecStart + 1 + folderKeyLen;
        if (folderBodyOff & 1U) folderBodyOff += 1;  // align body to even
        raw[folderBodyOff + 0x00] = 0x01;            // recType = folder
        // 0x02..0x03 flags=0; 0x04..0x05 valence=0
        putBE32(raw, folderBodyOff + 0x06, kRootCNID);  // cnid = 2
        putBE32(raw, folderBodyOff + 0x0a, macNow);     // create
        putBE32(raw, folderBodyOff + 0x0e, macNow);     // modify
        // 0x12 backup, 0x16 DInfo[16], 0x26 DXInfo[16], 0x36 reserved[16] = 0
        const size_t folderRecEnd = folderBodyOff + 70;

        // Root thread record. Sample uses keyLen=7 (one extra zero pad byte
        // in the key) so that the body falls on an even offset without an
        // explicit padding step. Match the sample for byte-for-byte parity.
        const size_t threadRecStart = folderRecEnd;
        raw[threadRecStart + 0] = 7;                 // keyLen
        // raw[+1] = 0; reserved
        putBE32(raw, threadRecStart + 2, kRootCNID); // parent = 2
        // raw[+6] = 0; nameLen = 0
        // raw[+7] = 0; pad

        const size_t threadBodyOff = threadRecStart + 8;  // already even
        raw[threadBodyOff + 0x00] = 0x03;            // recType = folder thread
        // 0x01..0x09 reserved (9 bytes total reserved per Inside Mac
        // CatThreadRec — see PLAN_MACFDD.md §22.2 sample hexdump).
        putBE32(raw, threadBodyOff + 0x0a, kRootCNID);  // thdParID
        raw[threadBodyOff + 0x0e] =
            static_cast<uint8_t>(name.size());
        std::memcpy(raw.data() + threadBodyOff + 0x0f,
                    name.data(),
                    std::min<size_t>(name.size(), 31));
        const size_t threadRecEnd = threadBodyOff + 46;

        // Offset table: [folderRecOff, threadRecOff, freePtr].
        putBE16(raw, leafOff + kNodeSize - 2,
                static_cast<uint16_t>(folderRecStart - leafOff));
        putBE16(raw, leafOff + kNodeSize - 4,
                static_cast<uint16_t>(threadRecStart - leafOff));
        putBE16(raw, leafOff + kNodeSize - 6,
                static_cast<uint16_t>(threadRecEnd  - leafOff));
    }

    // --- 6. Commit + refresh -----------------------------------------------
    m_disk->setRawData(raw);
    m_childrenByParent.clear();
    m_byCNID.clear();
    m_extentsOverflow.clear();
    m_bootBlock = BootBlock{};
    if (!parseMdb()) return false;
    parseBootBlock();
    walkExtentsOverflowLeaves();
    if (!walkCatalogLeaves()) return false;
    return true;
}

// B2: single-leaf catalog inserter used by createDirectory. Throws
// NotImplementedException when the target leaf is full (split required) or
// the catalog is structurally unparseable. Returns false only when MDB /
// extents are degenerate (e.g., empty catalog).
bool MacintoshHFSHandler::insertCatalogLeafRecord(
        std::vector<uint8_t>& raw,
        uint32_t parentCNID,
        const std::string& name,
        const std::vector<uint8_t>& fullRecord) {
    if (m_mdb.allocBlockSize == 0) return false;
    if (m_mdb.catalogExtents[1] == 0) return false;

    const uint32_t blockSize = m_mdb.allocBlockSize;
    const uint64_t firstAllocByte =
        static_cast<uint64_t>(m_mdb.firstAllocBlock) * 512ULL;

    // Reassemble catalog file bytes from its initial 3 extents.
    std::vector<uint8_t> catalogBytes;
    for (size_t i = 0; i < 3; ++i) {
        const uint16_t start = m_mdb.catalogExtents[i * 2];
        const uint16_t count = m_mdb.catalogExtents[i * 2 + 1];
        if (count == 0) continue;
        const uint64_t off = firstAllocByte +
            static_cast<uint64_t>(start) * blockSize;
        const uint64_t len = static_cast<uint64_t>(count) * blockSize;
        if (off + len > raw.size()) {
            throw NotImplementedException("HFS catalog extents past EOF");
        }
        catalogBytes.insert(catalogBytes.end(),
            raw.begin() + off, raw.begin() + off + len);
    }
    if (catalogBytes.size() < 14 + 2 * 0x20) {
        throw NotImplementedException("HFS catalog too small to parse");
    }
    const uint16_t nodeSize = (static_cast<uint16_t>(catalogBytes[0x20]) << 8) |
                                catalogBytes[0x21];
    if (nodeSize == 0 || nodeSize > 16384 ||
        catalogBytes.size() % nodeSize != 0) {
        throw NotImplementedException("HFS catalog node size invalid");
    }

    const uint32_t firstLeaf = (static_cast<uint32_t>(catalogBytes[0x18]) << 24) |
                                (static_cast<uint32_t>(catalogBytes[0x19]) << 16) |
                                (static_cast<uint32_t>(catalogBytes[0x1a]) << 8) |
                                 static_cast<uint32_t>(catalogBytes[0x1b]);

    // Walk leaf chain to find the leaf whose last key ≥ our key.
    uint32_t targetNode = firstLeaf;
    while (true) {
        const size_t nodeOff = static_cast<size_t>(targetNode) * nodeSize;
        if (nodeOff + nodeSize > catalogBytes.size()) {
            throw NotImplementedException("HFS catalog leaf walk OOB");
        }
        const uint8_t* p = catalogBytes.data() + nodeOff;
        const uint16_t numRecs = (static_cast<uint16_t>(p[0x0a]) << 8) | p[0x0b];
        if (numRecs == 0) break;
        const uint16_t lastOff =
            (static_cast<uint16_t>(p[nodeSize - 2 * (numRecs - 1) - 1]) |
             (static_cast<uint16_t>(p[nodeSize - 2 * (numRecs - 1) - 2]) << 8));
        const uint8_t lastKeyLen = p[lastOff];
        if (lastOff + 1U + 6U > nodeSize) break;
        const uint32_t lastParent =
            (static_cast<uint32_t>(p[lastOff + 0x02]) << 24) |
            (static_cast<uint32_t>(p[lastOff + 0x03]) << 16) |
            (static_cast<uint32_t>(p[lastOff + 0x04]) << 8) |
             static_cast<uint32_t>(p[lastOff + 0x05]);
        const uint8_t lastNameLen = p[lastOff + 0x06];
        const std::string lastName(reinterpret_cast<const char*>(p + lastOff + 0x07),
                                    std::min<size_t>(lastNameLen,
                                        static_cast<size_t>(lastKeyLen >= 6 ? lastKeyLen - 6 : 0)));
        if (compareCatalogKey(parentCNID, name, lastParent, lastName) <= 0) {
            break;
        }
        const uint32_t fLink = (static_cast<uint32_t>(p[0x00]) << 24) |
                                (static_cast<uint32_t>(p[0x01]) << 16) |
                                (static_cast<uint32_t>(p[0x02]) << 8) |
                                 static_cast<uint32_t>(p[0x03]);
        if (fLink == 0) break;
        targetNode = fLink;
    }

    // Insert into chosen leaf (key-sorted).
    const size_t nodeOff = static_cast<size_t>(targetNode) * nodeSize;
    uint8_t* p = catalogBytes.data() + nodeOff;
    const uint16_t numRecs = (static_cast<uint16_t>(p[0x0a]) << 8) | p[0x0b];

    const size_t offsetTableBytes = static_cast<size_t>(numRecs + 1) * 2U;
    auto recOff = [&](uint16_t idx) -> uint16_t {
        const size_t pos = nodeSize - 2 * (idx + 1);
        return (static_cast<uint16_t>(p[pos]) << 8) | p[pos + 1];
    };
    const uint16_t freePtr = recOff(numRecs);
    const size_t freeSpace = (nodeSize - offsetTableBytes) - freePtr;
    // +2 for the new offset table entry.
    if (fullRecord.size() + 2 > freeSpace) {
        // C4: split the leaf and insert into the appropriate half. Mutates
        // catalogBytes; throws NotImplementedException on tree shapes the
        // split path doesn't handle (depth>2, parent index also full, etc.)
        if (!splitLeafAndInsertRecord(catalogBytes, nodeSize, targetNode,
                                        parentCNID, name, fullRecord)) {
            return false;
        }
        // Cleanup: bump leafRecords (+1 for the new record).
        bumpLeafRecordsCount(catalogBytes, +1);
        // Spread the post-split catalog back into raw and return.
        size_t cursor = 0;
        for (size_t i = 0; i < 3 && cursor < catalogBytes.size(); ++i) {
            const uint16_t s = m_mdb.catalogExtents[i * 2];
            const uint16_t c = m_mdb.catalogExtents[i * 2 + 1];
            if (c == 0) continue;
            const uint64_t off = firstAllocByte +
                static_cast<uint64_t>(s) * blockSize;
            const size_t len = std::min<size_t>(
                static_cast<size_t>(c) * blockSize,
                catalogBytes.size() - cursor);
            std::memcpy(raw.data() + off,
                        catalogBytes.data() + cursor, len);
            cursor += len;
        }
        return true;
    }

    size_t insertIdx = numRecs;
    for (uint16_t i = 0; i < numRecs; ++i) {
        const uint16_t ko = recOff(i);
        const uint8_t kl = p[ko];
        if (ko + 1U + 6U > nodeSize) break;
        const uint32_t pCnid =
            (static_cast<uint32_t>(p[ko + 0x02]) << 24) |
            (static_cast<uint32_t>(p[ko + 0x03]) << 16) |
            (static_cast<uint32_t>(p[ko + 0x04]) << 8) |
             static_cast<uint32_t>(p[ko + 0x05]);
        const uint8_t nl = p[ko + 0x06];
        const std::string nm(reinterpret_cast<const char*>(p + ko + 0x07),
                              std::min<size_t>(nl,
                                static_cast<size_t>(kl >= 6 ? kl - 6 : 0)));
        if (compareCatalogKey(parentCNID, name, pCnid, nm) < 0) {
            insertIdx = i;
            break;
        }
    }

    const uint16_t newRecStart = (insertIdx == numRecs) ?
        freePtr : recOff(static_cast<uint16_t>(insertIdx));
    const size_t shiftLen = freePtr - newRecStart;
    if (shiftLen > 0) {
        std::memmove(p + newRecStart + fullRecord.size(),
                     p + newRecStart, shiftLen);
    }
    std::memcpy(p + newRecStart, fullRecord.data(), fullRecord.size());

    std::vector<uint16_t> offsets;
    offsets.reserve(numRecs + 2);
    for (uint16_t i = 0; i < numRecs; ++i) offsets.push_back(recOff(i));
    offsets.insert(offsets.begin() + insertIdx,
                   static_cast<uint16_t>(newRecStart));
    for (size_t i = insertIdx + 1; i < offsets.size(); ++i) {
        offsets[i] = static_cast<uint16_t>(offsets[i] + fullRecord.size());
    }
    offsets.push_back(static_cast<uint16_t>(freePtr + fullRecord.size()));
    for (size_t i = 0; i < offsets.size(); ++i) {
        const size_t pos = nodeSize - 2 * (i + 1);
        p[pos]     = static_cast<uint8_t>((offsets[i] >> 8) & 0xFF);
        p[pos + 1] = static_cast<uint8_t>(offsets[i] & 0xFF);
    }
    putBE16(catalogBytes, nodeOff + 0x0a, static_cast<uint16_t>(numRecs + 1));

    // C4: if the insertion landed at position 0 of the leaf, the leaf's
    // first key changed; sync the parent index entry.
    if (insertIdx == 0) {
        syncIndexEntryForLeaf(catalogBytes, nodeSize, targetNode);
    }
    // Cleanup: bump leafRecords for the direct-insert path.
    bumpLeafRecordsCount(catalogBytes, +1);

    // Spread catalog bytes back into raw.
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
    return true;
}

// C4: split a full leaf node and insert a record. Operates on the
// in-memory catalog file buffer (caller spreads back to raw). Throws
// NotImplementedException for tree shapes the partial implementation
// doesn't support (treeDepth>2, root-index also full, OOB nodes, etc.).
bool MacintoshHFSHandler::splitLeafAndInsertRecord(
        std::vector<uint8_t>& catalogBytes,
        uint16_t nodeSize,
        uint32_t fullLeafNode,
        uint32_t parentCNID,
        const std::string& name,
        const std::vector<uint8_t>& fullRecord) {
    if (catalogBytes.size() < nodeSize) return false;

    auto getU32 = [&](size_t off) -> uint32_t {
        return (static_cast<uint32_t>(catalogBytes[off    ]) << 24) |
               (static_cast<uint32_t>(catalogBytes[off + 1]) << 16) |
               (static_cast<uint32_t>(catalogBytes[off + 2]) <<  8) |
                static_cast<uint32_t>(catalogBytes[off + 3]);
    };
    auto putU32 = [&](size_t off, uint32_t v) {
        catalogBytes[off    ] = static_cast<uint8_t>((v >> 24) & 0xFF);
        catalogBytes[off + 1] = static_cast<uint8_t>((v >> 16) & 0xFF);
        catalogBytes[off + 2] = static_cast<uint8_t>((v >>  8) & 0xFF);
        catalogBytes[off + 3] = static_cast<uint8_t>(v & 0xFF);
    };

    // BT header rec lives in node 0 starting at byte 14.
    const size_t bth = 14;
    const uint16_t treeDepth =
        static_cast<uint16_t>((catalogBytes[bth] << 8) | catalogBytes[bth + 1]);
    const uint32_t rootNode  = getU32(bth + 0x02);
    const uint32_t lastLeaf  = getU32(bth + 0x0e);
    const uint32_t leafRecs  = getU32(bth + 0x06);

    if (treeDepth != 1 && treeDepth != 2) {
        throw NotImplementedException(
            "Macintosh HFS leaf split: tree depth > 2 unsupported "
            "(needs cascading index split)");
    }

    // 1. Read the full leaf's records into a list of byte vectors.
    const size_t oldOff = static_cast<size_t>(fullLeafNode) * nodeSize;
    if (oldOff + nodeSize > catalogBytes.size()) {
        throw NotImplementedException("HFS leaf split: full leaf node OOB");
    }
    const uint16_t oldNumRecs =
        static_cast<uint16_t>((catalogBytes[oldOff + 0x0a] << 8) |
                                catalogBytes[oldOff + 0x0b]);
    const uint32_t oldFLink = getU32(oldOff + 0x00);
    const uint32_t oldBLink = getU32(oldOff + 0x04);

    auto recOff = [&](const uint8_t* p, uint16_t idx) -> uint16_t {
        const size_t pos = nodeSize - 2 * (idx + 1);
        return static_cast<uint16_t>((p[pos] << 8) | p[pos + 1]);
    };
    std::vector<std::vector<uint8_t>> oldRecs;
    oldRecs.reserve(oldNumRecs);
    {
        const uint8_t* p = catalogBytes.data() + oldOff;
        for (uint16_t i = 0; i < oldNumRecs; ++i) {
            const uint16_t off = recOff(p, i);
            const uint16_t end = recOff(p, static_cast<uint16_t>(i + 1));
            if (off >= nodeSize || end > nodeSize || off >= end) {
                throw NotImplementedException("HFS leaf split: malformed offset table");
            }
            oldRecs.emplace_back(p + off, p + end);
        }
    }

    // 2. Find the insertion point for the new record (key-sorted).
    size_t insertIdx = oldNumRecs;
    for (size_t i = 0; i < oldRecs.size(); ++i) {
        const auto& r = oldRecs[i];
        if (r.size() < 7) continue;
        const uint8_t kl = r[0];
        if (kl < 6) continue;
        const uint32_t pc =
            (static_cast<uint32_t>(r[0x02]) << 24) |
            (static_cast<uint32_t>(r[0x03]) << 16) |
            (static_cast<uint32_t>(r[0x04]) <<  8) |
             static_cast<uint32_t>(r[0x05]);
        const uint8_t nl = r[0x06];
        const std::string nm(reinterpret_cast<const char*>(r.data() + 0x07),
                              std::min<size_t>(nl,
                                static_cast<size_t>(kl >= 6 ? kl - 6 : 0)));
        if (compareCatalogKey(parentCNID, name, pc, nm) < 0) {
            insertIdx = i;
            break;
        }
    }

    // 3. Build the merged record list and partition ~50/50 by count.
    std::vector<std::vector<uint8_t>> all = oldRecs;
    all.insert(all.begin() + insertIdx, fullRecord);
    const size_t total = all.size();
    if (total < 2) return false;
    const size_t splitIdx = total / 2;
    if (splitIdx == 0 || splitIdx >= total) return false;

    std::vector<std::vector<uint8_t>> leftRecs(all.begin(), all.begin() + splitIdx);
    std::vector<std::vector<uint8_t>> rightRecs(all.begin() + splitIdx, all.end());

    // 4. Allocate the new leaf node.
    const uint32_t newLeafNode = allocateBTreeNodeFromMap(catalogBytes, nodeSize);
    if (newLeafNode == 0) {
        throw NotImplementedException(
            "Macintosh HFS leaf split: B-tree map has no free nodes");
    }
    const size_t newOff = static_cast<size_t>(newLeafNode) * nodeSize;
    if (newOff + nodeSize > catalogBytes.size()) {
        throw NotImplementedException(
            "Macintosh HFS leaf split: new leaf node beyond catalog file size");
    }

    auto packLeaf = [&](size_t nodeBase,
                         uint32_t fLink, uint32_t bLink,
                         const std::vector<std::vector<uint8_t>>& recs) -> bool {
        std::memset(catalogBytes.data() + nodeBase, 0, nodeSize);
        // Descriptor.
        putU32(nodeBase + 0x00, fLink);
        putU32(nodeBase + 0x04, bLink);
        catalogBytes[nodeBase + 0x08] = 0xff;  // kind = leaf
        catalogBytes[nodeBase + 0x09] = 0x01;  // height = 1
        catalogBytes[nodeBase + 0x0a] =
            static_cast<uint8_t>((recs.size() >> 8) & 0xFF);
        catalogBytes[nodeBase + 0x0b] =
            static_cast<uint8_t>(recs.size() & 0xFF);
        // Pack records starting at offset 14.
        size_t cursor = 14;
        std::vector<uint16_t> offsets;
        offsets.reserve(recs.size() + 1);
        offsets.push_back(static_cast<uint16_t>(cursor));
        for (const auto& r : recs) {
            if (cursor + r.size() > nodeSize - 2 * (recs.size() + 1)) {
                return false;  // doesn't fit even after split
            }
            std::memcpy(catalogBytes.data() + nodeBase + cursor,
                        r.data(), r.size());
            cursor += r.size();
            offsets.push_back(static_cast<uint16_t>(cursor));
        }
        for (size_t i = 0; i < offsets.size(); ++i) {
            const size_t pos = nodeSize - 2 * (i + 1);
            catalogBytes[nodeBase + pos] =
                static_cast<uint8_t>((offsets[i] >> 8) & 0xFF);
            catalogBytes[nodeBase + pos + 1] =
                static_cast<uint8_t>(offsets[i] & 0xFF);
        }
        return true;
    };

    // 5. Repack old leaf with the left half + new leaf with the right half.
    if (!packLeaf(oldOff, /*fLink=*/newLeafNode, /*bLink=*/oldBLink, leftRecs)) {
        throw NotImplementedException(
            "Macintosh HFS leaf split: left half too large for one node");
    }
    if (!packLeaf(newOff, /*fLink=*/oldFLink,    /*bLink=*/fullLeafNode, rightRecs)) {
        throw NotImplementedException(
            "Macintosh HFS leaf split: right half too large for one node");
    }

    // 6. Patch old fLink target's bLink to point at the new leaf.
    if (oldFLink != 0) {
        const size_t f = static_cast<size_t>(oldFLink) * nodeSize;
        if (f + nodeSize <= catalogBytes.size()) {
            putU32(f + 0x04, newLeafNode);
        }
    }

    // 7. Update header.lastLeaf if applicable. Note: the BT header rec's
    //    `leafRecords` field is intentionally left untouched here for
    //    consistency with the existing M7 / B2 / B3 / C1..C3 insert paths,
    //    none of which update it. Python read walks the leaf chain rather
    //    than trusting the field. Bringing it into sync with reality is
    //    a separate cleanup PR.
    (void)leafRecs;
    if (oldFLink == 0 || lastLeaf == fullLeafNode) {
        putU32(bth + 0x0e, newLeafNode);
    }

    // 8. Propagate to parent.
    if (treeDepth == 1) {
        // Root WAS the leaf. Allocate a new index node and set it as root.
        const uint32_t newIdx = allocateBTreeNodeFromMap(catalogBytes, nodeSize);
        if (newIdx == 0) {
            throw NotImplementedException(
                "Macintosh HFS leaf split: no free node for new root index");
        }
        const size_t idxOff = static_cast<size_t>(newIdx) * nodeSize;
        if (idxOff + nodeSize > catalogBytes.size()) {
            throw NotImplementedException("HFS leaf split: new index node OOB");
        }
        std::memset(catalogBytes.data() + idxOff, 0, nodeSize);
        catalogBytes[idxOff + 0x08] = 0x00;  // kind = index
        catalogBytes[idxOff + 0x09] = 0x02;  // height = 2 (above leaves)
        catalogBytes[idxOff + 0x0a] = 0x00;
        catalogBytes[idxOff + 0x0b] = 0x02;  // numRecs = 2

        std::vector<uint8_t> r1 = buildIndexRecord(leftRecs.front(),  fullLeafNode);
        std::vector<uint8_t> r2 = buildIndexRecord(rightRecs.front(), newLeafNode);
        if (r1.empty() || r2.empty()) return false;

        size_t cursor = 14;
        std::memcpy(catalogBytes.data() + idxOff + cursor, r1.data(), r1.size());
        cursor += r1.size();
        const uint16_t off2 = static_cast<uint16_t>(cursor);
        std::memcpy(catalogBytes.data() + idxOff + cursor, r2.data(), r2.size());
        cursor += r2.size();
        const uint16_t off3 = static_cast<uint16_t>(cursor);

        // Offset table: [14, off2, off3]
        const size_t base = idxOff + nodeSize;
        catalogBytes[base - 2] = 0x00; catalogBytes[base - 1] = 14;
        catalogBytes[base - 4] = static_cast<uint8_t>((off2 >> 8) & 0xFF);
        catalogBytes[base - 3] = static_cast<uint8_t>(off2 & 0xFF);
        catalogBytes[base - 6] = static_cast<uint8_t>((off3 >> 8) & 0xFF);
        catalogBytes[base - 5] = static_cast<uint8_t>(off3 & 0xFF);

        // Update header: rootNode = newIdx, treeDepth = 2.
        catalogBytes[bth + 0x00] = 0x00;
        catalogBytes[bth + 0x01] = 0x02;
        putU32(bth + 0x02, newIdx);
    } else {
        // treeDepth == 2: existing root index. The split has two effects:
        //   (a) the left leaf's first key may have changed (insertion at
        //       front) — handled below via syncIndexEntryForLeaf
        //   (b) a new leaf is born — a new entry must be inserted (here)
        const size_t idxOff = static_cast<size_t>(rootNode) * nodeSize;
        if (idxOff + nodeSize > catalogBytes.size()) {
            throw NotImplementedException("HFS leaf split: root index OOB");
        }

        // (a) Sync the existing entry for the left (still fullLeafNode).
        syncIndexEntryForLeaf(catalogBytes, nodeSize, fullLeafNode);

        // (b) Insert (firstKey of new leaf, newLeafNode), sorted by key.
        const uint16_t idxNumRecs =
            static_cast<uint16_t>((catalogBytes[idxOff + 0x0a] << 8) |
                                    catalogBytes[idxOff + 0x0b]);
        auto idxRecOff = [&](uint16_t i) -> uint16_t {
            const size_t pos = idxOff + nodeSize - 2 * (i + 1);
            return static_cast<uint16_t>(
                (catalogBytes[pos] << 8) | catalogBytes[pos + 1]);
        };
        const uint16_t idxFreePtr = idxRecOff(idxNumRecs);
        const size_t idxOTSize = static_cast<size_t>(idxNumRecs + 1) * 2U;
        const size_t idxFreeSpace =
            (nodeSize - idxOTSize) - idxFreePtr;

        std::vector<uint8_t> idxRec = buildIndexRecord(rightRecs.front(),
                                                         newLeafNode);
        if (idxRec.empty()) return false;
        if (idxRec.size() + 2 > idxFreeSpace) {
            throw NotImplementedException(
                "Macintosh HFS leaf split: root index also full "
                "(cascading index split deferred — out of scope)");
        }

        // Find sorted insert position in the index.
        const uint8_t* idxP = catalogBytes.data() + idxOff;
        size_t idxInsertIdx = idxNumRecs;
        for (uint16_t i = 0; i < idxNumRecs; ++i) {
            const uint16_t ko = idxRecOff(i);
            if (ko + 1U + 6U > nodeSize) break;
            const uint8_t kl = idxP[ko];
            if (kl < 6) continue;
            const uint32_t pc =
                (static_cast<uint32_t>(idxP[ko + 0x02]) << 24) |
                (static_cast<uint32_t>(idxP[ko + 0x03]) << 16) |
                (static_cast<uint32_t>(idxP[ko + 0x04]) <<  8) |
                 static_cast<uint32_t>(idxP[ko + 0x05]);
            const uint8_t nl = idxP[ko + 0x06];
            const std::string nm(
                reinterpret_cast<const char*>(idxP + ko + 0x07),
                std::min<size_t>(nl,
                    static_cast<size_t>(kl >= 6 ? kl - 6 : 0)));
            // Compare against the new leaf's first key.
            const auto& firstRight = rightRecs.front();
            const uint8_t newKl = firstRight[0];
            const uint32_t newPc =
                (static_cast<uint32_t>(firstRight[0x02]) << 24) |
                (static_cast<uint32_t>(firstRight[0x03]) << 16) |
                (static_cast<uint32_t>(firstRight[0x04]) <<  8) |
                 static_cast<uint32_t>(firstRight[0x05]);
            const uint8_t newNl = firstRight[0x06];
            const std::string newNm(
                reinterpret_cast<const char*>(firstRight.data() + 0x07),
                std::min<size_t>(newNl,
                    static_cast<size_t>(newKl >= 6 ? newKl - 6 : 0)));
            if (compareCatalogKey(newPc, newNm, pc, nm) < 0) {
                idxInsertIdx = i;
                break;
            }
        }

        const uint16_t insertOff = (idxInsertIdx == idxNumRecs) ?
            idxFreePtr : idxRecOff(static_cast<uint16_t>(idxInsertIdx));
        const size_t shiftLen = idxFreePtr - insertOff;
        if (shiftLen > 0) {
            std::memmove(catalogBytes.data() + idxOff + insertOff + idxRec.size(),
                         catalogBytes.data() + idxOff + insertOff,
                         shiftLen);
        }
        std::memcpy(catalogBytes.data() + idxOff + insertOff,
                    idxRec.data(), idxRec.size());

        std::vector<uint16_t> idxOffsets;
        idxOffsets.reserve(idxNumRecs + 2);
        for (uint16_t i = 0; i < idxNumRecs; ++i) idxOffsets.push_back(idxRecOff(i));
        idxOffsets.insert(idxOffsets.begin() + idxInsertIdx, insertOff);
        for (size_t i = idxInsertIdx + 1; i < idxOffsets.size(); ++i) {
            idxOffsets[i] = static_cast<uint16_t>(idxOffsets[i] + idxRec.size());
        }
        idxOffsets.push_back(static_cast<uint16_t>(idxFreePtr + idxRec.size()));
        for (size_t i = 0; i < idxOffsets.size(); ++i) {
            const size_t pos = idxOff + nodeSize - 2 * (i + 1);
            catalogBytes[pos]     =
                static_cast<uint8_t>((idxOffsets[i] >> 8) & 0xFF);
            catalogBytes[pos + 1] =
                static_cast<uint8_t>(idxOffsets[i] & 0xFF);
        }
        catalogBytes[idxOff + 0x0a] =
            static_cast<uint8_t>(((idxNumRecs + 1) >> 8) & 0xFF);
        catalogBytes[idxOff + 0x0b] =
            static_cast<uint8_t>((idxNumRecs + 1) & 0xFF);
    }

    return true;
}

// C4 helper: keep the root-index entry pointing to `leafNode` in sync
// with the leaf's current first-record key. Called after every operation
// that may shift a leaf's first record (insert at front, split, etc.).
// No-op for treeDepth==1 (root is the leaf, no index exists).
bool MacintoshHFSHandler::syncIndexEntryForLeaf(
        std::vector<uint8_t>& catalogBytes,
        uint16_t nodeSize,
        uint32_t leafNode) {
    if (catalogBytes.size() < nodeSize) return false;

    auto getU32 = [&](size_t off) -> uint32_t {
        return (static_cast<uint32_t>(catalogBytes[off    ]) << 24) |
               (static_cast<uint32_t>(catalogBytes[off + 1]) << 16) |
               (static_cast<uint32_t>(catalogBytes[off + 2]) <<  8) |
                static_cast<uint32_t>(catalogBytes[off + 3]);
    };

    const size_t bth = 14;
    const uint16_t treeDepth =
        static_cast<uint16_t>((catalogBytes[bth] << 8) | catalogBytes[bth + 1]);
    if (treeDepth < 2) return true;
    const uint32_t rootNode = getU32(bth + 0x02);

    // Read the leaf's current first record key.
    const size_t leafOff = static_cast<size_t>(leafNode) * nodeSize;
    if (leafOff + nodeSize > catalogBytes.size()) return false;
    const uint8_t* lp = catalogBytes.data() + leafOff;
    const uint16_t leafNumRecs =
        static_cast<uint16_t>((lp[0x0a] << 8) | lp[0x0b]);
    if (leafNumRecs == 0) return true;
    const uint16_t leafFirstOff =
        static_cast<uint16_t>((lp[nodeSize - 2] << 8) | lp[nodeSize - 1]);
    const uint8_t leafKL = lp[leafFirstOff];
    if (leafFirstOff + 1U + leafKL > nodeSize) return false;
    std::vector<uint8_t> firstRec(lp + leafFirstOff,
                                    lp + leafFirstOff + 1 + leafKL);

    // Build the desired index record.
    std::vector<uint8_t> desired = buildIndexRecord(firstRec, leafNode);
    if (desired.empty()) return false;

    // Find the existing index entry pointing to leafNode.
    const size_t idxOff = static_cast<size_t>(rootNode) * nodeSize;
    if (idxOff + nodeSize > catalogBytes.size()) return false;
    uint8_t* ip = catalogBytes.data() + idxOff;
    const uint16_t idxNumRecs =
        static_cast<uint16_t>((ip[0x0a] << 8) | ip[0x0b]);
    auto idxRecOff = [&](uint16_t i) -> uint16_t {
        const size_t pos = nodeSize - 2 * (i + 1);
        return static_cast<uint16_t>((ip[pos] << 8) | ip[pos + 1]);
    };
    for (uint16_t i = 0; i < idxNumRecs; ++i) {
        const uint16_t off = idxRecOff(i);
        const uint16_t end = idxRecOff(static_cast<uint16_t>(i + 1));
        if (off >= nodeSize || end > nodeSize || off >= end) continue;
        const uint8_t kl = ip[off];
        size_t dataOff = 1U + kl;
        if (dataOff & 1U) dataOff += 1;
        if (dataOff + 4 > static_cast<size_t>(end - off)) continue;
        const uint32_t child =
            (static_cast<uint32_t>(ip[off + dataOff    ]) << 24) |
            (static_cast<uint32_t>(ip[off + dataOff + 1]) << 16) |
            (static_cast<uint32_t>(ip[off + dataOff + 2]) <<  8) |
             static_cast<uint32_t>(ip[off + dataOff + 3]);
        if (child != leafNode) continue;

        // Compare existing record bytes with desired.
        const size_t oldRecLen = static_cast<size_t>(end - off);
        if (oldRecLen == desired.size() &&
            std::memcmp(ip + off, desired.data(), oldRecLen) == 0) {
            return true;  // already in sync
        }

        const ptrdiff_t sizeDelta =
            static_cast<ptrdiff_t>(desired.size()) -
            static_cast<ptrdiff_t>(oldRecLen);
        const uint16_t freePtr = idxRecOff(idxNumRecs);
        const size_t freeSpace =
            (nodeSize - static_cast<size_t>(idxNumRecs + 1) * 2U) - freePtr;
        if (sizeDelta > 0 && static_cast<size_t>(sizeDelta) > freeSpace) {
            throw NotImplementedException(
                "HFS index sync: entry update would overflow node");
        }
        const size_t shiftLen = freePtr - end;
        if (sizeDelta != 0 && shiftLen > 0) {
            std::memmove(ip + end + sizeDelta, ip + end, shiftLen);
        }
        std::memcpy(ip + off, desired.data(), desired.size());
        for (uint16_t j = static_cast<uint16_t>(i + 1); j <= idxNumRecs; ++j) {
            const size_t pos = nodeSize - 2 * (j + 1);
            const uint16_t o0 =
                static_cast<uint16_t>((ip[pos] << 8) | ip[pos + 1]);
            const uint16_t no0 = static_cast<uint16_t>(o0 + sizeDelta);
            ip[pos]     = static_cast<uint8_t>((no0 >> 8) & 0xFF);
            ip[pos + 1] = static_cast<uint8_t>(no0 & 0xFF);
        }
        return true;
    }
    // No matching entry — leaf isn't pointed to by root index. Could happen
    // for the very first leaf in a depth-1→depth-2 transition that hasn't
    // completed yet; safe to ignore.
    return true;
}

// B2: single-leaf catalog remover used by deleteDirectory. Looks up by
// (parentCNID, name) and drops the matching record. Returns true when a
// record was removed; false when the key was not found.
bool MacintoshHFSHandler::removeCatalogLeafRecord(
        std::vector<uint8_t>& raw,
        uint32_t parentCNID,
        const std::string& name) {
    if (m_mdb.allocBlockSize == 0) return false;
    if (m_mdb.catalogExtents[1] == 0) return false;

    const uint32_t blockSize = m_mdb.allocBlockSize;
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
    std::set<uint32_t> visited;
    while (node != 0 && !removed) {
        if (visited.count(node)) break;
        visited.insert(node);
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
            if (kl < 6) continue;
            const uint32_t pc =
                (static_cast<uint32_t>(p[off + 0x02]) << 24) |
                (static_cast<uint32_t>(p[off + 0x03]) << 16) |
                (static_cast<uint32_t>(p[off + 0x04]) << 8) |
                 static_cast<uint32_t>(p[off + 0x05]);
            if (pc != parentCNID) continue;
            const uint8_t nl = p[off + 0x06];
            const std::string nm(reinterpret_cast<const char*>(p + off + 0x07),
                                  std::min<size_t>(nl,
                                    static_cast<size_t>(kl >= 6 ? kl - 6 : 0)));
            if (nm != name) continue;

            // Match — drop this record.
            const uint16_t freePtr = recOff(numRecs);
            const size_t shiftLen = freePtr - end;
            if (shiftLen > 0) {
                std::memmove(p + off, p + end, shiftLen);
            }
            std::memset(p + freePtr - recLen, 0, recLen);

            std::vector<uint16_t> offsets;
            offsets.reserve(numRecs);
            for (uint16_t k = 0; k < numRecs; ++k) offsets.push_back(recOff(k));
            offsets.erase(offsets.begin() + i);
            for (size_t k = i; k < offsets.size(); ++k) {
                offsets[k] = static_cast<uint16_t>(offsets[k] - recLen);
            }
            offsets.push_back(static_cast<uint16_t>(freePtr - recLen));
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
            putBE16(catalogBytes, nodeOff + 0x0a, static_cast<uint16_t>(numRecs - 1));
            // Cleanup: BT header rec leafRecords -1, and if we removed
            // record 0 the leaf's first key changed → sync index entry.
            bumpLeafRecordsCount(catalogBytes, -1);
            if (i == 0) {
                syncIndexEntryForLeaf(catalogBytes, nodeSize, node);
            }
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
    return true;
}

bool MacintoshHFSHandler::createDirectory(const std::string& path) {
    if (!m_disk) return false;
    if (m_disk->isWriteProtected()) {
        throw WriteProtectedException();
    }

    // C1: resolve parent path → parent CNID (returns root for plain leaf).
    ParentResolved pr = resolveParentForMutation(path);
    const uint32_t parentCNID = pr.parentCNID;
    const std::string& leaf = pr.leafName;
    if (leaf.empty() || leaf.size() > 31) return false;
    if (lookupByPath(path) != nullptr) return false;
    if (m_mdb.allocBlockSize == 0) return false;
    if (m_mdb.catalogExtents[1] == 0) {
        throw NotImplementedException("HFS createDirectory: catalog file empty");
    }

    std::vector<uint8_t> raw = m_disk->getRawData();

    const uint32_t newCNID = m_mdb.nextCNID;
    const uint32_t macNow = toMacEpoch(std::time(nullptr));

    // 1. Folder record (recType=0x01, 70-byte body per Inside Mac).
    std::vector<uint8_t> folderBody(70, 0);
    folderBody[0x00] = 0x01;  // recType
    // 0x02..0x03 flags = 0; 0x04..0x05 valence = 0
    putBE32(folderBody, 0x06, newCNID);
    putBE32(folderBody, 0x0a, macNow);  // create
    putBE32(folderBody, 0x0e, macNow);  // modify
    // 0x12 backup, 0x16 DInfo[16], 0x26 DXInfo[16], 0x36 reserved[16] = 0

    std::vector<uint8_t> folderKey;
    {
        const uint8_t kl = static_cast<uint8_t>(6 + leaf.size());
        folderKey.push_back(kl);
        folderKey.push_back(0);
        folderKey.push_back(static_cast<uint8_t>((parentCNID >> 24) & 0xFF));
        folderKey.push_back(static_cast<uint8_t>((parentCNID >> 16) & 0xFF));
        folderKey.push_back(static_cast<uint8_t>((parentCNID >> 8) & 0xFF));
        folderKey.push_back(static_cast<uint8_t>(parentCNID & 0xFF));
        folderKey.push_back(static_cast<uint8_t>(leaf.size()));
        folderKey.insert(folderKey.end(), leaf.begin(), leaf.end());
    }
    std::vector<uint8_t> folderRecord = folderKey;
    if (folderRecord.size() & 1U) folderRecord.push_back(0);
    folderRecord.insert(folderRecord.end(), folderBody.begin(), folderBody.end());

    // 2. Folder thread record (recType=0x03, 46-byte body per §22.2 sample
    //    + Inside Mac CatThreadRec). thdParID = the actual parent.
    std::vector<uint8_t> threadBody(46, 0);
    threadBody[0x00] = 0x03;  // folder thread
    putBE32(threadBody, 0x0a, parentCNID);  // thdParID
    threadBody[0x0e] = static_cast<uint8_t>(leaf.size());
    std::memcpy(threadBody.data() + 0x0f, leaf.data(),
                std::min<size_t>(leaf.size(), 31));

    std::vector<uint8_t> threadKey;
    {
        const uint8_t kl = 6;  // reserved + parentCNID + nameLen=0
        threadKey.push_back(kl);
        threadKey.push_back(0);
        threadKey.push_back(static_cast<uint8_t>((newCNID >> 24) & 0xFF));
        threadKey.push_back(static_cast<uint8_t>((newCNID >> 16) & 0xFF));
        threadKey.push_back(static_cast<uint8_t>((newCNID >> 8) & 0xFF));
        threadKey.push_back(static_cast<uint8_t>(newCNID & 0xFF));
        threadKey.push_back(0);  // empty name
    }
    std::vector<uint8_t> threadRecord = threadKey;
    if (threadRecord.size() & 1U) threadRecord.push_back(0);
    threadRecord.insert(threadRecord.end(), threadBody.begin(), threadBody.end());

    // 3. Insert both records. Either insertion may throw (split required) —
    //    in that case `raw` is dropped without commit, leaving disk untouched.
    if (!insertCatalogLeafRecord(raw, parentCNID, leaf, folderRecord)) return false;
    if (!insertCatalogLeafRecord(raw, newCNID, std::string(), threadRecord)) {
        return false;
    }

    // 4. MDB updates. drNmRtDirs (root-direct dir count) only bumps when the
    //    new directory is at root; drDirCnt (recursive total) always bumps.
    const int32_t rootDirsDelta = (parentCNID == HFS_ROOT_CNID) ? +1 : 0;
    putBE32(raw, 0x400 + 0x1e, m_mdb.nextCNID + 1);  // drNxtCNID
    bumpMdbWriteMetadata(raw, 0, +1, rootDirsDelta, macNow);
    applyFolderValenceByCNID(raw, m_mdb.firstAllocBlock,
                              m_mdb.allocBlockSize,
                              m_mdb.catalogExtents,
                              parentCNID, +1);

    // 5. Commit.
    m_disk->setRawData(raw);

    // 6. Refresh caches.
    m_childrenByParent.clear();
    m_byCNID.clear();
    m_extentsOverflow.clear();
    parseMdb();
    walkExtentsOverflowLeaves();
    walkCatalogLeaves();
    return true;
}

bool MacintoshHFSHandler::deleteDirectory(const std::string& path) {
    if (!m_disk) return false;
    if (m_disk->isWriteProtected()) {
        throw WriteProtectedException();
    }

    // C1: resolve parent → parent CNID, allowing nested rmdir.
    ParentResolved pr = resolveParentForMutation(path);
    const uint32_t parentCNID = pr.parentCNID;
    const std::string& leaf = pr.leafName;
    if (leaf.empty()) return false;

    const CatalogChild* victim = lookupByPath(path);
    if (!victim) return false;
    if (!victim->isDirectory) return false;

    // Emptiness check via cached catalog (refreshed at end of every mutator).
    auto childIt = m_childrenByParent.find(victim->cnid);
    if (childIt != m_childrenByParent.end() && !childIt->second.empty()) {
        return false;  // non-empty — POSIX rmdir semantics
    }

    std::vector<uint8_t> raw = m_disk->getRawData();

    // Drop the folder record (key parent=parentCNID, name=leaf) AND its
    // thread record (key parent=victim.cnid, name=""). Either failure
    // aborts before commit.
    if (!removeCatalogLeafRecord(raw, parentCNID, leaf)) return false;
    if (!removeCatalogLeafRecord(raw, victim->cnid, std::string())) {
        // First removal succeeded in our snapshot but second didn't —
        // drop the snapshot and throw so the on-disk image is untouched.
        throw NotImplementedException(
            "Macintosh HFS deleteDirectory: thread record missing — "
            "catalog inconsistent (volume may have been written by a tool "
            "that omits thread records).");
    }

    const int32_t rootDirsDelta = (parentCNID == HFS_ROOT_CNID) ? -1 : 0;
    bumpMdbWriteMetadata(raw, 0, -1, rootDirsDelta,
                         toMacEpoch(std::time(nullptr)));
    applyFolderValenceByCNID(raw, m_mdb.firstAllocBlock,
                              m_mdb.allocBlockSize,
                              m_mdb.catalogExtents,
                              parentCNID, -1);

    m_disk->setRawData(raw);

    m_childrenByParent.clear();
    m_byCNID.clear();
    m_extentsOverflow.clear();
    parseMdb();
    walkExtentsOverflowLeaves();
    walkCatalogLeaves();
    return true;
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
