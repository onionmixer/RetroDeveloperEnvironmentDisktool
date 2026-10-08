/**
 * Apple II ProDOS File System Handler
 *
 * Full implementation of ProDOS file system operations.
 *
 * Structure:
 * - Blocks 0-1: Boot blocks
 * - Block 2+: Volume directory (key block)
 * - Block 6: Volume bitmap (typically)
 * - Remaining blocks: File data, subdirectories
 *
 * Storage types:
 * - Seedling: Files up to 512 bytes (1 block)
 * - Sapling: Files up to 128KB (1 index block + up to 256 data blocks)
 * - Tree: Files up to 16MB (1 master + 256 index + 256*256 data blocks)
 */

#include "rdedisktool/filesystem/AppleProDOSHandler.h"
#include "rdedisktool/Exceptions.h"
#include "rdedisktool/utils/BinaryReader.h"
#include <algorithm>
#include <cstring>
#include <cctype>
#include <ctime>
#include <functional>
#include <optional>
#include <unordered_set>

namespace rde {

namespace {

// Directory files are followed through their next pointers. A corrupt pointer
// that leads back to a block already read would loop forever, so a repeat is
// an error (the chain is not cut short silently, which could let a write go on).
class DirectoryChainGuard {
public:
    void visit(uint16_t block) {
        if (!m_seen.insert(block).second) {
            throw ReadException("ProDOS directory chain loops back to block " + std::to_string(block));
        }
    }
private:
    std::unordered_set<uint16_t> m_seen;
};

} // namespace

AppleProDOSHandler::AppleProDOSHandler() {
    std::memset(&m_volumeHeader, 0, sizeof(m_volumeHeader));
}

FileSystemType AppleProDOSHandler::getType() const {
    return FileSystemType::ProDOS;
}

bool AppleProDOSHandler::initialize(DiskImage* disk) {
    if (!disk) {
        return false;
    }
    m_disk = disk;

    if (!parseVolumeHeader()) {
        return false;
    }

    if (!parseVolumeBitmap()) {
        return false;
    }

    return true;
}

//=============================================================================
// Block I/O
//=============================================================================

size_t AppleProDOSHandler::imageBlocks() const {
    return m_disk ? std::min<size_t>(m_disk->getTotalBlocks(), 0xFFFF) : 0;
}

std::vector<uint8_t> AppleProDOSHandler::readBlock(size_t block) const {
    if (!m_disk || block >= imageBlocks()) {
        return {};
    }
    return m_disk->readBlock(block);
}

void AppleProDOSHandler::writeBlock(size_t block, const std::vector<uint8_t>& data) {
    if (!m_disk) {
        return;
    }
    // A write outside the image must not vanish silently
    if (block >= imageBlocks()) {
        throw WriteException("ProDOS block " + std::to_string(block) + " is outside the image (" +
                             std::to_string(imageBlocks()) + " blocks)");
    }
    std::vector<uint8_t> blockData = data;
    blockData.resize(BLOCK_SIZE, 0);
    m_disk->writeBlock(block, blockData);
}

//=============================================================================
// Volume Header Operations
//=============================================================================

bool AppleProDOSHandler::parseVolumeHeader() {
    auto block = readBlock(VOLUME_DIR_BLOCK);
    if (block.size() < BLOCK_SIZE) {
        return false;
    }

    // Skip prev/next block pointers (4 bytes)
    size_t offset = 4;

    // Parse storage type and name length
    uint8_t storageAndLength = block[offset];
    m_volumeHeader.storageType = (storageAndLength >> 4) & 0x0F;
    m_volumeHeader.nameLength = storageAndLength & 0x0F;

    // Validate volume header
    if (m_volumeHeader.storageType != STORAGE_VOLUME_HEADER) {
        return false;
    }

    if (m_volumeHeader.nameLength > MAX_FILENAME_LENGTH) {
        m_volumeHeader.nameLength = MAX_FILENAME_LENGTH;
    }

    // Parse volume name
    std::memcpy(m_volumeHeader.name, &block[offset + 1], m_volumeHeader.nameLength);
    m_volumeHeader.name[m_volumeHeader.nameLength] = '\0';

    // Skip reserved (8 bytes at offset 0x14)
    // Parse remaining header fields using BinaryReader
    rdedisktool::BinaryReader reader(block);
    m_volumeHeader.creationDateTime = reader.readU32LE(0x1C);
    m_volumeHeader.version = reader.readU8(0x20);
    m_volumeHeader.minVersion = reader.readU8(0x21);
    m_volumeHeader.access = reader.readU8(0x22);
    m_volumeHeader.entryLength = reader.readU8(0x23);
    m_volumeHeader.entriesPerBlock = reader.readU8(0x24);
    m_volumeHeader.fileCount = reader.readU16LE(0x25);
    m_volumeHeader.bitmapPointer = reader.readU16LE(0x27);
    m_volumeHeader.totalBlocks = reader.readU16LE(0x29);

    // Validate
    if (m_volumeHeader.entryLength != DIR_ENTRY_SIZE) {
        // Use default if invalid
        m_volumeHeader.entryLength = DIR_ENTRY_SIZE;
    }
    if (m_volumeHeader.entriesPerBlock == 0) {
        m_volumeHeader.entriesPerBlock = ENTRIES_PER_BLOCK;
    }
    if (m_volumeHeader.totalBlocks == 0) {
        m_volumeHeader.totalBlocks = static_cast<uint16_t>(imageBlocks());
    }
    m_blockLimit = std::min<size_t>(m_volumeHeader.totalBlocks, imageBlocks());
    if (m_volumeHeader.bitmapPointer == 0) {
        m_volumeHeader.bitmapPointer = BITMAP_BLOCK;
    }

    return true;
}

void AppleProDOSHandler::writeVolumeHeader() {
    auto block = readBlock(VOLUME_DIR_BLOCK);
    if (block.size() < BLOCK_SIZE) {
        block.resize(BLOCK_SIZE, 0);
    }

    size_t offset = 4;
    rdedisktool::BinaryWriter writer(block);

    // Storage type and name length
    writer.writeU8(offset, (m_volumeHeader.storageType << 4) | (m_volumeHeader.nameLength & 0x0F));

    // Volume name
    writer.writeBytes(offset + 1, m_volumeHeader.name, m_volumeHeader.nameLength);

    // Write header fields using BinaryWriter
    writer.writeU32LE(0x1C, m_volumeHeader.creationDateTime);
    writer.writeU8(0x20, m_volumeHeader.version);
    writer.writeU8(0x21, m_volumeHeader.minVersion);
    writer.writeU8(0x22, m_volumeHeader.access);
    writer.writeU8(0x23, m_volumeHeader.entryLength);
    writer.writeU8(0x24, m_volumeHeader.entriesPerBlock);
    writer.writeU16LE(0x25, m_volumeHeader.fileCount);
    writer.writeU16LE(0x27, m_volumeHeader.bitmapPointer);
    writer.writeU16LE(0x29, m_volumeHeader.totalBlocks);

    writeBlock(VOLUME_DIR_BLOCK, block);
}

//=============================================================================
// Bitmap Operations
//=============================================================================

bool AppleProDOSHandler::parseVolumeBitmap() {
    m_bitmap.clear();
    m_bitmap.resize(m_blockLimit, false);

    // Calculate number of bitmap blocks needed
    size_t bitsNeeded = m_blockLimit;
    size_t blocksNeeded = (bitsNeeded + (BLOCK_SIZE * 8) - 1) / (BLOCK_SIZE * 8);

    for (size_t i = 0; i < blocksNeeded; ++i) {
        auto block = readBlock(m_volumeHeader.bitmapPointer + i);
        if (block.size() < BLOCK_SIZE) {
            return false;
        }

        for (size_t byte = 0; byte < BLOCK_SIZE; ++byte) {
            for (int bit = 7; bit >= 0; --bit) {
                size_t blockNum = i * BLOCK_SIZE * 8 + byte * 8 + (7 - bit);
                if (blockNum < m_blockLimit) {
                    // In ProDOS bitmap, 1 = free, 0 = used
                    m_bitmap[blockNum] = (block[byte] & (1 << bit)) != 0;
                }
            }
        }
    }

    return true;
}

void AppleProDOSHandler::writeVolumeBitmap() {
    size_t bitsNeeded = m_blockLimit;
    size_t blocksNeeded = (bitsNeeded + (BLOCK_SIZE * 8) - 1) / (BLOCK_SIZE * 8);

    for (size_t i = 0; i < blocksNeeded; ++i) {
        std::vector<uint8_t> block(BLOCK_SIZE, 0);

        for (size_t byte = 0; byte < BLOCK_SIZE; ++byte) {
            uint8_t value = 0;
            for (int bit = 7; bit >= 0; --bit) {
                size_t blockNum = i * BLOCK_SIZE * 8 + byte * 8 + (7 - bit);
                if (blockNum < m_blockLimit && m_bitmap[blockNum]) {
                    value |= (1 << bit);
                }
            }
            block[byte] = value;
        }

        writeBlock(m_volumeHeader.bitmapPointer + i, block);
    }
}

bool AppleProDOSHandler::isBlockFree(size_t block) const {
    if (block >= m_bitmap.size()) {
        return false;
    }
    return m_bitmap[block];
}

void AppleProDOSHandler::markBlockUsed(size_t block) {
    if (block < m_bitmap.size()) {
        m_bitmap[block] = false;
    }
}

void AppleProDOSHandler::markBlockFree(size_t block) {
    if (block < m_bitmap.size()) {
        m_bitmap[block] = true;
    }
}

size_t AppleProDOSHandler::allocateBlock() {
    // Find first free block (skip boot blocks and system blocks)
    for (size_t i = 7; i < m_bitmap.size(); ++i) {
        if (m_bitmap[i]) {
            markBlockUsed(i);
            return i;
        }
    }
    return 0;  // No free blocks
}

size_t AppleProDOSHandler::countFreeBlocks() const {
    size_t count = 0;
    for (size_t i = 0; i < m_bitmap.size(); ++i) {
        if (m_bitmap[i]) {
            ++count;
        }
    }
    return count;
}

//=============================================================================
// Directory Operations
//=============================================================================

std::vector<AppleProDOSHandler::DirectoryEntry> AppleProDOSHandler::readDirectory(uint16_t keyBlock) const {
    std::vector<DirectoryEntry> entries;

    uint16_t currentBlock = keyBlock;
    bool firstBlock = true;

    DirectoryChainGuard chain;
    while (currentBlock != 0) {
        chain.visit(currentBlock);
        auto block = readBlock(currentBlock);
        if (block.size() < BLOCK_SIZE) {
            break;
        }

        // Get prev/next block pointers
        uint16_t nextBlock = block[2] | (block[3] << 8);

        // First entry starts at offset 4
        // In first block, first entry is the directory/volume header
        size_t startEntry = firstBlock ? 1 : 0;
        size_t offset = 4 + (startEntry * DIR_ENTRY_SIZE);

        for (size_t i = startEntry; i < ENTRIES_PER_BLOCK && offset + DIR_ENTRY_SIZE <= BLOCK_SIZE; ++i) {
            DirectoryEntry entry;
            std::memset(&entry, 0, sizeof(entry));

            uint8_t storageAndLength = block[offset];
            entry.storageType = (storageAndLength >> 4) & 0x0F;
            entry.nameLength = storageAndLength & 0x0F;

            if (entry.storageType != STORAGE_DELETED && entry.nameLength > 0) {
                if (entry.nameLength > MAX_FILENAME_LENGTH) {
                    entry.nameLength = MAX_FILENAME_LENGTH;
                }
                std::memcpy(entry.filename, &block[offset + 1], entry.nameLength);
                entry.filename[entry.nameLength] = '\0';

                entry.fileType = block[offset + 0x10];
                entry.keyPointer = block[offset + 0x11] | (block[offset + 0x12] << 8);
                entry.blocksUsed = block[offset + 0x13] | (block[offset + 0x14] << 8);
                entry.eof = block[offset + 0x15] | (block[offset + 0x16] << 8) |
                           (block[offset + 0x17] << 16);
                entry.creationDateTime = block[offset + 0x18] | (block[offset + 0x19] << 8) |
                                        (block[offset + 0x1A] << 16) | (block[offset + 0x1B] << 24);
                entry.version = block[offset + 0x1C];
                entry.minVersion = block[offset + 0x1D];
                entry.access = block[offset + 0x1E];
                entry.auxType = block[offset + 0x1F] | (block[offset + 0x20] << 8);
                entry.lastModDateTime = block[offset + 0x21] | (block[offset + 0x22] << 8) |
                                       (block[offset + 0x23] << 16) | (block[offset + 0x24] << 24);
                entry.headerPointer = block[offset + 0x25] | (block[offset + 0x26] << 8);

                entries.push_back(entry);
            }

            offset += DIR_ENTRY_SIZE;
        }

        firstBlock = false;
        currentBlock = nextBlock;
    }

    return entries;
}

bool AppleProDOSHandler::locateDirectoryEntry(uint16_t dirKeyBlock, size_t entryIndex,
                                               uint16_t& block, size_t& slot) const {
    // Entry indexes skip the header (slot 0 of the key block); later blocks
    // use all 13 slots. Same order as findFreeDirectoryEntry.
    uint16_t currentBlock = dirKeyBlock;
    size_t entriesSeen = 0;
    bool firstBlock = true;
    DirectoryChainGuard chain;
    while (currentBlock != 0) {
        chain.visit(currentBlock);
        auto data = readBlock(currentBlock);
        if (data.size() < BLOCK_SIZE) {
            return false;
        }
        for (size_t i = firstBlock ? 1 : 0; i < ENTRIES_PER_BLOCK; ++i) {
            if (entriesSeen == entryIndex) {
                block = currentBlock;
                slot = i;
                return true;
            }
            ++entriesSeen;
        }
        firstBlock = false;
        currentBlock = static_cast<uint16_t>(data[2] | (data[3] << 8));
    }
    return false;
}

bool AppleProDOSHandler::writeDirectoryEntry(uint16_t dirKeyBlock, size_t entryIndex, const DirectoryEntry& entry) {
    uint16_t currentBlock = 0;
    size_t slot = 0;
    if (!locateDirectoryEntry(dirKeyBlock, entryIndex, currentBlock, slot)) {
        return false;
    }
    auto block = readBlock(currentBlock);
    if (block.size() < BLOCK_SIZE) {
        return false;
    }
    const size_t offset = 4 + (slot * DIR_ENTRY_SIZE);

    block[offset] = (entry.storageType << 4) | (entry.nameLength & 0x0F);
    std::memcpy(&block[offset + 1], entry.filename, entry.nameLength);
    // Pad with zeros
    for (size_t j = entry.nameLength; j < MAX_FILENAME_LENGTH; ++j) {
        block[offset + 1 + j] = 0;
    }

    block[offset + 0x10] = entry.fileType;
    block[offset + 0x11] = entry.keyPointer & 0xFF;
    block[offset + 0x12] = (entry.keyPointer >> 8) & 0xFF;
    block[offset + 0x13] = entry.blocksUsed & 0xFF;
    block[offset + 0x14] = (entry.blocksUsed >> 8) & 0xFF;
    block[offset + 0x15] = entry.eof & 0xFF;
    block[offset + 0x16] = (entry.eof >> 8) & 0xFF;
    block[offset + 0x17] = (entry.eof >> 16) & 0xFF;
    block[offset + 0x18] = entry.creationDateTime & 0xFF;
    block[offset + 0x19] = (entry.creationDateTime >> 8) & 0xFF;
    block[offset + 0x1A] = (entry.creationDateTime >> 16) & 0xFF;
    block[offset + 0x1B] = (entry.creationDateTime >> 24) & 0xFF;
    block[offset + 0x1C] = entry.version;
    block[offset + 0x1D] = entry.minVersion;
    block[offset + 0x1E] = entry.access;
    block[offset + 0x1F] = entry.auxType & 0xFF;
    block[offset + 0x20] = (entry.auxType >> 8) & 0xFF;
    block[offset + 0x21] = entry.lastModDateTime & 0xFF;
    block[offset + 0x22] = (entry.lastModDateTime >> 8) & 0xFF;
    block[offset + 0x23] = (entry.lastModDateTime >> 16) & 0xFF;
    block[offset + 0x24] = (entry.lastModDateTime >> 24) & 0xFF;
    block[offset + 0x25] = entry.headerPointer & 0xFF;
    block[offset + 0x26] = (entry.headerPointer >> 8) & 0xFF;
    writeBlock(currentBlock, block);
    return true;
}

int AppleProDOSHandler::findDirectoryEntry(uint16_t dirKeyBlock, const std::string& filename) const {
    // Returns PHYSICAL entry index (counting all slots including deleted ones)
    // This is critical for writeDirectoryEntry() to work correctly

    std::string upperFilename = filename;
    std::transform(upperFilename.begin(), upperFilename.end(), upperFilename.begin(), ::toupper);

    uint16_t currentBlock = dirKeyBlock;
    int physicalIndex = 0;
    bool firstBlock = true;

    DirectoryChainGuard chain;
    while (currentBlock != 0) {
        chain.visit(currentBlock);
        auto block = readBlock(currentBlock);
        if (block.size() < BLOCK_SIZE) {
            break;
        }

        uint16_t nextBlock = block[2] | (block[3] << 8);

        // First entry in first block is the directory/volume header, skip it
        size_t startEntry = firstBlock ? 1 : 0;

        for (size_t i = startEntry; i < ENTRIES_PER_BLOCK; ++i) {
            size_t offset = 4 + (i * DIR_ENTRY_SIZE);

            uint8_t storageAndLength = block[offset];
            uint8_t storageType = (storageAndLength >> 4) & 0x0F;
            uint8_t nameLength = storageAndLength & 0x0F;

            // Check if this is a valid (non-deleted) entry with matching name
            if (storageType != STORAGE_DELETED && nameLength > 0) {
                if (nameLength > MAX_FILENAME_LENGTH) {
                    nameLength = MAX_FILENAME_LENGTH;
                }

                char entryFilename[MAX_FILENAME_LENGTH + 1];
                std::memcpy(entryFilename, &block[offset + 1], nameLength);
                entryFilename[nameLength] = '\0';

                std::string entryName(entryFilename, nameLength);
                std::transform(entryName.begin(), entryName.end(), entryName.begin(), ::toupper);

                if (entryName == upperFilename) {
                    return physicalIndex;
                }
            }

            ++physicalIndex;
        }

        firstBlock = false;
        currentBlock = nextBlock;
    }

    return -1;
}

std::optional<AppleProDOSHandler::DirectoryEntry> AppleProDOSHandler::readDirectoryEntryAt(
    uint16_t dirKeyBlock, size_t physicalIndex) const {
    // Read a single directory entry at the given physical index

    uint16_t currentBlock = dirKeyBlock;
    size_t entriesSeen = 0;
    bool firstBlock = true;

    DirectoryChainGuard chain;
    while (currentBlock != 0) {
        chain.visit(currentBlock);
        auto block = readBlock(currentBlock);
        if (block.size() < BLOCK_SIZE) {
            return std::nullopt;
        }

        uint16_t nextBlock = block[2] | (block[3] << 8);
        size_t startEntry = firstBlock ? 1 : 0;

        for (size_t i = startEntry; i < ENTRIES_PER_BLOCK; ++i) {
            if (entriesSeen == physicalIndex) {
                size_t offset = 4 + (i * DIR_ENTRY_SIZE);

                DirectoryEntry entry;
                std::memset(&entry, 0, sizeof(entry));

                uint8_t storageAndLength = block[offset];
                entry.storageType = (storageAndLength >> 4) & 0x0F;
                entry.nameLength = storageAndLength & 0x0F;

                if (entry.nameLength > MAX_FILENAME_LENGTH) {
                    entry.nameLength = MAX_FILENAME_LENGTH;
                }
                std::memcpy(entry.filename, &block[offset + 1], entry.nameLength);
                entry.filename[entry.nameLength] = '\0';

                entry.fileType = block[offset + 0x10];
                entry.keyPointer = block[offset + 0x11] | (block[offset + 0x12] << 8);
                entry.blocksUsed = block[offset + 0x13] | (block[offset + 0x14] << 8);
                entry.eof = block[offset + 0x15] | (block[offset + 0x16] << 8) |
                           (block[offset + 0x17] << 16);
                entry.creationDateTime = block[offset + 0x18] | (block[offset + 0x19] << 8) |
                                        (block[offset + 0x1A] << 16) | (block[offset + 0x1B] << 24);
                entry.version = block[offset + 0x1C];
                entry.minVersion = block[offset + 0x1D];
                entry.access = block[offset + 0x1E];
                entry.auxType = block[offset + 0x1F] | (block[offset + 0x20] << 8);
                entry.lastModDateTime = block[offset + 0x21] | (block[offset + 0x22] << 8) |
                                       (block[offset + 0x23] << 16) | (block[offset + 0x24] << 24);
                entry.headerPointer = block[offset + 0x25] | (block[offset + 0x26] << 8);

                return entry;
            }
            ++entriesSeen;
        }

        firstBlock = false;
        currentBlock = nextBlock;
    }

    return std::nullopt;
}

int AppleProDOSHandler::findFreeDirectoryEntry(uint16_t dirKeyBlock) const {
    uint16_t currentBlock = dirKeyBlock;
    int entryIndex = 0;
    bool firstBlock = true;

    DirectoryChainGuard chain;
    while (currentBlock != 0) {
        chain.visit(currentBlock);
        auto block = readBlock(currentBlock);
        if (block.size() < BLOCK_SIZE) {
            return -1;
        }

        uint16_t nextBlock = block[2] | (block[3] << 8);
        size_t startEntry = firstBlock ? 1 : 0;

        for (size_t i = startEntry; i < ENTRIES_PER_BLOCK; ++i) {
            size_t offset = 4 + (i * DIR_ENTRY_SIZE);
            uint8_t storageType = (block[offset] >> 4) & 0x0F;

            if (storageType == STORAGE_DELETED) {
                return entryIndex;
            }
            ++entryIndex;
        }

        firstBlock = false;
        currentBlock = nextBlock;
    }

    return -1;  // No free entries
}

bool AppleProDOSHandler::updateDirectoryFileCount(uint16_t dirKeyBlock, int delta) {
    if (dirKeyBlock == VOLUME_DIR_BLOCK) {
        // Update volume header file count
        if (delta > 0) {
            m_volumeHeader.fileCount += delta;
        } else if (delta < 0 && m_volumeHeader.fileCount >= static_cast<uint16_t>(-delta)) {
            m_volumeHeader.fileCount += delta;
        }
        writeVolumeHeader();
        return true;
    }

    // Update subdirectory header file count
    auto block = readBlock(dirKeyBlock);
    if (block.size() < BLOCK_SIZE) {
        return false;
    }

    // Read current file count from subdirectory header (offset 0x25-0x26)
    uint16_t fileCount = block[0x25] | (block[0x26] << 8);

    // Update file count
    if (delta > 0) {
        fileCount += delta;
    } else if (delta < 0 && fileCount >= static_cast<uint16_t>(-delta)) {
        fileCount += delta;
    }

    // Write updated file count
    block[0x25] = fileCount & 0xFF;
    block[0x26] = (fileCount >> 8) & 0xFF;

    writeBlock(dirKeyBlock, block);
    return true;
}

//=============================================================================
// File I/O Operations
//=============================================================================

std::vector<uint16_t> AppleProDOSHandler::dataBlockSlots(const DirectoryEntry& entry) const {
    const size_t slots = (entry.eof + BLOCK_SIZE - 1) / BLOCK_SIZE;
    std::vector<uint16_t> out(slots, 0);
    auto readIndex = [this](uint16_t block) {
        auto index = readBlock(block);
        if (index.size() < BLOCK_SIZE) {
            throw ReadException("Cannot read ProDOS index block " + std::to_string(block));
        }
        return index;
    };

    switch (entry.storageType) {
        case STORAGE_SEEDLING:
            if (slots > 0) {
                out[0] = entry.keyPointer;
            }
            break;

        case STORAGE_SAPLING: {
            // Index block: pointer i = low byte at i, high byte at 256 + i
            const auto index = readIndex(entry.keyPointer);
            for (size_t i = 0; i < slots && i < 256; ++i) {
                out[i] = index[i] | (index[256 + i] << 8);
            }
            break;
        }

        case STORAGE_TREE: {
            // Master index: 256 data blocks per index block; a zero index
            // pointer stands for 256 unallocated data blocks
            const auto master = readIndex(entry.keyPointer);
            for (size_t mi = 0; mi * 256 < slots && mi < 256; ++mi) {
                const uint16_t indexBlock = master[mi] | (master[256 + mi] << 8);
                if (indexBlock == 0) {
                    continue;
                }
                const auto index = readIndex(indexBlock);
                for (size_t i = 0; i < 256 && mi * 256 + i < slots; ++i) {
                    out[mi * 256 + i] = index[i] | (index[256 + i] << 8);
                }
            }
            break;
        }

        default:
            break;
    }
    return out;
}

std::vector<uint16_t> AppleProDOSHandler::getFileBlocks(const DirectoryEntry& entry) const {
    // Allocated data blocks only (a seedling's key block always counts)
    if (entry.storageType == STORAGE_SEEDLING) {
        return entry.keyPointer != 0 ? std::vector<uint16_t>{entry.keyPointer}
                                     : std::vector<uint16_t>{};
    }
    std::vector<uint16_t> blocks;
    for (uint16_t b : dataBlockSlots(entry)) {
        if (b != 0) {
            blocks.push_back(b);
        }
    }
    return blocks;
}

std::vector<uint8_t> AppleProDOSHandler::readFileData(const DirectoryEntry& entry) const {
    std::vector<uint8_t> data;
    data.reserve(entry.eof);

    // Sparse (zero-pointer) blocks keep their place in the file as zeros
    for (uint16_t blockNum : dataBlockSlots(entry)) {
        if (blockNum == 0) {
            data.insert(data.end(), BLOCK_SIZE, 0);
            continue;
        }
        auto block = readBlock(blockNum);
        if (block.size() < BLOCK_SIZE) {
            throw ReadException("Cannot read ProDOS data block " + std::to_string(blockNum));
        }
        data.insert(data.end(), block.begin(), block.end());
    }

    // Trim to exact file size
    if (data.size() > entry.eof) {
        data.resize(entry.eof);
    }

    return data;
}

uint8_t AppleProDOSHandler::calculateStorageType(size_t size) const {
    if (size <= BLOCK_SIZE) {
        return STORAGE_SEEDLING;
    } else if (size <= BLOCK_SIZE * 256) {
        return STORAGE_SAPLING;
    } else {
        return STORAGE_TREE;
    }
}

bool AppleProDOSHandler::writeFileData(uint16_t keyBlock, uint8_t storageType, const std::vector<uint8_t>& data) {
    switch (storageType) {
        case STORAGE_SEEDLING: {
            // Write data directly to key block
            std::vector<uint8_t> blockData(BLOCK_SIZE, 0);
            size_t copySize = std::min(data.size(), BLOCK_SIZE);
            std::copy(data.begin(), data.begin() + copySize, blockData.begin());
            writeBlock(keyBlock, blockData);
            return true;
        }

        case STORAGE_SAPLING: {
            // keyBlock is the index block
            std::vector<uint8_t> indexBlock(BLOCK_SIZE, 0);
            size_t dataOffset = 0;
            size_t blockIndex = 0;

            while (dataOffset < data.size() && blockIndex < 256) {
                size_t newBlock = allocateBlock();
                if (newBlock == 0) {
                    return false;
                }

                // Write data block
                std::vector<uint8_t> blockData(BLOCK_SIZE, 0);
                size_t copySize = std::min(BLOCK_SIZE, data.size() - dataOffset);
                std::copy(data.begin() + dataOffset, data.begin() + dataOffset + copySize, blockData.begin());
                writeBlock(newBlock, blockData);

                // Update index block
                indexBlock[blockIndex] = newBlock & 0xFF;
                indexBlock[256 + blockIndex] = (newBlock >> 8) & 0xFF;

                dataOffset += BLOCK_SIZE;
                ++blockIndex;
            }

            writeBlock(keyBlock, indexBlock);
            return true;
        }

        case STORAGE_TREE: {
            // keyBlock is the master index block
            std::vector<uint8_t> masterBlock(BLOCK_SIZE, 0);
            size_t dataOffset = 0;
            size_t masterIndex = 0;

            while (dataOffset < data.size() && masterIndex < 256) {
                // Allocate index block
                size_t indexBlockNum = allocateBlock();
                if (indexBlockNum == 0) {
                    return false;
                }

                std::vector<uint8_t> indexBlock(BLOCK_SIZE, 0);
                size_t blockIndex = 0;

                while (dataOffset < data.size() && blockIndex < 256) {
                    size_t dataBlockNum = allocateBlock();
                    if (dataBlockNum == 0) {
                        return false;
                    }

                    // Write data block
                    std::vector<uint8_t> blockData(BLOCK_SIZE, 0);
                    size_t copySize = std::min(BLOCK_SIZE, data.size() - dataOffset);
                    std::copy(data.begin() + dataOffset, data.begin() + dataOffset + copySize, blockData.begin());
                    writeBlock(dataBlockNum, blockData);

                    // Update index block
                    indexBlock[blockIndex] = dataBlockNum & 0xFF;
                    indexBlock[256 + blockIndex] = (dataBlockNum >> 8) & 0xFF;

                    dataOffset += BLOCK_SIZE;
                    ++blockIndex;
                }

                writeBlock(indexBlockNum, indexBlock);

                // Update master block
                masterBlock[masterIndex] = indexBlockNum & 0xFF;
                masterBlock[256 + masterIndex] = (indexBlockNum >> 8) & 0xFF;

                ++masterIndex;
            }

            writeBlock(keyBlock, masterBlock);
            return true;
        }

        default:
            return false;
    }
}

void AppleProDOSHandler::freeFileBlocks(const DirectoryEntry& entry) {
    switch (entry.storageType) {
        case STORAGE_SEEDLING: {
            if (entry.keyPointer != 0) {
                markBlockFree(entry.keyPointer);
            }
            break;
        }

        case STORAGE_SAPLING: {
            auto indexBlock = readBlock(entry.keyPointer);
            if (indexBlock.size() >= BLOCK_SIZE) {
                for (size_t i = 0; i < 256; ++i) {
                    uint16_t dataBlock = indexBlock[i] | (indexBlock[256 + i] << 8);
                    if (dataBlock != 0) {
                        markBlockFree(dataBlock);
                    }
                }
            }
            markBlockFree(entry.keyPointer);
            break;
        }

        case STORAGE_TREE: {
            auto masterBlock = readBlock(entry.keyPointer);
            if (masterBlock.size() >= BLOCK_SIZE) {
                for (size_t mi = 0; mi < 256; ++mi) {
                    uint16_t indexBlockNum = masterBlock[mi] | (masterBlock[256 + mi] << 8);
                    if (indexBlockNum == 0) {
                        continue;
                    }

                    auto indexBlock = readBlock(indexBlockNum);
                    if (indexBlock.size() >= BLOCK_SIZE) {
                        for (size_t i = 0; i < 256; ++i) {
                            uint16_t dataBlock = indexBlock[i] | (indexBlock[256 + i] << 8);
                            if (dataBlock != 0) {
                                markBlockFree(dataBlock);
                            }
                        }
                    }
                    markBlockFree(indexBlockNum);
                }
            }
            markBlockFree(entry.keyPointer);
            break;
        }

        default:
            break;
    }
}

//=============================================================================
// Path Handling
//=============================================================================

std::pair<uint16_t, std::string> AppleProDOSHandler::resolvePath(const std::string& path) const {
    // Strip leading slash or volume name
    std::string cleanPath = path;
    if (!cleanPath.empty() && cleanPath[0] == '/') {
        cleanPath = cleanPath.substr(1);
    }

    // Check if path starts with volume name
    std::string volName = formatFilename(m_volumeHeader.name, m_volumeHeader.nameLength);
    if (cleanPath.length() > volName.length() &&
        cleanPath.substr(0, volName.length()) == volName &&
        cleanPath[volName.length()] == '/') {
        cleanPath = cleanPath.substr(volName.length() + 1);
    }

    // Find last path separator
    size_t lastSlash = cleanPath.rfind('/');
    if (lastSlash == std::string::npos) {
        // No subdirectory, return volume directory
        return {VOLUME_DIR_BLOCK, cleanPath};
    }

    // Navigate to subdirectory
    uint16_t currentDir = VOLUME_DIR_BLOCK;
    size_t pos = 0;

    while (pos < lastSlash) {
        size_t nextSlash = cleanPath.find('/', pos);
        if (nextSlash == std::string::npos || nextSlash > lastSlash) {
            nextSlash = lastSlash;
        }

        std::string dirName = cleanPath.substr(pos, nextSlash - pos);
        int entryIndex = findDirectoryEntry(currentDir, dirName);

        if (entryIndex < 0) {
            return {0, ""};  // Directory not found
        }

        // Read entry at physical index (not from filtered list)
        auto entryOpt = readDirectoryEntryAt(currentDir, static_cast<size_t>(entryIndex));
        if (!entryOpt) {
            return {0, ""};
        }

        const auto& entry = *entryOpt;
        if (!entry.isDirectory()) {
            return {0, ""};  // Not a directory
        }

        currentDir = entry.keyPointer;
        pos = nextSlash + 1;
    }

    std::string filename = cleanPath.substr(lastSlash + 1);
    return {currentDir, filename};
}

std::string AppleProDOSHandler::formatFilename(const char* name, uint8_t length) const {
    if (length > MAX_FILENAME_LENGTH) {
        length = MAX_FILENAME_LENGTH;
    }
    return std::string(name, length);
}

void AppleProDOSHandler::parseFilename(const std::string& filename, char* name, uint8_t& length) const {
    length = static_cast<uint8_t>(std::min(filename.length(), static_cast<size_t>(MAX_FILENAME_LENGTH)));
    std::memset(name, 0, MAX_FILENAME_LENGTH);

    for (uint8_t i = 0; i < length; ++i) {
        name[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(filename[i])));
    }
}

bool AppleProDOSHandler::isValidFilename(const std::string& filename) const {
    if (filename.empty() || filename.length() > MAX_FILENAME_LENGTH) {
        return false;
    }

    // First character must be a letter
    if (!std::isalpha(static_cast<unsigned char>(filename[0]))) {
        return false;
    }

    // Rest must be alphanumeric or period
    for (size_t i = 1; i < filename.length(); ++i) {
        char c = filename[i];
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.') {
            return false;
        }
    }

    return true;
}

//=============================================================================
// Date/Time Handling
//=============================================================================

uint32_t AppleProDOSHandler::packDateTime(std::time_t time) {
    struct tm* tm = localtime(&time);
    if (!tm) {
        return 0;
    }

    // ProDOS date format:
    // Bytes 0-1: Date: YYYYYYYM MMMDDDDD
    // Bytes 2-3: Time: 000HHHHH 00MMMMMM

    uint16_t year = (tm->tm_year >= 100) ? (tm->tm_year - 100) : tm->tm_year;
    if (year > 127) year = 127;

    uint16_t date = (year << 9) | ((tm->tm_mon + 1) << 5) | tm->tm_mday;
    uint16_t timeVal = (tm->tm_hour << 8) | tm->tm_min;

    return date | (static_cast<uint32_t>(timeVal) << 16);
}

std::time_t AppleProDOSHandler::unpackDateTime(uint32_t packed) {
    uint16_t date = packed & 0xFFFF;
    uint16_t timeVal = (packed >> 16) & 0xFFFF;

    int year = (date >> 9) & 0x7F;
    int month = (date >> 5) & 0x0F;
    int day = date & 0x1F;
    int hour = (timeVal >> 8) & 0x1F;
    int minute = timeVal & 0x3F;

    // Convert to time_t
    struct tm tm = {};
    tm.tm_year = year + 100;  // Years since 1900, ProDOS uses years since 2000 for 0-39
    if (year >= 40) {
        tm.tm_year = year;  // 1940-1999
    }
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_isdst = -1;

    return mktime(&tm);
}

//=============================================================================
// File Type Handling
//=============================================================================

std::string AppleProDOSHandler::fileTypeToString(uint8_t type) const {
    switch (type) {
        case FILETYPE_UNK: return "UNK";
        case FILETYPE_BAD: return "BAD";
        case FILETYPE_TXT: return "TXT";
        case FILETYPE_BIN: return "BIN";
        case FILETYPE_DIR: return "DIR";
        case FILETYPE_ADB: return "ADB";
        case FILETYPE_AWP: return "AWP";
        case FILETYPE_ASP: return "ASP";
        case FILETYPE_INT: return "INT";
        case FILETYPE_BAS: return "BAS";
        case FILETYPE_VAR: return "VAR";
        case FILETYPE_REL: return "REL";
        case FILETYPE_SYS: return "SYS";
        case FILETYPE_CMD: return "CMD";
        default: {
            static const char hex[] = "0123456789ABCDEF";
            return std::string("$") + hex[type >> 4] + hex[type & 0x0F];
        }
    }
}

FileEntry AppleProDOSHandler::directoryEntryToFileEntry(const DirectoryEntry& entry) const {
    FileEntry fe;
    fe.name = formatFilename(entry.filename, entry.nameLength);
    fe.size = entry.eof;
    fe.fileType = entry.fileType;
    fe.loadAddress = entry.auxType;
    fe.isDirectory = entry.isDirectory();
    fe.isDeleted = entry.isDeleted();
    fe.attributes = entry.access;
    fe.typeName = entry.isDirectory() ? "DIR" : fileTypeToString(entry.fileType);
    // BASIC.SYSTEM CAT shows a file as locked ('*') when its write bit is
    // clear; LOCK sets access $21, UNLOCK $E3
    fe.locked = (entry.access & 0x02) == 0;

    if (entry.creationDateTime != 0) {
        fe.createdTime = unpackDateTime(entry.creationDateTime);
    }
    if (entry.lastModDateTime != 0) {
        fe.modifiedTime = unpackDateTime(entry.lastModDateTime);
    }

    return fe;
}

//=============================================================================
// Public Interface Implementation
//=============================================================================

std::vector<FileEntry> AppleProDOSHandler::listFiles(const std::string& path) {
    std::vector<FileEntry> files;

    uint16_t dirBlock = VOLUME_DIR_BLOCK;

    if (!path.empty() && path != "/" && path != getVolumeName()) {
        auto [resolvedDir, filename] = resolvePath(path + "/dummy");
        if (resolvedDir == 0) {
            // Try as a subdirectory
            int entryIndex = findDirectoryEntry(VOLUME_DIR_BLOCK, path);
            if (entryIndex >= 0) {
                // Read entry at physical index
                auto entryOpt = readDirectoryEntryAt(VOLUME_DIR_BLOCK, static_cast<size_t>(entryIndex));
                if (entryOpt && entryOpt->isDirectory()) {
                    dirBlock = entryOpt->keyPointer;
                }
            }
        } else {
            dirBlock = resolvedDir;
        }
    }

    auto entries = readDirectory(dirBlock);
    for (const auto& entry : entries) {
        if (!entry.isDeleted() && !entry.isVolumeHeader() && !entry.isSubdirHeader()) {
            files.push_back(directoryEntryToFileEntry(entry));
        }
    }

    return files;
}

std::vector<uint8_t> AppleProDOSHandler::readFile(const std::string& filename) {
    auto [dirBlock, name] = resolvePath(filename);
    if (dirBlock == 0 && name.empty()) {
        // Try root directory
        dirBlock = VOLUME_DIR_BLOCK;
        name = filename;
    }

    int entryIndex = findDirectoryEntry(dirBlock, name);
    if (entryIndex < 0) {
        throw FileNotFoundException(filename);
    }

    // Read entry at physical index (not from filtered list)
    auto entryOpt = readDirectoryEntryAt(dirBlock, static_cast<size_t>(entryIndex));
    if (!entryOpt) {
        throw FileNotFoundException(filename);
    }

    const DirectoryEntry& entry = *entryOpt;
    if (entry.isDirectory()) {
        throw InvalidFormatException("Cannot read directory as file");
    }

    return readFileData(entry);
}

// Helper function to convert DOS 3.3 file type to ProDOS file type
static uint8_t convertDOS33ToProDOSFileType(uint8_t dos33Type) {
    // DOS 3.3 file type codes → ProDOS file type codes
    switch (dos33Type) {
        case 0x00:  // DOS 3.3 Text (T)
            return AppleConstants::ProDOS::FILETYPE_TXT;  // 0x04
        case 0x01:  // DOS 3.3 Integer BASIC (I)
            return AppleConstants::ProDOS::FILETYPE_INT;  // 0xFA
        case 0x02:  // DOS 3.3 Applesoft BASIC (A)
            return AppleConstants::ProDOS::FILETYPE_BAS;  // 0xFC
        case 0x04:  // DOS 3.3 Binary (B)
            return AppleConstants::ProDOS::FILETYPE_BIN;  // 0x06
        case 0x10:  // DOS 3.3 Relocatable (R)
            return AppleConstants::ProDOS::FILETYPE_REL;  // 0xFE
        default:
            // If already a ProDOS type or unknown, return as-is
            return dos33Type;
    }
}

// Resolve a user file type for ProDOS: the names SYS/CMD/BIN/TXT/BAS/INT/REL,
// the DOS 3.3 letters T/I/A/B/R (mapped to their ProDOS equivalents), or a
// hex ProDOS type code used as is. Callers that pass only a numeric type keep
// the old DOS 3.3 -> ProDOS mapping.
static uint8_t resolveProDOSFileType(const FileMetadata& metadata) {
    std::string name;
    for (char c : metadata.fileTypeName) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            name += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }

    if (name.empty()) {
        return metadata.fileType != 0 ? convertDOS33ToProDOSFileType(metadata.fileType)
                                      : AppleConstants::ProDOS::FILETYPE_BIN;
    }

    if (name == "TXT" || name == "T") return AppleConstants::ProDOS::FILETYPE_TXT;
    if (name == "BIN" || name == "B") return AppleConstants::ProDOS::FILETYPE_BIN;
    if (name == "BAS" || name == "A") return AppleConstants::ProDOS::FILETYPE_BAS;
    if (name == "INT" || name == "I") return AppleConstants::ProDOS::FILETYPE_INT;
    if (name == "REL" || name == "R") return AppleConstants::ProDOS::FILETYPE_REL;
    if (name == "SYS") return AppleConstants::ProDOS::FILETYPE_SYS;
    if (name == "CMD") return AppleConstants::ProDOS::FILETYPE_CMD;

    std::string hex;
    if (name.size() > 1 && name[0] == '$') {
        hex = name.substr(1);
    } else if (name.size() > 2 && name[0] == '0' && name[1] == 'X') {
        hex = name.substr(2);
    }
    if (!hex.empty() && hex.size() <= 2 &&
        hex.find_first_not_of("0123456789ABCDEF") == std::string::npos) {
        return static_cast<uint8_t>(std::stoul(hex, nullptr, 16));
    }

    throw DiskException(DiskError::InvalidParameter, "File type '" + metadata.fileTypeName +
                                 "' is not available on ProDOS "
                                 "(use SYS/CMD/BIN/TXT/BAS/INT/REL, T/I/A/B/R or a hex code)");
}

bool AppleProDOSHandler::writeFile(const std::string& filename,
                                    const std::vector<uint8_t>& data,
                                    const FileMetadata& metadata) {
    // Resolve the type before anything on the disk changes
    const uint8_t resolvedType = resolveProDOSFileType(metadata);

    // First resolve path to get directory and base filename
    auto [dirBlock, name] = resolvePath(filename);
    if (dirBlock == 0 && name.empty()) {
        dirBlock = VOLUME_DIR_BLOCK;
        name = filename;
    }

    // Validate only the base filename (not the full path)
    if (!isValidFilename(name)) {
        throw InvalidFilenameException(name);
    }

    // Check if file already exists
    int existingIndex = findDirectoryEntry(dirBlock, name);
    if (existingIndex >= 0) {
        // Delete existing file first
        deleteFile(filename);
    }

    // Calculate storage type and blocks needed
    uint8_t storageType = calculateStorageType(data.size());
    size_t blocksNeeded = 1;  // At least key block

    if (storageType == STORAGE_SEEDLING) {
        blocksNeeded = 1;
    } else if (storageType == STORAGE_SAPLING) {
        blocksNeeded = 1 + ((data.size() + BLOCK_SIZE - 1) / BLOCK_SIZE);
    } else if (storageType == STORAGE_TREE) {
        size_t dataBlocks = (data.size() + BLOCK_SIZE - 1) / BLOCK_SIZE;
        size_t indexBlocks = (dataBlocks + 255) / 256;
        blocksNeeded = 1 + indexBlocks + dataBlocks;
    }

    // Check free space
    if (countFreeBlocks() < blocksNeeded) {
        throw DiskFullException();
    }

    // Find free directory entry
    int freeEntry = findFreeDirectoryEntry(dirBlock);
    if (freeEntry < 0) {
        throw DirectoryFullException();
    }

    // Allocate key block
    size_t keyBlock = allocateBlock();
    if (keyBlock == 0) {
        throw DiskFullException();
    }

    // Write file data
    if (!writeFileData(static_cast<uint16_t>(keyBlock), storageType, data)) {
        markBlockFree(keyBlock);
        throw WriteException("Failed to write file data");
    }

    // Create directory entry
    DirectoryEntry entry;
    std::memset(&entry, 0, sizeof(entry));
    entry.storageType = storageType;
    parseFilename(name, entry.filename, entry.nameLength);
    entry.fileType = resolvedType;
    entry.keyPointer = static_cast<uint16_t>(keyBlock);
    entry.blocksUsed = static_cast<uint16_t>(blocksNeeded);
    entry.eof = static_cast<uint32_t>(data.size());
    entry.creationDateTime = packDateTime(std::time(nullptr));
    entry.lastModDateTime = entry.creationDateTime;
    entry.version = 0;
    entry.minVersion = 0;
    entry.access = ACCESS_DEFAULT;
    entry.auxType = metadata.loadAddress;
    entry.headerPointer = dirBlock;

    if (!writeDirectoryEntry(dirBlock, freeEntry, entry)) {
        freeFileBlocks(entry);
        throw WriteException("Failed to write directory entry");
    }

    // Update file count in the parent directory (volume or subdirectory)
    updateDirectoryFileCount(dirBlock, +1);

    // Write bitmap
    writeVolumeBitmap();

    return true;
}

bool AppleProDOSHandler::deleteFile(const std::string& filename) {
    auto [dirBlock, name] = resolvePath(filename);
    if (dirBlock == 0 && name.empty()) {
        dirBlock = VOLUME_DIR_BLOCK;
        name = filename;
    }

    int entryIndex = findDirectoryEntry(dirBlock, name);
    if (entryIndex < 0) {
        return false;
    }

    // Read entry at physical index (not from filtered list)
    auto entryOpt = readDirectoryEntryAt(dirBlock, static_cast<size_t>(entryIndex));
    if (!entryOpt) {
        return false;
    }

    const DirectoryEntry& entry = *entryOpt;

    // Handle directories via deleteDirectory
    if (entry.isDirectory()) {
        return deleteDirectory(filename);
    }

    // Free file blocks
    freeFileBlocks(entry);

    // Mark directory entry as deleted — zero the entire entry
    // ProDOS expects the first byte (storageType|nameLength) to be 0x00 for deleted entries.
    // Keeping nameLength non-zero can confuse ProDOS 2.4.3's startup file search.
    DirectoryEntry deletedEntry;
    std::memset(&deletedEntry, 0, sizeof(deletedEntry));
    deletedEntry.storageType = STORAGE_DELETED;
    deletedEntry.nameLength = 0;

    if (!writeDirectoryEntry(dirBlock, entryIndex, deletedEntry)) {
        return false;
    }

    // Update file count in the parent directory (volume or subdirectory)
    updateDirectoryFileCount(dirBlock, -1);

    // Write bitmap
    writeVolumeBitmap();

    return true;
}

bool AppleProDOSHandler::renameFile(const std::string& oldName, const std::string& newName) {
    // Resolve new name path first to get base filename
    auto [newDirBlock, newFileName] = resolvePath(newName);
    if (newDirBlock == 0 && newFileName.empty()) {
        newDirBlock = VOLUME_DIR_BLOCK;
        newFileName = newName;
    }

    // Validate only the base filename (not the full path)
    if (!isValidFilename(newFileName)) {
        return false;
    }

    auto [dirBlock, name] = resolvePath(oldName);
    if (dirBlock == 0 && name.empty()) {
        dirBlock = VOLUME_DIR_BLOCK;
        name = oldName;
    }

    // Cross-directory rename not supported
    if (dirBlock != newDirBlock) {
        return false;
    }

    // Check if new name already exists in the same directory
    if (findDirectoryEntry(dirBlock, newFileName) >= 0) {
        return false;  // New name already exists
    }

    int entryIndex = findDirectoryEntry(dirBlock, name);
    if (entryIndex < 0) {
        return false;
    }

    // Read entry at physical index (not from filtered list)
    auto entryOpt = readDirectoryEntryAt(dirBlock, static_cast<size_t>(entryIndex));
    if (!entryOpt) {
        return false;
    }

    DirectoryEntry entry = *entryOpt;
    parseFilename(newFileName, entry.filename, entry.nameLength);
    entry.lastModDateTime = packDateTime(std::time(nullptr));

    if (!writeDirectoryEntry(dirBlock, entryIndex, entry)) {
        return false;
    }

    // Sync subdirectory header name if renaming a directory.
    // ProDOS subdirectories store their own name in the header entry at the
    // start of the key block.  Without this update the parent entry and the
    // header would disagree, which can confuse ProDOS utilities.
    if (entry.storageType == STORAGE_SUBDIRECTORY) {
        auto block = readBlock(entry.keyPointer);
        if (block.size() >= BLOCK_SIZE) {
            block[0x04] = (STORAGE_SUBDIR_HEADER << 4) | (entry.nameLength & 0x0F);
            std::memcpy(&block[0x05], entry.filename, entry.nameLength);
            for (size_t i = entry.nameLength; i < MAX_FILENAME_LENGTH; ++i) {
                block[0x05 + i] = 0;
            }
            writeBlock(entry.keyPointer, block);
        }
    }

    return true;
}

size_t AppleProDOSHandler::getFreeSpace() const {
    return countFreeBlocks() * BLOCK_SIZE;
}

size_t AppleProDOSHandler::getTotalSpace() const {
    // Space files can use: the volume minus boot blocks 0-1, the four volume
    // directory blocks and the bitmap blocks (like the FAT / HFS handlers'
    // data area)
    const size_t overhead = 2 + 4 + (m_blockLimit + 4095) / 4096;
    return m_blockLimit > overhead ? (m_blockLimit - overhead) * BLOCK_SIZE : 0;
}

bool AppleProDOSHandler::fileExists(const std::string& filename) const {
    auto [dirBlock, name] = resolvePath(filename);
    if (dirBlock == 0 && name.empty()) {
        dirBlock = VOLUME_DIR_BLOCK;
        name = filename;
    }

    return findDirectoryEntry(dirBlock, name) >= 0;
}

bool AppleProDOSHandler::format(const std::string& volumeName) {
    if (!m_disk) {
        return false;
    }

    // Initialize volume header
    std::memset(&m_volumeHeader, 0, sizeof(m_volumeHeader));
    m_volumeHeader.storageType = STORAGE_VOLUME_HEADER;

    // Volume names follow the file name rules (Tech Ref 2.1): 1-15 of A-Z,
    // 0-9 and '.', starting with a letter; refused rather than cut or kept
    std::string name = volumeName.empty() ? "BLANK" : volumeName;
    if (!isValidFilename(name)) {
        throw InvalidFilenameException(name);
    }
    m_volumeHeader.nameLength = static_cast<uint8_t>(name.length());
    for (size_t i = 0; i < name.length(); ++i) {
        m_volumeHeader.name[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[i])));
    }

    m_volumeHeader.creationDateTime = packDateTime(std::time(nullptr));
    m_volumeHeader.version = 0;
    m_volumeHeader.minVersion = 0;
    m_volumeHeader.access = ACCESS_DEFAULT;
    m_volumeHeader.entryLength = DIR_ENTRY_SIZE;
    m_volumeHeader.entriesPerBlock = ENTRIES_PER_BLOCK;
    m_volumeHeader.fileCount = 0;
    m_volumeHeader.bitmapPointer = BITMAP_BLOCK;
    // The volume fills the image (140K: 280 blocks, 800K: 1600)
    m_volumeHeader.totalBlocks = static_cast<uint16_t>(imageBlocks());
    m_blockLimit = m_volumeHeader.totalBlocks;
    const size_t bitmapBlocks = (m_blockLimit + 4095) / 4096;  // one per 4096 blocks

    // Initialize bitmap - all blocks free except system blocks
    m_bitmap.clear();
    m_bitmap.resize(m_blockLimit, true);

    // Mark boot blocks as used (0-1)
    m_bitmap[0] = false;
    m_bitmap[1] = false;

    // Mark volume directory blocks as used (2-5)
    for (size_t i = 2; i <= 5; ++i) {
        m_bitmap[i] = false;
    }

    // Mark bitmap blocks as used
    for (size_t i = 0; i < bitmapBlocks; ++i) {
        m_bitmap[BITMAP_BLOCK + i] = false;
    }

    // Write boot blocks (zeros)
    std::vector<uint8_t> bootBlock(BLOCK_SIZE, 0);
    writeBlock(0, bootBlock);
    writeBlock(1, bootBlock);

    // Write volume directory
    // Block 2: Volume header + first entries
    std::vector<uint8_t> volDirBlock(BLOCK_SIZE, 0);

    // Prev/Next pointers
    volDirBlock[0] = 0;  // Prev = 0
    volDirBlock[1] = 0;
    volDirBlock[2] = 3;  // Next = 3
    volDirBlock[3] = 0;

    // Volume header entry at offset 4
    size_t offset = 4;
    volDirBlock[offset] = (STORAGE_VOLUME_HEADER << 4) | m_volumeHeader.nameLength;
    std::memcpy(&volDirBlock[offset + 1], m_volumeHeader.name, m_volumeHeader.nameLength);

    // Reserved bytes (0x14-0x1B)
    // Creation date/time
    volDirBlock[0x1C] = m_volumeHeader.creationDateTime & 0xFF;
    volDirBlock[0x1D] = (m_volumeHeader.creationDateTime >> 8) & 0xFF;
    volDirBlock[0x1E] = (m_volumeHeader.creationDateTime >> 16) & 0xFF;
    volDirBlock[0x1F] = (m_volumeHeader.creationDateTime >> 24) & 0xFF;

    volDirBlock[0x20] = m_volumeHeader.version;
    volDirBlock[0x21] = m_volumeHeader.minVersion;
    volDirBlock[0x22] = m_volumeHeader.access;
    volDirBlock[0x23] = m_volumeHeader.entryLength;
    volDirBlock[0x24] = m_volumeHeader.entriesPerBlock;
    volDirBlock[0x25] = m_volumeHeader.fileCount & 0xFF;
    volDirBlock[0x26] = (m_volumeHeader.fileCount >> 8) & 0xFF;
    volDirBlock[0x27] = m_volumeHeader.bitmapPointer & 0xFF;
    volDirBlock[0x28] = (m_volumeHeader.bitmapPointer >> 8) & 0xFF;
    volDirBlock[0x29] = m_volumeHeader.totalBlocks & 0xFF;
    volDirBlock[0x2A] = (m_volumeHeader.totalBlocks >> 8) & 0xFF;

    writeBlock(VOLUME_DIR_BLOCK, volDirBlock);

    // Write remaining directory blocks (3-5)
    for (size_t i = 3; i <= 5; ++i) {
        std::vector<uint8_t> dirBlock(BLOCK_SIZE, 0);
        dirBlock[0] = static_cast<uint8_t>(i - 1);  // Prev
        dirBlock[1] = 0;
        if (i < 5) {
            dirBlock[2] = static_cast<uint8_t>(i + 1);  // Next
        }
        dirBlock[3] = 0;
        writeBlock(i, dirBlock);
    }

    // Write bitmap
    writeVolumeBitmap();

    return true;
}

std::string AppleProDOSHandler::getVolumeName() const {
    return "/" + formatFilename(m_volumeHeader.name, m_volumeHeader.nameLength);
}

//=============================================================================
// Subdirectory Support
//=============================================================================

bool AppleProDOSHandler::createDirectory(const std::string& path) {
    // First resolve path to get parent directory and directory name
    auto [parentBlock, dirName] = resolvePath(path);
    if (parentBlock == 0 && dirName.empty()) {
        parentBlock = VOLUME_DIR_BLOCK;
        dirName = path;
    }

    // Validate only the directory name (not the full path)
    if (dirName.empty() || !isValidFilename(dirName)) {
        return false;
    }

    // Check if already exists
    if (findDirectoryEntry(parentBlock, dirName) >= 0) {
        return false;  // Already exists
    }

    // Allocate a block for the new subdirectory
    size_t newDirBlock = allocateBlock();
    if (newDirBlock == 0) {
        return false;  // No free blocks
    }

    // Initialize the subdirectory block
    std::vector<uint8_t> dirBlock(BLOCK_SIZE, 0);

    // Prev/Next pointers (no linked blocks for now)
    dirBlock[0] = 0;
    dirBlock[1] = 0;
    dirBlock[2] = 0;
    dirBlock[3] = 0;

    // Subdirectory header entry at offset 4
    size_t offset = 4;
    uint8_t nameLen = static_cast<uint8_t>(std::min(dirName.length(), MAX_FILENAME_LENGTH));
    dirBlock[offset] = (STORAGE_SUBDIR_HEADER << 4) | nameLen;

    // Copy uppercase name
    for (size_t i = 0; i < nameLen; ++i) {
        dirBlock[offset + 1 + i] = static_cast<uint8_t>(
            std::toupper(static_cast<unsigned char>(dirName[i])));
    }

    // Set creation date/time at 0x1C
    uint32_t now = packDateTime(std::time(nullptr));
    dirBlock[0x1C] = now & 0xFF;
    dirBlock[0x1D] = (now >> 8) & 0xFF;
    dirBlock[0x1E] = (now >> 16) & 0xFF;
    dirBlock[0x1F] = (now >> 24) & 0xFF;

    // Version, min version, access as ProDOS 2.4.3 CREATE writes a directory
    // (measured in MAME: header version $24, min 0, access $C3; the entry in
    // the parent gets version $24, min 0, access $E3 = backup bit set)
    dirBlock[0x20] = CREATE_DIR_VERSION;
    dirBlock[0x21] = 0;
    dirBlock[0x22] = ACCESS_DEFAULT;

    // Reserved bytes 0x14-0x1B as ProDOS 8 writes them (measured: ProDOS 2.4.3
    // CREATE gives 75 <version> <min_version> C3 27 0D 00 00; the manual's
    // B.2.2 "must be set ... to prevent I/O ERROR" list)
    const uint8_t reserved[8] = {0x75, dirBlock[0x20], dirBlock[0x21], 0xC3, 0x27, 0x0D, 0x00, 0x00};
    std::memcpy(&dirBlock[0x14], reserved, sizeof(reserved));

    // Entry length, entries per block
    dirBlock[0x23] = DIR_ENTRY_SIZE;
    dirBlock[0x24] = ENTRIES_PER_BLOCK;

    // File count (initially 0)
    dirBlock[0x25] = 0;
    dirBlock[0x26] = 0;

    // Parent pointer / entry number: set below, once the entry's place is known

    // Parent entry length
    dirBlock[0x2A] = DIR_ENTRY_SIZE;

    writeBlock(newDirBlock, dirBlock);

    // Create entry in parent directory
    int freeEntry = findFreeDirectoryEntry(parentBlock);
    if (freeEntry < 0) {
        // No free entries - restore block
        markBlockFree(newDirBlock);
        writeVolumeBitmap();
        return false;
    }

    DirectoryEntry newEntry;
    std::memset(&newEntry, 0, sizeof(newEntry));
    newEntry.storageType = STORAGE_SUBDIRECTORY;
    parseFilename(dirName, newEntry.filename, newEntry.nameLength);
    newEntry.fileType = FILETYPE_DIR;
    newEntry.keyPointer = static_cast<uint16_t>(newDirBlock);
    newEntry.blocksUsed = 1;
    newEntry.eof = BLOCK_SIZE;
    newEntry.creationDateTime = now;
    newEntry.lastModDateTime = now;
    newEntry.version = CREATE_DIR_VERSION;
    newEntry.minVersion = 0;
    newEntry.access = ACCESS_DEFAULT | ACCESS_BACKUP;
    newEntry.headerPointer = parentBlock;

    if (!writeDirectoryEntry(parentBlock, freeEntry, newEntry)) {
        markBlockFree(newDirBlock);
        writeVolumeBitmap();
        return false;
    }

    // Where the entry went (B.2.3): parent_pointer = the directory block that
    // holds it, parent_entry_number = its slot in that block + 1 (slot 0 of
    // the key block is the header, entry 1)
    uint16_t entryBlock = 0;
    size_t entrySlot = 0;
    if (!locateDirectoryEntry(parentBlock, static_cast<size_t>(freeEntry), entryBlock, entrySlot)) {
        return false;
    }
    dirBlock[0x27] = entryBlock & 0xFF;
    dirBlock[0x28] = (entryBlock >> 8) & 0xFF;
    dirBlock[0x29] = static_cast<uint8_t>(entrySlot + 1);
    writeBlock(newDirBlock, dirBlock);

    // Update file count in the parent directory (volume or subdirectory)
    updateDirectoryFileCount(parentBlock, +1);

    writeVolumeBitmap();
    return true;
}

bool AppleProDOSHandler::deleteDirectory(const std::string& path) {
    auto [parentBlock, dirName] = resolvePath(path);
    if (parentBlock == 0 && dirName.empty()) {
        parentBlock = VOLUME_DIR_BLOCK;
        dirName = path;
    }

    if (dirName.empty()) {
        return false;
    }

    int entryIndex = findDirectoryEntry(parentBlock, dirName);
    if (entryIndex < 0) {
        return false;
    }

    // Read entry at physical index (not from filtered list)
    auto entryOpt = readDirectoryEntryAt(parentBlock, static_cast<size_t>(entryIndex));
    if (!entryOpt) {
        return false;
    }

    const DirectoryEntry& entry = *entryOpt;

    // Must be a directory
    if (!entry.isDirectory()) {
        return false;
    }

    // Check if directory is empty
    auto dirEntries = readDirectory(entry.keyPointer);
    for (const auto& subEntry : dirEntries) {
        if (!subEntry.isDeleted() && !subEntry.isSubdirHeader()) {
            return false;  // Directory not empty
        }
    }

    // Free the directory block
    markBlockFree(entry.keyPointer);

    // Mark directory entry as deleted — zero the entire entry
    // ProDOS expects the first byte (storageType|nameLength) to be 0x00 for deleted entries.
    DirectoryEntry deletedEntry;
    std::memset(&deletedEntry, 0, sizeof(deletedEntry));
    deletedEntry.storageType = STORAGE_DELETED;
    deletedEntry.nameLength = 0;

    if (!writeDirectoryEntry(parentBlock, entryIndex, deletedEntry)) {
        return false;
    }

    // Update file count in the parent directory (volume or subdirectory)
    updateDirectoryFileCount(parentBlock, -1);

    writeVolumeBitmap();
    return true;
}

bool AppleProDOSHandler::isDirectory(const std::string& path) const {
    if (path.empty() || path == "/") {
        return true;  // Root is always a directory
    }

    auto [parentBlock, targetName] = resolvePath(path);

    if (targetName.empty()) {
        return true;  // Path resolved to root
    }

    int entryIndex = findDirectoryEntry(parentBlock, targetName);
    if (entryIndex < 0) {
        return false;  // Not found
    }

    auto entryOpt = readDirectoryEntryAt(parentBlock, static_cast<size_t>(entryIndex));
    if (!entryOpt) {
        return false;
    }

    return entryOpt->isDirectory();
}

std::vector<std::string> AppleProDOSHandler::mountWarnings() const {
    if (m_volumeHeader.totalBlocks > imageBlocks()) {
        return {"ProDOS total_blocks " + std::to_string(m_volumeHeader.totalBlocks) +
                " exceeds the image (" + std::to_string(imageBlocks()) + " blocks); using " +
                std::to_string(m_blockLimit)};
    }
    return {};
}

ValidationResult AppleProDOSHandler::validateExtended() const {
    ValidationResult result;

    if (!m_disk) {
        result.addError("Disk image not loaded");
        return result;
    }

    // 1. Validate Volume Header
    if (m_volumeHeader.storageType != STORAGE_VOLUME_HEADER) {
        result.addError("Invalid volume header storage type", "Block 2");
    }

    if (m_volumeHeader.nameLength == 0 || m_volumeHeader.nameLength > MAX_FILENAME_LENGTH) {
        result.addError("Invalid volume name length: " + std::to_string(m_volumeHeader.nameLength), "Block 2");
    }

    if (m_volumeHeader.entryLength != DIR_ENTRY_SIZE) {
        result.addWarning("Non-standard entry length: " + std::to_string(m_volumeHeader.entryLength), "Block 2");
    }

    if (m_volumeHeader.entriesPerBlock != ENTRIES_PER_BLOCK) {
        result.addWarning("Non-standard entries per block: " + std::to_string(m_volumeHeader.entriesPerBlock), "Block 2");
    }

    // 2. Validate Bitmap Pointer
    if (m_volumeHeader.bitmapPointer == 0 || m_volumeHeader.bitmapPointer >= m_blockLimit) {
        result.addError("Invalid bitmap pointer: " + std::to_string(m_volumeHeader.bitmapPointer), "Block 2");
    }

    // 3. Validate Total Blocks
    if (m_volumeHeader.totalBlocks == 0 || m_volumeHeader.totalBlocks > imageBlocks()) {
        result.addWarning("Unusual total blocks: " + std::to_string(m_volumeHeader.totalBlocks), "Block 2");
    }

    // 4. Blocks in use: boot blocks 0-1, every block of every directory file,
    // the bitmap, and every index / master / data block a file points to
    // (B.2-B.3; the same rule as tests/tools/a2_prodos_ref.py check)
    std::vector<uint8_t> refs(m_blockLimit, 0);   // references per block (capped)
    auto use = [&](size_t block, const std::string& who) {
        if (block >= m_blockLimit) {
            result.addError("Block " + std::to_string(block) + " outside the volume", who);
            return;
        }
        if (refs[block] == 1) {
            result.addWarning("Block " + std::to_string(block) + " referenced multiple times", who);
        }
        if (refs[block] < 2) {
            ++refs[block];
        }
    };
    use(0, "Boot blocks");
    use(1, "Boot blocks");
    const size_t bitmapBlocks = (m_blockLimit + 4095) / 4096;  // 4096 bits per block
    for (size_t i = 0; i < bitmapBlocks; ++i) {
        use(static_cast<size_t>(m_volumeHeader.bitmapPointer) + i, "Volume bitmap");
    }

    // Every block of a directory file, following the next pointers
    auto directoryBlocks = [&](uint16_t keyBlock) {
        std::vector<uint16_t> blocks;
        DirectoryChainGuard chain;
        for (uint16_t b = keyBlock; b != 0;) {
            chain.visit(b);
            auto data = readBlock(b);
            if (data.size() < BLOCK_SIZE) {
                throw ReadException("Cannot read directory block " + std::to_string(b));
            }
            blocks.push_back(b);
            b = static_cast<uint16_t>(data[2] | (data[3] << 8));
        }
        return blocks;
    };

    // Index pointer i: low byte at i, high byte at 256 + i
    auto indexPointers = [&](uint16_t block, size_t count) {
        auto data = readBlock(block);
        if (data.size() < BLOCK_SIZE) {
            throw ReadException("Cannot read index block " + std::to_string(block));
        }
        std::vector<uint16_t> out;
        for (size_t i = 0; i < count; ++i) {
            out.push_back(static_cast<uint16_t>(data[i] | (data[256 + i] << 8)));
        }
        return out;
    };

    // 5. Validate directory structure and file blocks
    std::unordered_set<uint16_t> directoriesSeen;
    std::function<void(uint16_t, const std::string&)> validateDirectory;
    validateDirectory = [&](uint16_t keyBlock, const std::string& path) {
        const std::string where = path.empty() ? "Volume directory" : path;
        if (keyBlock >= m_blockLimit) {
            result.addError("Directory key block out of range: " + std::to_string(keyBlock), where);
            return;
        }
        if (!directoriesSeen.insert(keyBlock).second) {
            result.addError("Directory loops back to block " + std::to_string(keyBlock), where);
            return;
        }

        std::vector<DirectoryEntry> entries;
        try {
            for (uint16_t b : directoryBlocks(keyBlock)) {
                use(b, where);
            }
            entries = readDirectory(keyBlock);
        } catch (const std::exception& e) {
            result.addError(e.what(), where);
            return;
        }

        // file_count of a directory = its own active entries (B.2.2 / B.2.3)
        size_t active = 0;
        for (const auto& entry : entries) {
            if (!entry.isDeleted() && !entry.isVolumeHeader() && !entry.isSubdirHeader()) {
                ++active;
            }
        }
        uint16_t headerCount = m_volumeHeader.fileCount;
        if (keyBlock != VOLUME_DIR_BLOCK) {
            const auto key = readBlock(keyBlock);
            headerCount = key.size() >= BLOCK_SIZE ? static_cast<uint16_t>(key[0x25] | (key[0x26] << 8)) : 0;
        }
        if (active != headerCount) {
            result.addWarning("File count mismatch: header says " + std::to_string(headerCount) +
                              ", found " + std::to_string(active),
                              path.empty() ? "Volume header" : path);
        }
        for (const auto& entry : entries) {
            if (entry.isDeleted() || entry.isVolumeHeader() || entry.isSubdirHeader()) {
                continue;
            }

            std::string filename = formatFilename(entry.filename, entry.nameLength);
            std::string fullPath = path.empty() ? filename : path + "/" + filename;

            // Validate key pointer
            if (entry.keyPointer >= m_blockLimit) {
                result.addError("File key block out of range: " + std::to_string(entry.keyPointer), fullPath);
                continue;
            }

            // Recurse into subdirectories (their blocks are counted there)
            if (entry.isDirectory()) {
                validateDirectory(entry.keyPointer, fullPath);
                continue;
            }

            // Mark the file's blocks as used: key, index / master and data blocks
            try {
                use(entry.keyPointer, fullPath);
                if (entry.storageType == STORAGE_SAPLING) {
                    for (uint16_t b : indexPointers(entry.keyPointer, 256)) {
                        if (b != 0) use(b, fullPath);
                    }
                } else if (entry.storageType == STORAGE_TREE) {
                    for (uint16_t ib : indexPointers(entry.keyPointer, 128)) {
                        if (ib == 0) continue;
                        use(ib, fullPath);
                        if (ib >= m_blockLimit) continue;
                        for (uint16_t b : indexPointers(ib, 256)) {
                            if (b != 0) use(b, fullPath);
                        }
                    }
                }
            } catch (const std::exception& e) {
                result.addError("Error reading file blocks: " + std::string(e.what()), fullPath);
            }
        }
    };

    validateDirectory(VOLUME_DIR_BLOCK, "");

    // 6. File counts are compared per directory in validateDirectory

    // 7. Verify bitmap matches used blocks
    std::vector<size_t> lost;
    for (size_t i = 0; i < m_blockLimit; ++i) {
        const bool bitmapSaysFree = isBlockFree(i);
        if (bitmapSaysFree && refs[i] != 0) {
            result.addError("Block " + std::to_string(i) + " is used but marked free in bitmap");
        } else if (!bitmapSaysFree && refs[i] == 0) {
            lost.push_back(i);
        }
    }
    if (!lost.empty()) {
        std::string list;
        for (size_t k = 0; k < lost.size() && k < 10; ++k) {
            list += (k ? ", " : "") + std::to_string(lost[k]);
        }
        result.addWarning(std::to_string(lost.size()) +
                          " block(s) marked used in bitmap but not referenced: " + list +
                          (lost.size() > 10 ? ", ..." : ""));
    }

    // 8. Validate boot blocks (Block 0 and 1)
    auto block0 = readBlock(0);
    if (block0.size() == BLOCK_SIZE) {
        // Check for obvious corruption patterns
        bool allFF = true;
        bool allZero = true;
        for (size_t i = 0; i < BLOCK_SIZE; ++i) {
            if (block0[i] != 0xFF) allFF = false;
            if (block0[i] != 0x00) allZero = false;
        }
        if (allFF) {
            result.addError("Boot block 0 appears corrupted (all 0xFF)", "Block 0");
        }
        // All zeros is valid for unbootable disk (data disks created by rdedisktool)
        if (allZero) {
            result.addInfo("Boot block 0 is empty (disk is not bootable - normal for data disks)");
        }
    }

    auto block1 = readBlock(1);
    if (block1.size() == BLOCK_SIZE) {
        // Check for corruption patterns in boot block 1
        // A valid ProDOS boot block should not start with 0xFF
        // This typically indicates data corruption (e.g., partial overwrite)
        if (block1[0] == 0xFF) {
            // Count how many leading bytes are 0xFF
            size_t ffCount = 0;
            for (size_t i = 0; i < std::min(static_cast<size_t>(64), BLOCK_SIZE); ++i) {
                if (block1[i] == 0xFF) ffCount++;
                else break;
            }

            if (ffCount >= 64) {
                result.addError("Boot block 1 appears corrupted (64+ bytes of 0xFF)", "Block 1");
            } else {
                // Even a single 0xFF at the start indicates corruption
                // Valid ProDOS boot blocks don't start with 0xFF
                result.addError("Boot block 1 corruption detected at offset 0 (0xFF pattern)", "Block 1");
            }
        }
    }

    if (result.errorCount == 0 && result.warningCount == 0) {
        result.addInfo("All validation checks passed");
    }

    return result;
}

} // namespace rde
