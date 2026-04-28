#include "rdedisktool/filesystem/MacintoshMFSHandler.h"
#include "rdedisktool/Exceptions.h"
#include "rdedisktool/utils/MacEpoch.h"
#include "rdedisktool/utils/MacRoman.h"
#include "rdedisktool/utils/PascalString.h"

#include <algorithm>
#include <cstring>

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

// 12-bit BE allocation map encoding (per macdiskimage.py:5242 — same algorithm
// transcribed for the C++ port). The map starts at MDB offset 64; index 0
// represents allocation block 2.
constexpr uint16_t MFS_CHAIN_END     = 0x001;  // value 1 terminates the chain
constexpr uint16_t MFS_FREE_OR_BAD_0 = 0x000;  // damaged / unallocated
constexpr uint16_t MFS_FREE_OR_BAD_F = 0xFFF;

constexpr size_t MFS_MAP_OFFSET_IN_MDB = 64;
constexpr size_t MFS_MDB_OFFSET = 0x400;

constexpr size_t MFS_DIR_BLOCK_SIZE = 512;
constexpr size_t MFS_DIR_ENTRY_HEADER = 0x32;  // bytes before flNam

} // namespace

uint16_t MacintoshMFSHandler::readAllocEntry(size_t index) const {
    const auto& raw = m_disk->getRawData();
    const size_t mapOffset = MFS_MDB_OFFSET + MFS_MAP_OFFSET_IN_MDB;
    const size_t byteOffset = mapOffset + (index * 12) / 8;
    if (byteOffset + 1 >= raw.size()) return MFS_FREE_OR_BAD_0;
    const uint8_t b0 = raw[byteOffset];
    const uint8_t b1 = raw[byteOffset + 1];
    if ((index & 1U) == 0) {
        return static_cast<uint16_t>((b0 << 4) | (b1 >> 4));
    } else {
        return static_cast<uint16_t>(((b0 & 0x0F) << 8) | b1);
    }
}

uint64_t MacintoshMFSHandler::blockOffset(uint16_t block) const {
    // SPEC §410: byte_offset = drAlBlSt * 512 + (N - 2) * drAlBlkSiz
    // (block 2 is the first addressable allocation block in MFS)
    if (block < 2) return 0;
    return static_cast<uint64_t>(m_mdb.firstAllocBlock) * 512ULL +
           static_cast<uint64_t>(block - 2U) *
            static_cast<uint64_t>(m_mdb.allocBlockSize);
}

bool MacintoshMFSHandler::initialize(DiskImage* disk) {
    m_disk = disk;
    if (!disk) return false;
    if (!parseMdb()) return false;
    parseBootBlock();
    if (!parseDirectory()) return false;
    return true;
}

bool MacintoshMFSHandler::parseMdb() {
    const auto& raw = m_disk->getRawData();
    if (raw.size() < MFS_MDB_OFFSET + 64) return false;
    const uint8_t* p = raw.data() + MFS_MDB_OFFSET;

    m_mdb.signature = be16(p + 0x00);
    if (m_mdb.signature != 0xD2D7) return false;

    m_mdb.numFiles        = be16(p + 0x0c);
    m_mdb.directoryStart  = be16(p + 0x0e);
    m_mdb.directoryLength = be16(p + 0x10);
    m_mdb.numAllocBlocks  = be16(p + 0x12);
    m_mdb.allocBlockSize  = be32(p + 0x14);
    m_mdb.firstAllocBlock = be16(p + 0x1c);
    m_mdb.nextFileNumber  = be32(p + 0x1e);
    m_mdb.freeAllocBlocks = be16(p + 0x22);

    // Volume name @ 0x24, 28-byte field (1 length byte + up to 27 MacRoman).
    {
        const size_t avail = raw.size() - (MFS_MDB_OFFSET + 0x24);
        const std::string raw_pname = readPascalBounded(p + 0x24, avail, 27);
        m_mdb.volumeName = macRomanToUtf8(raw_pname);
    }

    if (m_mdb.allocBlockSize == 0 || m_mdb.numAllocBlocks == 0) return false;
    return true;
}

bool MacintoshMFSHandler::parseBootBlock() {
    const auto& raw = m_disk->getRawData();
    if (raw.size() < 0x80) return false;
    const uint8_t* p = raw.data();
    if (p[0] != 'L' || p[1] != 'K') {
        m_bootBlock.present = false;
        return false;
    }
    m_bootBlock.present = true;
    m_bootBlock.systemName = macRomanToUtf8(readPascalBounded(p + 0x0a, 16, 15));
    m_bootBlock.finderName = macRomanToUtf8(readPascalBounded(p + 0x1a, 16, 15));
    return true;
}

bool MacintoshMFSHandler::parseDirectory() {
    const auto& raw = m_disk->getRawData();
    const uint64_t dirStartByte =
        static_cast<uint64_t>(m_mdb.directoryStart) * 512ULL;
    const uint64_t dirEndByte =
        dirStartByte + static_cast<uint64_t>(m_mdb.directoryLength) * 512ULL;
    if (dirEndByte > raw.size()) return false;

    // Directory area is a flat sequence of 512-byte blocks; entries do not
    // cross block boundaries. flFlags == 0 marks the end of active entries
    // within a block.
    for (uint64_t blockBase = dirStartByte; blockBase < dirEndByte;
         blockBase += MFS_DIR_BLOCK_SIZE) {
        size_t off = 0;
        while (off + MFS_DIR_ENTRY_HEADER + 1 <= MFS_DIR_BLOCK_SIZE) {
            const uint8_t* e = raw.data() + blockBase + off;
            const uint8_t flags = e[0];
            if (flags == 0) break;          // end-of-block sentinel
            const uint8_t nameLen = e[0x32];
            // Entry length = 0x32 (header) + 1 (name length) + nameLen, then
            // round up to even (the next entry must start on an even offset).
            size_t entryLen = static_cast<size_t>(MFS_DIR_ENTRY_HEADER) + 1U + nameLen;
            if (entryLen & 1U) entryLen += 1;
            if (off + entryLen > MFS_DIR_BLOCK_SIZE) break;

            const bool used = (flags & 0x80) != 0;
            if (used) {
                DirEntry de;
                de.used   = true;
                de.locked = (flags & 0x01) != 0;
                std::memcpy(de.fileType, e + 0x02, 4);
                std::memcpy(de.creator,  e + 0x06, 4);
                std::memcpy(de.flUsrWds, e + 0x02, 16);
                de.cnid           = be32(e + 0x12);
                de.dataStartBlock = be16(e + 0x16);
                de.dataLogical    = be32(e + 0x18);
                de.rsrcStartBlock = be16(e + 0x20);
                de.rsrcLogical    = be32(e + 0x22);
                de.createDate     = be32(e + 0x2a);
                de.modifyDate     = be32(e + 0x2e);
                std::string macName(reinterpret_cast<const char*>(e + 0x33), nameLen);
                de.macRomanName = macName;
                de.name = macRomanToUtf8(macName);
                m_entries.push_back(std::move(de));
            }
            off += entryLen;
        }
    }
    return true;
}

const MacintoshMFSHandler::DirEntry*
MacintoshMFSHandler::findEntry(const std::string& name) const {
    // Path uses '/' separator; strip leading '/' and any volume-name prefix.
    std::string p = name;
    while (!p.empty() && p.front() == '/') p.erase(p.begin());
    // Optional volume prefix: "/Empty MFS/Hello.txt" → drop "Empty MFS/"
    if (!m_mdb.volumeName.empty() &&
        p.size() > m_mdb.volumeName.size() + 1 &&
        p.compare(0, m_mdb.volumeName.size(), m_mdb.volumeName) == 0 &&
        p[m_mdb.volumeName.size()] == '/') {
        p.erase(0, m_mdb.volumeName.size() + 1);
    }
    // MFS is flat — name must be a leaf with no slashes.
    if (p.find('/') != std::string::npos) return nullptr;

    for (const auto& e : m_entries) {
        if (e.name == p) return &e;
    }
    return nullptr;
}

std::vector<uint8_t> MacintoshMFSHandler::extractFork(uint16_t startBlock,
                                                       uint32_t logical) const {
    std::vector<uint8_t> out;
    if (logical == 0 || startBlock < 2) return out;
    const auto& raw = m_disk->getRawData();

    uint16_t block = startBlock;
    std::vector<bool> visited(static_cast<size_t>(m_mdb.numAllocBlocks) + 4, false);
    while (true) {
        if (block < 2) break;
        const size_t idx = static_cast<size_t>(block) - 2;
        if (idx < visited.size()) {
            if (visited[idx]) break;       // loop guard
            visited[idx] = true;
        }
        const uint64_t off = blockOffset(block);
        if (off >= raw.size()) break;
        const uint64_t blockSize = m_mdb.allocBlockSize;
        const uint64_t take = std::min<uint64_t>(blockSize, raw.size() - off);
        out.insert(out.end(), raw.begin() + off, raw.begin() + off + take);
        if (out.size() >= logical) break;

        // Allocation map index for this block is (block - 2).
        const size_t mapIdx = static_cast<size_t>(block) - 2;
        if (mapIdx >= m_mdb.numAllocBlocks) break;
        const uint16_t next = readAllocEntry(mapIdx);
        if (next == MFS_CHAIN_END)              break;
        if (next == MFS_FREE_OR_BAD_0)          break;
        if (next == MFS_FREE_OR_BAD_F)          break;
        block = next;
    }

    if (out.size() > logical) out.resize(logical);
    return out;
}

const MacintoshMFSHandler::DirEntry*
MacintoshMFSHandler::lookupByName(const std::string& name) const {
    return findEntry(name);
}

std::vector<FileEntry> MacintoshMFSHandler::listFiles(const std::string& path) {
    std::vector<FileEntry> out;
    if (!path.empty() && path != "/" && path != m_mdb.volumeName &&
        path != ("/" + m_mdb.volumeName)) {
        // MFS has no real subdirectories; any non-root path returns empty.
        return out;
    }
    for (const auto& e : m_entries) {
        FileEntry fe;
        fe.name = e.name;
        fe.size = static_cast<size_t>(e.dataLogical);
        fe.isDirectory = false;
        if (e.createDate) fe.createdTime  = fromMacEpoch(e.createDate);
        if (e.modifyDate) fe.modifiedTime = fromMacEpoch(e.modifyDate);
        out.push_back(fe);
    }
    return out;
}

std::vector<uint8_t> MacintoshMFSHandler::readFile(const std::string& filename) {
    const DirEntry* e = findEntry(filename);
    if (!e) {
        throw FileNotFoundException(filename);
    }
    return extractFork(e->dataStartBlock, e->dataLogical);
}

// 12-bit BE allocation map writer. Inverse of readAllocEntry.
void MacintoshMFSHandler::writeAllocEntry(std::vector<uint8_t>& raw,
                                            size_t index, uint16_t value) const {
    const size_t mapOffset = MFS_MDB_OFFSET + MFS_MAP_OFFSET_IN_MDB;
    const size_t byteOffset = mapOffset + (index * 12) / 8;
    if (byteOffset + 1 >= raw.size()) return;
    const uint16_t v = value & 0x0FFF;
    if ((index & 1U) == 0) {
        // entry occupies high 12 bits of (b0 << 8 | b1):  b0 = v[11..4],
        // b1 high nibble = v[3..0], b1 low nibble preserves the next entry.
        raw[byteOffset] = static_cast<uint8_t>((v >> 4) & 0xFF);
        raw[byteOffset + 1] = static_cast<uint8_t>(
            (raw[byteOffset + 1] & 0x0F) | ((v & 0x0F) << 4));
    } else {
        // entry occupies low 12 bits of (b0 << 8 | b1):  b0 high nibble
        // preserves the previous entry, b0 low nibble = v[11..8], b1 = v[7..0].
        raw[byteOffset] = static_cast<uint8_t>(
            (raw[byteOffset] & 0xF0) | ((v >> 8) & 0x0F));
        raw[byteOffset + 1] = static_cast<uint8_t>(v & 0xFF);
    }
}

std::vector<uint16_t> MacintoshMFSHandler::findFreeBlocks(
        const std::vector<uint8_t>& /*raw*/, size_t need) const {
    std::vector<uint16_t> out;
    out.reserve(need);
    for (size_t idx = 0; idx < m_mdb.numAllocBlocks && out.size() < need; ++idx) {
        const uint16_t v = readAllocEntry(idx);
        if (v == MFS_FREE_OR_BAD_0) {
            // free
            out.push_back(static_cast<uint16_t>(idx + 2));
        }
    }
    return out;
}

bool MacintoshMFSHandler::insertDirectoryEntry(std::vector<uint8_t>& raw,
                                                  const DirEntry& de) const {
    const uint64_t dirStartByte =
        static_cast<uint64_t>(m_mdb.directoryStart) * 512ULL;
    const uint64_t dirEndByte =
        dirStartByte + static_cast<uint64_t>(m_mdb.directoryLength) * 512ULL;
    if (dirEndByte > raw.size()) return false;

    // Build the entry buffer.
    const size_t nameLen = std::min<size_t>(de.name.size(), 255);
    size_t entryLen = MFS_DIR_ENTRY_HEADER + 1U + nameLen;
    if (entryLen & 1U) entryLen += 1;

    std::vector<uint8_t> buf(entryLen, 0);
    buf[0x00] = static_cast<uint8_t>(0x80 | (de.locked ? 0x01 : 0x00));  // flFlags
    buf[0x01] = 0;                                                         // flTyp
    std::memcpy(buf.data() + 0x02, de.fileType, 4);
    std::memcpy(buf.data() + 0x06, de.creator, 4);
    // 0x0a..0x11 left zero (Finder fdLocation / fdFldr / fdIconID)
    // flFlNum @ 0x12
    buf[0x12] = static_cast<uint8_t>((de.cnid >> 24) & 0xFF);
    buf[0x13] = static_cast<uint8_t>((de.cnid >> 16) & 0xFF);
    buf[0x14] = static_cast<uint8_t>((de.cnid >> 8) & 0xFF);
    buf[0x15] = static_cast<uint8_t>(de.cnid & 0xFF);
    // flStBlk @ 0x16
    buf[0x16] = static_cast<uint8_t>((de.dataStartBlock >> 8) & 0xFF);
    buf[0x17] = static_cast<uint8_t>(de.dataStartBlock & 0xFF);
    // flLgLen @ 0x18
    buf[0x18] = static_cast<uint8_t>((de.dataLogical >> 24) & 0xFF);
    buf[0x19] = static_cast<uint8_t>((de.dataLogical >> 16) & 0xFF);
    buf[0x1a] = static_cast<uint8_t>((de.dataLogical >> 8) & 0xFF);
    buf[0x1b] = static_cast<uint8_t>(de.dataLogical & 0xFF);
    // flPyLen @ 0x1c (round logical up to allocBlockSize)
    const uint32_t physical =
        m_mdb.allocBlockSize == 0 ? 0 :
        ((de.dataLogical + m_mdb.allocBlockSize - 1) / m_mdb.allocBlockSize) *
            m_mdb.allocBlockSize;
    buf[0x1c] = static_cast<uint8_t>((physical >> 24) & 0xFF);
    buf[0x1d] = static_cast<uint8_t>((physical >> 16) & 0xFF);
    buf[0x1e] = static_cast<uint8_t>((physical >> 8) & 0xFF);
    buf[0x1f] = static_cast<uint8_t>(physical & 0xFF);
    // flRStBlk @ 0x20, flRLgLen @ 0x22, flRPyLen @ 0x26 — all zero (no rsrc fork)
    // flCrDat @ 0x2a, flMdDat @ 0x2e
    auto putBE32 = [&](size_t off, uint32_t v) {
        buf[off]     = static_cast<uint8_t>((v >> 24) & 0xFF);
        buf[off + 1] = static_cast<uint8_t>((v >> 16) & 0xFF);
        buf[off + 2] = static_cast<uint8_t>((v >> 8) & 0xFF);
        buf[off + 3] = static_cast<uint8_t>(v & 0xFF);
    };
    putBE32(0x2a, de.createDate);
    putBE32(0x2e, de.modifyDate);
    // flNam @ 0x32
    buf[0x32] = static_cast<uint8_t>(nameLen);
    std::memcpy(buf.data() + 0x33, de.name.data(), nameLen);

    // Find a 512-byte directory block with enough trailing free space
    // (sentinel-zero region must be large enough to fit the new entry +
    // a final zero terminator byte).
    for (uint64_t blockBase = dirStartByte; blockBase < dirEndByte;
         blockBase += MFS_DIR_BLOCK_SIZE) {
        size_t off = 0;
        while (off + MFS_DIR_ENTRY_HEADER + 1 <= MFS_DIR_BLOCK_SIZE) {
            const uint8_t flags = raw[blockBase + off];
            if (flags == 0) break;
            const uint8_t nLen = raw[blockBase + off + 0x32];
            size_t curLen = MFS_DIR_ENTRY_HEADER + 1U + nLen;
            if (curLen & 1U) curLen += 1;
            off += curLen;
        }
        if (off + entryLen <= MFS_DIR_BLOCK_SIZE) {
            std::memcpy(raw.data() + blockBase + off, buf.data(), entryLen);
            return true;
        }
    }
    return false;  // directory full
}

void MacintoshMFSHandler::updateMdb(std::vector<uint8_t>& raw,
                                      int delta_files, int delta_freeBlocks,
                                      uint32_t bumpNxtFNum) const {
    if (raw.size() < MFS_MDB_OFFSET + 0x40) return;
    auto getBE16 = [&](size_t off) -> uint16_t {
        return static_cast<uint16_t>(
            (static_cast<uint16_t>(raw[off]) << 8) | raw[off + 1]);
    };
    auto putBE16 = [&](size_t off, uint16_t v) {
        raw[off]     = static_cast<uint8_t>((v >> 8) & 0xFF);
        raw[off + 1] = static_cast<uint8_t>(v & 0xFF);
    };
    auto putBE32 = [&](size_t off, uint32_t v) {
        raw[off]     = static_cast<uint8_t>((v >> 24) & 0xFF);
        raw[off + 1] = static_cast<uint8_t>((v >> 16) & 0xFF);
        raw[off + 2] = static_cast<uint8_t>((v >> 8) & 0xFF);
        raw[off + 3] = static_cast<uint8_t>(v & 0xFF);
    };
    const size_t base = MFS_MDB_OFFSET;
    if (delta_files != 0) {
        const uint16_t cur = getBE16(base + 0x0c);
        putBE16(base + 0x0c, static_cast<uint16_t>(static_cast<int>(cur) + delta_files));
    }
    if (delta_freeBlocks != 0) {
        const uint16_t cur = getBE16(base + 0x22);
        putBE16(base + 0x22, static_cast<uint16_t>(static_cast<int>(cur) + delta_freeBlocks));
    }
    if (bumpNxtFNum > 0) {
        putBE32(base + 0x1e, bumpNxtFNum);
    }
}

bool MacintoshMFSHandler::writeFile(const std::string& filename,
                                      const std::vector<uint8_t>& data,
                                      const FileMetadata& metadata) {
    if (!m_disk) return false;
    if (m_disk->isWriteProtected()) {
        throw WriteProtectedException();
    }

    // Resolve the file name (last path segment).
    std::string leaf = filename;
    {
        const size_t pos = leaf.find_last_of('/');
        if (pos != std::string::npos) leaf = leaf.substr(pos + 1);
    }
    if (leaf.empty() || leaf.size() > 255) return false;

    if (findEntry(leaf) != nullptr) {
        // Phase 2 simplification: refuse overwrite. Caller is expected to
        // delete-then-add for replacement (see CLI cmdAdd behavior).
        return false;
    }

    if (m_mdb.allocBlockSize == 0) return false;

    // Compute how many allocation blocks are needed.
    const size_t blockSize = m_mdb.allocBlockSize;
    const size_t needed =
        data.empty() ? 0 : ((data.size() + blockSize - 1) / blockSize);

    // Snapshot the disk's raw bytes, mutate locally, then commit via
    // setRawData() so the container layer (IMG) can save back atomically.
    std::vector<uint8_t> raw = m_disk->getRawData();

    // Allocate blocks.
    std::vector<uint16_t> blocks = findFreeBlocks(raw, needed);
    if (blocks.size() < needed) return false;  // disk full

    // Write the data into the allocation blocks.
    for (size_t i = 0; i < needed; ++i) {
        const uint64_t off = blockOffset(blocks[i]);
        if (off + blockSize > raw.size()) return false;
        const size_t srcOff = i * blockSize;
        const size_t take = std::min<size_t>(blockSize, data.size() - srcOff);
        std::memcpy(raw.data() + off, data.data() + srcOff, take);
        if (take < blockSize) {
            std::memset(raw.data() + off + take, 0, blockSize - take);
        }
    }

    // Update the allocation map: chain n → n+1, last → MFS_CHAIN_END.
    for (size_t i = 0; i < needed; ++i) {
        const uint16_t cur = blocks[i];
        const uint16_t next = (i + 1 < needed) ? blocks[i + 1] : MFS_CHAIN_END;
        const size_t mapIdx = static_cast<size_t>(cur) - 2;
        writeAllocEntry(raw, mapIdx, next);
    }

    // Build the directory entry.
    DirEntry de;
    de.used = true;
    de.locked = metadata.readOnly;
    de.cnid = m_mdb.nextFileNumber;
    de.dataStartBlock = needed > 0 ? blocks[0] : 0;
    de.dataLogical = static_cast<uint32_t>(data.size());
    de.rsrcStartBlock = 0;
    de.rsrcLogical = 0;
    // Default Finder type/creator: ttxt 'TEXT' if the caller didn't supply
    // anything via metadata. fileType byte from FileMetadata is Apple-centric;
    // for Mac fixtures we leave both zeroed if not overridden.
    de.name = leaf;
    de.createDate = 0;
    de.modifyDate = 0;

    if (!insertDirectoryEntry(raw, de)) {
        // Roll back: free the blocks we just allocated.
        for (size_t i = 0; i < needed; ++i) {
            const size_t mapIdx = static_cast<size_t>(blocks[i]) - 2;
            writeAllocEntry(raw, mapIdx, MFS_FREE_OR_BAD_0);
        }
        return false;
    }

    // Update MDB scalars.
    updateMdb(raw, +1, -static_cast<int>(needed), m_mdb.nextFileNumber + 1);

    // Commit.
    m_disk->setRawData(raw);

    // Refresh the cached structures.
    m_entries.clear();
    parseMdb();
    parseDirectory();
    return true;
}

bool MacintoshMFSHandler::deleteFile(const std::string& filename) {
    if (!m_disk) return false;
    if (m_disk->isWriteProtected()) {
        throw WriteProtectedException();
    }
    const DirEntry* victim = findEntry(filename);
    if (!victim) return false;
    if (victim->rsrcStartBlock != 0) {
        // Phase 2 simplification: only handle the data fork case for now.
        // Files with a non-empty resource fork are out of scope here.
        return false;
    }

    std::vector<uint8_t> raw = m_disk->getRawData();
    const uint32_t targetCNID = victim->cnid;

    // Walk the data-fork chain and free every block.
    const size_t blockSize = m_mdb.allocBlockSize;
    if (blockSize == 0) return false;
    int freedBlocks = 0;
    {
        uint16_t block = victim->dataStartBlock;
        std::vector<bool> visited(static_cast<size_t>(m_mdb.numAllocBlocks) + 4, false);
        while (block >= 2) {
            const size_t mapIdx = static_cast<size_t>(block) - 2;
            if (mapIdx >= m_mdb.numAllocBlocks) break;
            if (visited[mapIdx]) break;
            visited[mapIdx] = true;
            const uint16_t next = readAllocEntry(mapIdx);
            writeAllocEntry(raw, mapIdx, MFS_FREE_OR_BAD_0);
            freedBlocks++;
            if (next == MFS_CHAIN_END || next == MFS_FREE_OR_BAD_0 ||
                next == MFS_FREE_OR_BAD_F) break;
            block = next;
        }
    }

    // Locate the directory entry by CNID and clear its used bit. We sweep
    // every directory block; entries don't cross block boundaries.
    const uint64_t dirStartByte =
        static_cast<uint64_t>(m_mdb.directoryStart) * 512ULL;
    const uint64_t dirEndByte =
        dirStartByte + static_cast<uint64_t>(m_mdb.directoryLength) * 512ULL;
    bool cleared = false;
    for (uint64_t blockBase = dirStartByte; blockBase < dirEndByte && !cleared;
         blockBase += MFS_DIR_BLOCK_SIZE) {
        size_t off = 0;
        while (off + MFS_DIR_ENTRY_HEADER + 1 <= MFS_DIR_BLOCK_SIZE) {
            const uint8_t flags = raw[blockBase + off];
            if (flags == 0) break;
            const uint8_t nLen = raw[blockBase + off + 0x32];
            size_t curLen = MFS_DIR_ENTRY_HEADER + 1U + nLen;
            if (curLen & 1U) curLen += 1;

            // Read the cnid field at offset 0x12.
            const uint32_t cnid =
                (static_cast<uint32_t>(raw[blockBase + off + 0x12]) << 24) |
                (static_cast<uint32_t>(raw[blockBase + off + 0x13]) << 16) |
                (static_cast<uint32_t>(raw[blockBase + off + 0x14]) << 8)  |
                 static_cast<uint32_t>(raw[blockBase + off + 0x15]);
            if (cnid == targetCNID) {
                // Mark unused — clear flFlags. The entry payload remains as
                // tombstone; MFS dir scan stops at flFlags == 0 sentinel, so
                // we also have to keep entries packed. Simplest correct
                // approach: shift subsequent entries in this block up by
                // curLen bytes and zero the trailing region.
                const size_t blockTail = MFS_DIR_BLOCK_SIZE - off - curLen;
                if (blockTail > 0) {
                    std::memmove(raw.data() + blockBase + off,
                                 raw.data() + blockBase + off + curLen,
                                 blockTail);
                }
                std::memset(raw.data() + blockBase + (MFS_DIR_BLOCK_SIZE - curLen),
                            0, curLen);
                cleared = true;
                break;
            }
            off += curLen;
        }
    }
    if (!cleared) return false;

    updateMdb(raw, -1, +freedBlocks, /*bumpNxtFNum*/ 0);
    m_disk->setRawData(raw);

    // Refresh caches.
    m_entries.clear();
    parseMdb();
    parseDirectory();
    return true;
}

bool MacintoshMFSHandler::renameFile(const std::string& oldName,
                                       const std::string& newName) {
    if (!m_disk) return false;
    if (m_disk->isWriteProtected()) {
        throw WriteProtectedException();
    }
    const DirEntry* target = findEntry(oldName);
    if (!target) return false;
    if (target->rsrcLogical != 0) {
        // Rename of files with a resource fork would require copying the
        // resource fork bytes through writeFile, which currently only writes
        // the data fork. Out of M11 minimal scope.
        throw NotImplementedException(
            "Macintosh MFS rename of files with resource forks is out of scope");
    }
    std::string leafNew = newName;
    {
        const size_t pos = leafNew.find_last_of('/');
        if (pos != std::string::npos) leafNew = leafNew.substr(pos + 1);
    }
    if (leafNew.empty() || leafNew.size() > 255) return false;
    if (leafNew == target->name) return true;        // no-op
    if (findEntry(leafNew) != nullptr) return false; // collision

    // Snapshot the data fork, then delete-then-add. Both are MFS write paths
    // already covered by M6's cross-tool tests.
    std::vector<uint8_t> data = extractFork(target->dataStartBlock,
                                              target->dataLogical);
    if (!deleteFile(oldName)) return false;
    FileMetadata md;
    md.targetName = leafNew;
    return writeFile(leafNew, data, md);
}

bool MacintoshMFSHandler::format(const std::string& volumeName) {
    // Native MFS format. Mirrors macdiskimage.py:5624 (write_mfs_init_empty)
    // byte-for-byte so rdedisktool create produces the same image bytes as
    // the Python reference for the same parameters.
    if (!m_disk) return false;

    // Defaults match Python: 800 sectors, alloc_block_size=1024, dir=12
    // sectors, bootable=false. Native format takes the volume name from
    // the CLI; everything else uses the same canonical defaults.
    constexpr size_t SECTOR_SZ = 512;
    constexpr size_t MFS_BOOT_BLOCK_SECTORS = 2;
    constexpr size_t MFS_MDB_AND_MAP_SECTORS = 2;
    constexpr size_t MFS_DIR_START_BLOCK = MFS_BOOT_BLOCK_SECTORS + MFS_MDB_AND_MAP_SECTORS;
    constexpr size_t MFS_MAP_CAP_BYTES = MFS_MDB_AND_MAP_SECTORS * SECTOR_SZ - 64;
    constexpr size_t MFS_MAP_MAX_ENTRIES = (MFS_MAP_CAP_BYTES * 8) / 12;

    const auto& existing = m_disk->getRawData();
    const size_t totalBytes = existing.size();
    if (totalBytes == 0 || (totalBytes % SECTOR_SZ) != 0) {
        throw InvalidFormatException("Macintosh MFS format: backing image must be "
                                      "a non-zero multiple of 512 bytes "
                                      "(call create() first)");
    }
    const size_t totalSectors = totalBytes / SECTOR_SZ;

    // Volume name validation matches Python (max 27 MacRoman bytes).
    std::string vname = volumeName.empty() ? std::string("UNTITLED") : volumeName;
    if (vname.size() > 27) {
        throw InvalidFormatException("MFS format: volume name longer than 27 bytes");
    }

    // Allocation block geometry: defaults from Python, with the same
    // capacity/limit checks. allocation_block_size = 1024, directory_sectors = 12.
    const size_t allocBlockSize = 1024;
    const size_t directorySectors = 12;
    const size_t allocStartBlockSec = MFS_DIR_START_BLOCK + directorySectors;
    if (allocStartBlockSec >= totalSectors) {
        throw InvalidFormatException("MFS format: image too small for boot+MDB+directory");
    }
    const size_t availableBytes = totalBytes - allocStartBlockSec * SECTOR_SZ;
    if (availableBytes < allocBlockSize) {
        throw InvalidFormatException("MFS format: no space for any allocation block");
    }
    size_t allocCount = availableBytes / allocBlockSize;
    if (allocCount > MFS_MAP_MAX_ENTRIES) {
        throw InvalidFormatException(
            "MFS format: allocation block count " + std::to_string(allocCount) +
            " exceeds map capacity " + std::to_string(MFS_MAP_MAX_ENTRIES) +
            "; use larger allocation_block_size or smaller image");
    }
    if (allocCount > 0xFFFFu) {
        throw InvalidFormatException("MFS format: allocation block count exceeds drNmAlBlks 16-bit limit");
    }

    std::vector<uint8_t> raw(totalBytes, 0);

    // MDB at 0x400 (matches macdiskimage.py:5695). All-free volume so
    // drFreeBks = allocCount; drNmFls = 0; drNxtFNum = 1.
    raw[MFS_MDB_OFFSET]     = 0xD2;
    raw[MFS_MDB_OFFSET + 1] = 0xD7;
    auto put16 = [&](size_t off, uint16_t v) {
        raw[off]     = static_cast<uint8_t>((v >> 8) & 0xFF);
        raw[off + 1] = static_cast<uint8_t>(v & 0xFF);
    };
    auto put32 = [&](size_t off, uint32_t v) {
        raw[off]     = static_cast<uint8_t>((v >> 24) & 0xFF);
        raw[off + 1] = static_cast<uint8_t>((v >> 16) & 0xFF);
        raw[off + 2] = static_cast<uint8_t>((v >> 8) & 0xFF);
        raw[off + 3] = static_cast<uint8_t>(v & 0xFF);
    };
    put16(MFS_MDB_OFFSET + 0x0c, 0);                       // drNmFls
    put16(MFS_MDB_OFFSET + 0x0e, MFS_DIR_START_BLOCK);     // drDrSt
    put16(MFS_MDB_OFFSET + 0x10, directorySectors);        // drBlLen
    put16(MFS_MDB_OFFSET + 0x12,
          static_cast<uint16_t>(allocCount));              // drNmAlBlks
    put32(MFS_MDB_OFFSET + 0x14,
          static_cast<uint32_t>(allocBlockSize));          // drAlBlkSiz
    put16(MFS_MDB_OFFSET + 0x1c,
          static_cast<uint16_t>(allocStartBlockSec));      // drAlBlSt
    put32(MFS_MDB_OFFSET + 0x1e, 1u);                       // drNxtFNum
    put16(MFS_MDB_OFFSET + 0x22,
          static_cast<uint16_t>(allocCount));              // drFreeBks
    raw[MFS_MDB_OFFSET + 0x24] = static_cast<uint8_t>(vname.size());  // length
    std::memcpy(raw.data() + MFS_MDB_OFFSET + 0x25,
                vname.data(), vname.size());

    // Allocation map and directory area are already zero (free / no entries).

    m_disk->setRawData(raw);

    // Refresh caches.
    m_entries.clear();
    if (!parseMdb()) return false;
    if (!parseDirectory()) return false;
    return true;
}

size_t MacintoshMFSHandler::getFreeSpace() const {
    return static_cast<size_t>(m_mdb.freeAllocBlocks) *
           static_cast<size_t>(m_mdb.allocBlockSize);
}

size_t MacintoshMFSHandler::getTotalSpace() const {
    return static_cast<size_t>(m_mdb.numAllocBlocks) *
           static_cast<size_t>(m_mdb.allocBlockSize);
}

bool MacintoshMFSHandler::fileExists(const std::string& filename) const {
    return findEntry(filename) != nullptr;
}

std::string MacintoshMFSHandler::getVolumeName() const {
    return m_mdb.volumeName;
}

} // namespace rde
