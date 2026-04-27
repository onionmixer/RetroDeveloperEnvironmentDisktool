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
                de.cnid           = be32(e + 0x12);
                de.dataStartBlock = be16(e + 0x16);
                de.dataLogical    = be32(e + 0x18);
                de.rsrcStartBlock = be16(e + 0x20);
                de.rsrcLogical    = be32(e + 0x22);
                de.createDate     = be32(e + 0x2a);
                de.modifyDate     = be32(e + 0x2e);
                std::string macName(reinterpret_cast<const char*>(e + 0x33), nameLen);
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

bool MacintoshMFSHandler::writeFile(const std::string&, const std::vector<uint8_t>&,
                                     const FileMetadata&) {
    throw NotImplementedException("Macintosh MFS write support is not yet implemented (Phase 2)");
}
bool MacintoshMFSHandler::deleteFile(const std::string&) {
    throw NotImplementedException("Macintosh MFS delete support is not yet implemented (Phase 2)");
}
bool MacintoshMFSHandler::renameFile(const std::string&, const std::string&) {
    throw NotImplementedException("Macintosh MFS rename support is not yet implemented (Phase 2)");
}
bool MacintoshMFSHandler::format(const std::string&) {
    throw NotImplementedException("Macintosh MFS format support is not yet implemented (Phase 2)");
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
