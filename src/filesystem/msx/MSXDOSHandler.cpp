/**
 * MSX-DOS File System Handler
 *
 * Full implementation of FAT12 file system operations for MSX-DOS.
 *
 * Structure:
 * - Boot sector (sector 0) - Contains BPB
 * - FAT tables (usually 2 copies)
 * - Root directory
 * - Data area
 */

#include "rdedisktool/filesystem/MSXDOSHandler.h"
#include "rdedisktool/Exceptions.h"
#include <algorithm>
#include <cstring>
#include <cctype>

namespace rde {

MSXDOSHandler::MSXDOSHandler() = default;

FileSystemType MSXDOSHandler::getType() const {
    return FileSystemType::MSXDOS1;
}

bool MSXDOSHandler::initialize(DiskImage* disk) {
    if (!disk) {
        return false;
    }
    m_disk = disk;
    return parseBPB();
}

bool MSXDOSHandler::parseBPB() {
    if (!m_disk) {
        return false;
    }

    // Read boot sector (sector 0)
    auto bootSector = m_disk->readSector(0, 0, 0);
    if (bootSector.size() < 512) {
        return false;
    }

    // Parse BPB (BIOS Parameter Block) starting at offset 0x0B
    m_bytesPerSector = bootSector[0x0B] | (static_cast<uint16_t>(bootSector[0x0C]) << 8);
    m_sectorsPerCluster = bootSector[0x0D];
    m_reservedSectors = bootSector[0x0E] | (static_cast<uint16_t>(bootSector[0x0F]) << 8);
    m_numberOfFATs = bootSector[0x10];
    m_rootEntryCount = bootSector[0x11] | (static_cast<uint16_t>(bootSector[0x12]) << 8);
    m_totalSectors = bootSector[0x13] | (static_cast<uint16_t>(bootSector[0x14]) << 8);
    m_mediaDescriptor = bootSector[0x15];
    m_sectorsPerFAT = bootSector[0x16] | (static_cast<uint16_t>(bootSector[0x17]) << 8);
    m_sectorsPerTrack = bootSector[0x18] | (static_cast<uint16_t>(bootSector[0x19]) << 8);
    m_numberOfHeads = bootSector[0x1A] | (static_cast<uint16_t>(bootSector[0x1B]) << 8);

    // Validate BPB
    if (m_bytesPerSector == 0 || m_sectorsPerCluster == 0 ||
        m_numberOfFATs == 0 || m_sectorsPerFAT == 0) {
        // Use default values for standard MSX disk
        m_bytesPerSector = 512;
        m_sectorsPerCluster = 2;
        m_reservedSectors = 1;
        m_numberOfFATs = 2;
        m_rootEntryCount = 112;
        m_sectorsPerFAT = 3;
        m_sectorsPerTrack = 9;
        m_numberOfHeads = 2;

        // Calculate total sectors from disk geometry
        auto geom = m_disk->getGeometry();
        m_totalSectors = static_cast<uint16_t>(geom.totalSectors());
    }

    // Calculate derived values
    m_rootDirSectors = ((m_rootEntryCount * 32) + (m_bytesPerSector - 1)) / m_bytesPerSector;
    m_firstDataSector = m_reservedSectors + (m_numberOfFATs * m_sectorsPerFAT) + m_rootDirSectors;
    m_dataSectors = m_totalSectors - m_firstDataSector;
    m_totalClusters = m_dataSectors / m_sectorsPerCluster;

    return true;
}

std::vector<uint8_t> MSXDOSHandler::readFAT() const {
    if (!m_disk) {
        return {};
    }

    std::vector<uint8_t> fat;
    fat.reserve(m_sectorsPerFAT * m_bytesPerSector);

    // Read all FAT sectors (first FAT copy)
    for (uint16_t i = 0; i < m_sectorsPerFAT; ++i) {
        uint16_t sector = m_reservedSectors + i;
        uint16_t track = sector / m_sectorsPerTrack;
        uint16_t head = 0;
        if (m_numberOfHeads > 1) {
            head = track % m_numberOfHeads;
            track /= m_numberOfHeads;
        }
        uint16_t sectorInTrack = sector % m_sectorsPerTrack;

        auto data = m_disk->readSector(track, head, sectorInTrack);
        fat.insert(fat.end(), data.begin(), data.end());
    }

    return fat;
}

void MSXDOSHandler::writeFAT(const std::vector<uint8_t>& fat) {
    if (!m_disk) {
        return;
    }

    // Write to all FAT copies
    for (uint8_t fatNum = 0; fatNum < m_numberOfFATs; ++fatNum) {
        for (uint16_t i = 0; i < m_sectorsPerFAT; ++i) {
            uint16_t sector = m_reservedSectors + (fatNum * m_sectorsPerFAT) + i;
            uint16_t track = sector / m_sectorsPerTrack;
            uint16_t head = 0;
            if (m_numberOfHeads > 1) {
                head = track % m_numberOfHeads;
                track /= m_numberOfHeads;
            }
            uint16_t sectorInTrack = sector % m_sectorsPerTrack;

            size_t offset = i * m_bytesPerSector;
            std::vector<uint8_t> sectorData(fat.begin() + offset,
                                            fat.begin() + offset + m_bytesPerSector);
            m_disk->writeSector(track, head, sectorInTrack, sectorData);
        }
    }
}

uint16_t MSXDOSHandler::getFATEntry(const std::vector<uint8_t>& fat, uint16_t cluster) const {
    // FAT12: 1.5 bytes per entry
    size_t offset = cluster + (cluster / 2);

    if (offset + 1 >= fat.size()) {
        return FAT12_EOF;
    }

    uint16_t value;
    if (cluster & 1) {
        // Odd cluster: high 4 bits of first byte, all 8 bits of second byte
        value = (fat[offset] >> 4) | (static_cast<uint16_t>(fat[offset + 1]) << 4);
    } else {
        // Even cluster: all 8 bits of first byte, low 4 bits of second byte
        value = fat[offset] | ((static_cast<uint16_t>(fat[offset + 1]) & 0x0F) << 8);
    }

    return value;
}

void MSXDOSHandler::setFATEntry(std::vector<uint8_t>& fat, uint16_t cluster, uint16_t value) {
    // FAT12: 1.5 bytes per entry
    size_t offset = cluster + (cluster / 2);

    if (offset + 1 >= fat.size()) {
        return;
    }

    if (cluster & 1) {
        // Odd cluster
        fat[offset] = (fat[offset] & 0x0F) | ((value & 0x0F) << 4);
        fat[offset + 1] = (value >> 4) & 0xFF;
    } else {
        // Even cluster
        fat[offset] = value & 0xFF;
        fat[offset + 1] = (fat[offset + 1] & 0xF0) | ((value >> 8) & 0x0F);
    }
}

std::vector<uint16_t> MSXDOSHandler::getClusterChain(uint16_t startCluster) const {
    std::vector<uint16_t> chain;
    auto fat = readFAT();

    uint16_t cluster = startCluster;
    while (cluster >= 2 && cluster < FAT12_EOF) {
        chain.push_back(cluster);

        // Prevent infinite loops
        if (chain.size() > m_totalClusters) {
            break;
        }

        cluster = getFATEntry(fat, cluster);
    }

    return chain;
}

uint16_t MSXDOSHandler::allocateCluster(std::vector<uint8_t>& fat) {
    // Find first free cluster (starting from cluster 2)
    for (uint16_t cluster = 2; cluster < m_totalClusters + 2; ++cluster) {
        if (getFATEntry(fat, cluster) == FAT12_FREE) {
            return cluster;
        }
    }
    return 0; // No free clusters
}

void MSXDOSHandler::freeClusterChain(std::vector<uint8_t>& fat, uint16_t startCluster) {
    uint16_t cluster = startCluster;
    while (cluster >= 2 && cluster < FAT12_EOF) {
        uint16_t next = getFATEntry(fat, cluster);
        setFATEntry(fat, cluster, FAT12_FREE);
        cluster = next;
    }
}

std::vector<uint8_t> MSXDOSHandler::readCluster(uint16_t cluster) const {
    if (!m_disk || cluster < 2) {
        return {};
    }

    std::vector<uint8_t> data;
    data.reserve(m_sectorsPerCluster * m_bytesPerSector);

    // Calculate first sector of cluster
    uint32_t firstSector = m_firstDataSector + ((cluster - 2) * m_sectorsPerCluster);

    for (uint8_t i = 0; i < m_sectorsPerCluster; ++i) {
        uint32_t sector = firstSector + i;
        uint16_t track = sector / m_sectorsPerTrack;
        uint16_t head = 0;
        if (m_numberOfHeads > 1) {
            head = track % m_numberOfHeads;
            track /= m_numberOfHeads;
        }
        uint16_t sectorInTrack = sector % m_sectorsPerTrack;

        auto sectorData = m_disk->readSector(track, head, sectorInTrack);
        data.insert(data.end(), sectorData.begin(), sectorData.end());
    }

    return data;
}

void MSXDOSHandler::writeCluster(uint16_t cluster, const std::vector<uint8_t>& data) {
    if (!m_disk || cluster < 2) {
        return;
    }

    // Calculate first sector of cluster
    uint32_t firstSector = m_firstDataSector + ((cluster - 2) * m_sectorsPerCluster);

    size_t offset = 0;
    for (uint8_t i = 0; i < m_sectorsPerCluster && offset < data.size(); ++i) {
        uint32_t sector = firstSector + i;
        uint16_t track = sector / m_sectorsPerTrack;
        uint16_t head = 0;
        if (m_numberOfHeads > 1) {
            head = track % m_numberOfHeads;
            track /= m_numberOfHeads;
        }
        uint16_t sectorInTrack = sector % m_sectorsPerTrack;

        std::vector<uint8_t> sectorData(m_bytesPerSector, 0);
        size_t copySize = std::min(static_cast<size_t>(m_bytesPerSector), data.size() - offset);
        std::copy(data.begin() + offset, data.begin() + offset + copySize, sectorData.begin());

        m_disk->writeSector(track, head, sectorInTrack, sectorData);
        offset += m_bytesPerSector;
    }
}

std::vector<MSXDOSHandler::DirEntry> MSXDOSHandler::readRootDirectory() const {
    if (!m_disk) {
        return {};
    }

    std::vector<DirEntry> entries;
    std::vector<uint8_t> dirData;
    dirData.reserve(m_rootDirSectors * m_bytesPerSector);

    // Read root directory sectors
    uint32_t firstDirSector = m_reservedSectors + (m_numberOfFATs * m_sectorsPerFAT);

    for (uint16_t i = 0; i < m_rootDirSectors; ++i) {
        uint32_t sector = firstDirSector + i;
        uint16_t track = sector / m_sectorsPerTrack;
        uint16_t head = 0;
        if (m_numberOfHeads > 1) {
            head = track % m_numberOfHeads;
            track /= m_numberOfHeads;
        }
        uint16_t sectorInTrack = sector % m_sectorsPerTrack;

        auto data = m_disk->readSector(track, head, sectorInTrack);
        dirData.insert(dirData.end(), data.begin(), data.end());
    }

    // Parse directory entries (32 bytes each)
    for (size_t offset = 0; offset + 32 <= dirData.size(); offset += 32) {
        DirEntry entry;
        std::memcpy(entry.name, &dirData[offset], 8);
        std::memcpy(entry.ext, &dirData[offset + 8], 3);
        entry.attr = dirData[offset + 11];
        std::memcpy(entry.reserved, &dirData[offset + 12], 10);
        entry.time = dirData[offset + 22] | (static_cast<uint16_t>(dirData[offset + 23]) << 8);
        entry.date = dirData[offset + 24] | (static_cast<uint16_t>(dirData[offset + 25]) << 8);
        entry.startCluster = dirData[offset + 26] | (static_cast<uint16_t>(dirData[offset + 27]) << 8);
        entry.fileSize = dirData[offset + 28] |
                        (static_cast<uint32_t>(dirData[offset + 29]) << 8) |
                        (static_cast<uint32_t>(dirData[offset + 30]) << 16) |
                        (static_cast<uint32_t>(dirData[offset + 31]) << 24);

        // Check for end of directory
        if (static_cast<uint8_t>(entry.name[0]) == DIR_END) {
            break;
        }

        entries.push_back(entry);
    }

    return entries;
}

void MSXDOSHandler::writeRootDirectory(const std::vector<DirEntry>& entries) {
    if (!m_disk) {
        return;
    }

    // Build directory data
    std::vector<uint8_t> dirData(m_rootDirSectors * m_bytesPerSector, 0);

    size_t offset = 0;
    for (const auto& entry : entries) {
        if (offset + 32 > dirData.size()) {
            break;
        }

        std::memcpy(&dirData[offset], entry.name, 8);
        std::memcpy(&dirData[offset + 8], entry.ext, 3);
        dirData[offset + 11] = entry.attr;
        std::memcpy(&dirData[offset + 12], entry.reserved, 10);
        dirData[offset + 22] = entry.time & 0xFF;
        dirData[offset + 23] = (entry.time >> 8) & 0xFF;
        dirData[offset + 24] = entry.date & 0xFF;
        dirData[offset + 25] = (entry.date >> 8) & 0xFF;
        dirData[offset + 26] = entry.startCluster & 0xFF;
        dirData[offset + 27] = (entry.startCluster >> 8) & 0xFF;
        dirData[offset + 28] = entry.fileSize & 0xFF;
        dirData[offset + 29] = (entry.fileSize >> 8) & 0xFF;
        dirData[offset + 30] = (entry.fileSize >> 16) & 0xFF;
        dirData[offset + 31] = (entry.fileSize >> 24) & 0xFF;

        offset += 32;
    }

    // Write root directory sectors
    uint32_t firstDirSector = m_reservedSectors + (m_numberOfFATs * m_sectorsPerFAT);

    for (uint16_t i = 0; i < m_rootDirSectors; ++i) {
        uint32_t sector = firstDirSector + i;
        uint16_t track = sector / m_sectorsPerTrack;
        uint16_t head = 0;
        if (m_numberOfHeads > 1) {
            head = track % m_numberOfHeads;
            track /= m_numberOfHeads;
        }
        uint16_t sectorInTrack = sector % m_sectorsPerTrack;

        size_t dataOffset = i * m_bytesPerSector;
        std::vector<uint8_t> sectorData(dirData.begin() + dataOffset,
                                        dirData.begin() + dataOffset + m_bytesPerSector);
        m_disk->writeSector(track, head, sectorInTrack, sectorData);
    }
}

int MSXDOSHandler::findDirectoryEntry(const std::vector<DirEntry>& entries,
                                      const std::string& filename) const {
    char name[8], ext[3];
    parseFilename(filename, name, ext);

    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];

        // Skip deleted and end entries
        if (static_cast<uint8_t>(entry.name[0]) == DIR_FREE ||
            static_cast<uint8_t>(entry.name[0]) == DIR_END) {
            continue;
        }

        // Skip volume labels (they are not files or directories)
        if (entry.attr & ATTR_VOLUME_ID) {
            continue;
        }

        // Compare name and extension
        if (std::memcmp(entry.name, name, 8) == 0 &&
            std::memcmp(entry.ext, ext, 3) == 0) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

std::string MSXDOSHandler::formatFilename(const char* name, const char* ext) const {
    std::string result;

    // Copy name, trimming trailing spaces
    for (int i = 7; i >= 0; --i) {
        if (name[i] != ' ') {
            result = std::string(name, i + 1);
            break;
        }
    }

    // Check for extension
    bool hasExt = false;
    for (int i = 0; i < 3; ++i) {
        if (ext[i] != ' ') {
            hasExt = true;
            break;
        }
    }

    if (hasExt) {
        result += '.';
        for (int i = 2; i >= 0; --i) {
            if (ext[i] != ' ') {
                result += std::string(ext, i + 1);
                break;
            }
        }
    }

    return result;
}

void MSXDOSHandler::parseFilename(const std::string& filename, char* name, char* ext) const {
    // Initialize with spaces
    std::memset(name, ' ', 8);
    std::memset(ext, ' ', 3);

    // Find extension separator
    size_t dotPos = filename.rfind('.');
    std::string baseName, extension;

    if (dotPos != std::string::npos && dotPos > 0) {
        baseName = filename.substr(0, dotPos);
        extension = filename.substr(dotPos + 1);
    } else {
        baseName = filename;
    }

    // Convert to uppercase and copy
    for (size_t i = 0; i < std::min(baseName.length(), static_cast<size_t>(8)); ++i) {
        name[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(baseName[i])));
    }

    for (size_t i = 0; i < std::min(extension.length(), static_cast<size_t>(3)); ++i) {
        ext[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(extension[i])));
    }
}

FileEntry MSXDOSHandler::dirEntryToFileEntry(const DirEntry& entry) const {
    FileEntry fe;
    fe.name = formatFilename(entry.name, entry.ext);
    fe.size = entry.fileSize;
    fe.isDirectory = (entry.attr & ATTR_DIRECTORY) != 0;

    // Store attributes as a bitmask
    fe.attributes = entry.attr;

    // Convert DOS date/time to Unix timestamp
    struct std::tm tm{};
    tm.tm_year = ((entry.date >> 9) & 0x7F) + 1980 - 1900;
    tm.tm_mon = ((entry.date >> 5) & 0x0F) - 1;
    tm.tm_mday = entry.date & 0x1F;
    tm.tm_hour = (entry.time >> 11) & 0x1F;
    tm.tm_min = (entry.time >> 5) & 0x3F;
    tm.tm_sec = (entry.time & 0x1F) * 2;

    fe.modifiedTime = std::mktime(&tm);

    return fe;
}

uint16_t MSXDOSHandler::countFreeClusters() const {
    auto fat = readFAT();
    uint16_t count = 0;

    for (uint16_t cluster = 2; cluster < m_totalClusters + 2; ++cluster) {
        if (getFATEntry(fat, cluster) == FAT12_FREE) {
            ++count;
        }
    }

    return count;
}

MSXDOSHandler::ClusterInfo MSXDOSHandler::getClusterInfo() const {
    ClusterInfo info;
    auto fat = readFAT();

    info.totalClusters = m_totalClusters;
    info.freeClusters = 0;
    info.usedClusters = 0;
    info.badClusters = 0;
    info.reservedClusters = 0;
    info.clusterMap.resize(m_totalClusters + 2, 0);

    // Clusters 0 and 1 are reserved (media descriptor and 0xFFF)
    info.clusterMap[0] = getFATEntry(fat, 0);
    info.clusterMap[1] = getFATEntry(fat, 1);
    info.reservedClusters = 2;

    // Analyze clusters 2 through totalClusters+1
    for (uint16_t cluster = 2; cluster < m_totalClusters + 2; ++cluster) {
        uint16_t entry = getFATEntry(fat, cluster);
        info.clusterMap[cluster] = entry;

        if (entry == FAT12_FREE) {
            info.freeClusters++;
        } else if (entry == FAT12_BAD) {
            info.badClusters++;
        } else if (entry >= FAT12_RESERVED && entry < FAT12_BAD) {
            info.reservedClusters++;
        } else {
            // Used cluster (either pointing to next cluster or EOF)
            info.usedClusters++;
        }
    }

    return info;
}

std::vector<FileEntry> MSXDOSHandler::listFiles(const std::string& path) {
    std::vector<FileEntry> files;

    // Determine which directory to list
    uint16_t dirCluster = 0;
    if (!path.empty() && path != "/" && path != "\\") {
        // Navigate to the specified directory
        auto [parentCluster, dirName] = resolvePath(path);

        if (!dirName.empty()) {
            // Find the directory entry and get its cluster
            auto parentEntries = getDirectoryEntries(parentCluster);
            int idx = findDirectoryEntry(parentEntries, dirName);
            if (idx < 0) {
                throw FileNotFoundException("Directory not found: " + path);
            }
            if (!(parentEntries[idx].attr & ATTR_DIRECTORY)) {
                throw DiskException(DiskError::InvalidParameter, "Not a directory: " + path);
            }
            dirCluster = parentEntries[idx].startCluster;
        } else {
            dirCluster = parentCluster;
        }
    }

    auto entries = getDirectoryEntries(dirCluster);

    for (const auto& entry : entries) {
        // Skip deleted entries
        if (static_cast<uint8_t>(entry.name[0]) == DIR_FREE) {
            continue;
        }
        // End of directory
        if (static_cast<uint8_t>(entry.name[0]) == DIR_END) {
            break;
        }
        // Skip volume label
        if (entry.attr & ATTR_VOLUME_ID) {
            continue;
        }
        // Skip . and .. entries
        if (entry.name[0] == '.') {
            continue;
        }

        files.push_back(dirEntryToFileEntry(entry));
    }

    return files;
}

std::vector<uint8_t> MSXDOSHandler::readFile(const std::string& filename) {
    // Resolve path to find directory and filename
    auto [dirCluster, baseName] = resolvePath(filename);

    // If path ended with /, it's a directory reference, not a file
    if (baseName.empty()) {
        throw FileNotFoundException("File not found: " + filename);
    }

    auto entries = getDirectoryEntries(dirCluster);
    int index = findDirectoryEntry(entries, baseName);

    if (index < 0) {
        throw FileNotFoundException("File not found: " + filename);
    }

    const auto& entry = entries[index];
    if (entry.attr & ATTR_DIRECTORY) {
        throw DiskException(DiskError::InvalidParameter, "Cannot read directory as file: " + filename);
    }

    std::vector<uint8_t> data;
    data.reserve(entry.fileSize);

    auto chain = getClusterChain(entry.startCluster);
    for (uint16_t cluster : chain) {
        auto clusterData = readCluster(cluster);
        data.insert(data.end(), clusterData.begin(), clusterData.end());
    }

    // Trim to actual file size
    if (data.size() > entry.fileSize) {
        data.resize(entry.fileSize);
    }

    return data;
}

bool MSXDOSHandler::writeFile(const std::string& filename,
                              const std::vector<uint8_t>& data,
                              const FileMetadata& metadata) {
    if (data.size() > 0xFFFFFFFF) {
        return false; // File too large
    }

    // Resolve path to find directory and filename
    auto [dirCluster, baseName] = resolvePath(filename);

    if (baseName.empty()) {
        return false; // Invalid path
    }

    auto entries = getDirectoryEntries(dirCluster);
    auto fat = readFAT();

    // Check if file already exists
    int existingIndex = findDirectoryEntry(entries, baseName);
    if (existingIndex >= 0) {
        // Free existing clusters
        freeClusterChain(fat, entries[existingIndex].startCluster);
        entries[existingIndex].startCluster = 0;
        entries[existingIndex].fileSize = 0;
    }

    // Find or create directory entry
    int entryIndex = existingIndex;
    if (entryIndex < 0) {
        // Find free entry
        for (size_t i = 0; i < entries.size(); ++i) {
            if (static_cast<uint8_t>(entries[i].name[0]) == DIR_FREE ||
                static_cast<uint8_t>(entries[i].name[0]) == DIR_END) {
                entryIndex = static_cast<int>(i);
                break;
            }
        }

        if (entryIndex < 0) {
            // Need to add new entry
            // For root directory, check entry limit
            if (dirCluster == 0 && entries.size() >= m_rootEntryCount) {
                return false; // Directory full
            }
            DirEntry newEntry{};
            std::memset(&newEntry, 0, sizeof(DirEntry));
            entries.push_back(newEntry);
            entryIndex = static_cast<int>(entries.size() - 1);
        }
    }

    // Initialize entry
    DirEntry& entry = entries[entryIndex];
    parseFilename(baseName, entry.name, entry.ext);
    entry.attr = ATTR_ARCHIVE;
    entry.fileSize = static_cast<uint32_t>(data.size());

    // Set timestamp
    // Current time approximation
    entry.time = (12 << 11) | (0 << 5) | 0;  // 12:00:00
    entry.date = ((2024 - 1980) << 9) | (1 << 5) | 1;  // 2024-01-01

    // Allocate clusters and write data
    if (!data.empty()) {
        uint16_t clusterSize = m_sectorsPerCluster * m_bytesPerSector;
        size_t numClusters = (data.size() + clusterSize - 1) / clusterSize;
        uint16_t prevCluster = 0;
        uint16_t firstCluster = 0;

        for (size_t i = 0; i < numClusters; ++i) {
            uint16_t cluster = allocateCluster(fat);
            if (cluster == 0) {
                // Out of space - free what we allocated
                if (firstCluster != 0) {
                    freeClusterChain(fat, firstCluster);
                }
                return false;
            }

            // Mark as end of chain for now
            setFATEntry(fat, cluster, FAT12_EOF);

            if (prevCluster != 0) {
                setFATEntry(fat, prevCluster, cluster);
            } else {
                firstCluster = cluster;
            }

            // Write cluster data
            size_t offset = i * clusterSize;
            size_t writeSize = std::min(static_cast<size_t>(clusterSize), data.size() - offset);
            std::vector<uint8_t> clusterData(data.begin() + offset, data.begin() + offset + writeSize);
            clusterData.resize(clusterSize, 0);  // Pad to cluster size
            writeCluster(cluster, clusterData);

            prevCluster = cluster;
        }

        entry.startCluster = firstCluster;
    } else {
        entry.startCluster = 0;
    }

    // Write FAT and directory
    writeFAT(fat);
    setDirectoryEntries(dirCluster, entries);

    return true;
}

bool MSXDOSHandler::deleteFile(const std::string& filename) {
    // Resolve path to find directory and filename
    auto [dirCluster, baseName] = resolvePath(filename);

    if (baseName.empty()) {
        return false;
    }

    auto entries = getDirectoryEntries(dirCluster);
    int index = findDirectoryEntry(entries, baseName);

    if (index < 0) {
        return false;
    }

    auto& entry = entries[index];
    if (entry.attr & ATTR_DIRECTORY) {
        // Use deleteDirectory for directories
        return deleteDirectory(filename);
    }

    // Free cluster chain
    if (entry.startCluster >= 2) {
        auto fat = readFAT();
        freeClusterChain(fat, entry.startCluster);
        writeFAT(fat);
    }

    // Mark entry as deleted
    entry.name[0] = static_cast<char>(DIR_FREE);

    setDirectoryEntries(dirCluster, entries);
    return true;
}

bool MSXDOSHandler::renameFile(const std::string& oldName, const std::string& newName) {
    // Resolve source path
    auto [oldDirCluster, oldBaseName] = resolvePath(oldName);
    if (oldBaseName.empty()) {
        return false;
    }

    // Resolve destination path
    auto [newDirCluster, newBaseName] = resolvePath(newName);
    if (newBaseName.empty()) {
        return false;
    }

    // Both must be in same directory for simple rename
    if (oldDirCluster != newDirCluster) {
        return false; // Move between directories not supported yet
    }

    auto entries = getDirectoryEntries(oldDirCluster);
    int oldIndex = findDirectoryEntry(entries, oldBaseName);
    int newIndex = findDirectoryEntry(entries, newBaseName);

    if (oldIndex < 0) {
        return false; // Source doesn't exist
    }

    if (newIndex >= 0 && newIndex != oldIndex) {
        return false; // Destination already exists
    }

    parseFilename(newBaseName, entries[oldIndex].name, entries[oldIndex].ext);
    setDirectoryEntries(oldDirCluster, entries);
    return true;
}

size_t MSXDOSHandler::getFreeSpace() const {
    return countFreeClusters() * m_sectorsPerCluster * m_bytesPerSector;
}

size_t MSXDOSHandler::getTotalSpace() const {
    return m_totalClusters * m_sectorsPerCluster * m_bytesPerSector;
}

bool MSXDOSHandler::fileExists(const std::string& filename) const {
    try {
        auto [dirCluster, baseName] = resolvePath(filename);
        if (baseName.empty()) {
            return false;
        }
        auto entries = getDirectoryEntries(dirCluster);
        return findDirectoryEntry(entries, baseName) >= 0;
    } catch (const FileNotFoundException&) {
        return false;  // Parent directory doesn't exist
    }
}

bool MSXDOSHandler::format(const std::string& volumeName) {
    if (!m_disk) {
        return false;
    }

    // Initialize BPB values from disk geometry
    auto geom = m_disk->getGeometry();

    m_bytesPerSector = static_cast<uint16_t>(geom.bytesPerSector);
    if (m_bytesPerSector == 0) {
        m_bytesPerSector = 512;
    }

    m_sectorsPerTrack = static_cast<uint16_t>(geom.sectorsPerTrack);
    if (m_sectorsPerTrack == 0) {
        m_sectorsPerTrack = 9;
    }

    m_numberOfHeads = static_cast<uint16_t>(geom.sides);
    if (m_numberOfHeads == 0) {
        m_numberOfHeads = 2;
    }

    m_totalSectors = static_cast<uint16_t>(geom.totalSectors());

    // Set appropriate parameters based on disk size
    if (m_totalSectors >= 2880) {
        // 1.44MB: 18 sectors, 2 sides, 80 tracks
        m_sectorsPerCluster = 1;
        m_sectorsPerFAT = 9;
        m_rootEntryCount = 224;
        m_mediaDescriptor = 0xF0;
    } else if (m_totalSectors >= 1440) {
        // 720KB: 9 sectors, 2 sides, 80 tracks
        m_sectorsPerCluster = 2;
        m_sectorsPerFAT = 3;
        m_rootEntryCount = 112;
        m_mediaDescriptor = 0xF9;
    } else if (m_totalSectors >= 720) {
        // 360KB: 9 sectors, 2 sides, 40 tracks or 1 side, 80 tracks
        m_sectorsPerCluster = 2;
        m_sectorsPerFAT = 2;
        m_rootEntryCount = 112;
        m_mediaDescriptor = (m_numberOfHeads == 2) ? 0xFD : 0xF8;
    } else {
        // Smaller disks: use conservative defaults
        m_sectorsPerCluster = 1;
        m_sectorsPerFAT = 2;
        m_rootEntryCount = 64;
        m_mediaDescriptor = 0xF8;
    }

    // Calculate derived values
    m_reservedSectors = 1;
    m_numberOfFATs = 2;
    m_rootDirSectors = ((m_rootEntryCount * 32) + (m_bytesPerSector - 1)) / m_bytesPerSector;
    m_firstDataSector = m_reservedSectors + (m_numberOfFATs * m_sectorsPerFAT) + m_rootDirSectors;
    m_dataSectors = m_totalSectors - m_firstDataSector;
    m_totalClusters = m_dataSectors / m_sectorsPerCluster;

    // Clear all sectors
    std::vector<uint8_t> emptySector(m_bytesPerSector, 0);
    for (size_t t = 0; t < geom.tracks; ++t) {
        for (size_t h = 0; h < geom.sides; ++h) {
            for (size_t s = 0; s < geom.sectorsPerTrack; ++s) {
                m_disk->writeSector(t, h, s, emptySector);
            }
        }
    }

    // Create boot sector with BPB
    std::vector<uint8_t> bootSector(m_bytesPerSector, 0);

    // Jump instruction
    bootSector[0] = 0xEB;
    bootSector[1] = 0xFE;
    bootSector[2] = 0x90;

    // OEM name
    const char* oemName = "MSXDOS  ";
    std::memcpy(&bootSector[3], oemName, 8);

    // BPB
    bootSector[0x0B] = m_bytesPerSector & 0xFF;
    bootSector[0x0C] = (m_bytesPerSector >> 8) & 0xFF;
    bootSector[0x0D] = m_sectorsPerCluster;
    bootSector[0x0E] = m_reservedSectors & 0xFF;
    bootSector[0x0F] = (m_reservedSectors >> 8) & 0xFF;
    bootSector[0x10] = m_numberOfFATs;
    bootSector[0x11] = m_rootEntryCount & 0xFF;
    bootSector[0x12] = (m_rootEntryCount >> 8) & 0xFF;
    bootSector[0x13] = m_totalSectors & 0xFF;
    bootSector[0x14] = (m_totalSectors >> 8) & 0xFF;
    bootSector[0x15] = m_mediaDescriptor;
    bootSector[0x16] = m_sectorsPerFAT & 0xFF;
    bootSector[0x17] = (m_sectorsPerFAT >> 8) & 0xFF;
    bootSector[0x18] = m_sectorsPerTrack & 0xFF;
    bootSector[0x19] = (m_sectorsPerTrack >> 8) & 0xFF;
    bootSector[0x1A] = m_numberOfHeads & 0xFF;
    bootSector[0x1B] = (m_numberOfHeads >> 8) & 0xFF;

    // Boot signature
    bootSector[0x1FE] = 0x55;
    bootSector[0x1FF] = 0xAA;

    m_disk->writeSector(0, 0, 0, bootSector);

    // Initialize FAT
    std::vector<uint8_t> fat(m_sectorsPerFAT * m_bytesPerSector, 0);

    // First two entries are reserved
    fat[0] = m_mediaDescriptor;
    fat[1] = 0xFF;
    fat[2] = 0xFF;

    writeFAT(fat);

    // Create volume label if specified
    if (!volumeName.empty()) {
        DirEntry volLabel{};
        std::memset(&volLabel, ' ', 11);
        size_t nameLen = std::min(volumeName.length(), static_cast<size_t>(11));
        for (size_t i = 0; i < nameLen; ++i) {
            if (i < 8) {
                volLabel.name[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(volumeName[i])));
            } else {
                volLabel.ext[i - 8] = static_cast<char>(std::toupper(static_cast<unsigned char>(volumeName[i])));
            }
        }
        volLabel.attr = ATTR_VOLUME_ID;

        std::vector<DirEntry> entries = {volLabel};
        writeRootDirectory(entries);
    }

    return true;
}

std::string MSXDOSHandler::getVolumeName() const {
    auto entries = readRootDirectory();

    for (const auto& entry : entries) {
        if (entry.attr & ATTR_VOLUME_ID) {
            return formatFilename(entry.name, entry.ext);
        }
    }

    return "";
}

//=============================================================================
// Subdirectory Support Implementation
//=============================================================================

std::pair<uint16_t, std::string> MSXDOSHandler::resolvePath(const std::string& path) const {
    if (path.empty() || path == "/" || path == "\\") {
        return {0, ""};  // 0 = root directory
    }

    std::string cleanPath = path;
    // Normalize separators
    for (char& c : cleanPath) {
        if (c == '\\') c = '/';
    }
    // Remove leading slash
    if (!cleanPath.empty() && cleanPath[0] == '/') {
        cleanPath = cleanPath.substr(1);
    }
    // Remove trailing slash
    if (!cleanPath.empty() && cleanPath.back() == '/') {
        cleanPath = cleanPath.substr(0, cleanPath.length() - 1);
    }

    if (cleanPath.empty()) {
        return {0, ""};
    }

    // Split path into components
    std::vector<std::string> components;
    size_t start = 0;
    size_t pos;
    while ((pos = cleanPath.find('/', start)) != std::string::npos) {
        if (pos > start) {
            components.push_back(cleanPath.substr(start, pos - start));
        }
        start = pos + 1;
    }
    if (start < cleanPath.length()) {
        components.push_back(cleanPath.substr(start));
    }

    if (components.empty()) {
        return {0, ""};
    }

    // Last component is the target name
    std::string targetName = components.back();
    components.pop_back();

    // Navigate through directories
    uint16_t currentCluster = 0;  // Start from root

    for (const auto& component : components) {
        auto entries = getDirectoryEntries(currentCluster);
        int idx = -1;

        // Find directory entry
        char name[8], ext[3];
        parseFilename(component, name, ext);

        for (size_t i = 0; i < entries.size(); ++i) {
            const auto& entry = entries[i];
            if (static_cast<uint8_t>(entry.name[0]) == DIR_FREE) continue;
            if (static_cast<uint8_t>(entry.name[0]) == DIR_END) break;
            // Skip volume labels (they are not directories)
            if (entry.attr & ATTR_VOLUME_ID) continue;

            if (std::memcmp(entry.name, name, 8) == 0 &&
                std::memcmp(entry.ext, ext, 3) == 0) {
                idx = static_cast<int>(i);
                break;
            }
        }

        if (idx < 0 || !(entries[idx].attr & ATTR_DIRECTORY)) {
            throw FileNotFoundException("Directory not found: " + component);
        }

        currentCluster = entries[idx].startCluster;
    }

    return {currentCluster, targetName};
}

std::vector<MSXDOSHandler::DirEntry> MSXDOSHandler::readDirectoryCluster(uint16_t cluster) const {
    std::vector<DirEntry> entries;

    if (cluster < 2) {
        return entries;
    }

    auto chain = getClusterChain(cluster);
    for (uint16_t clust : chain) {
        auto data = readCluster(clust);

        // Parse directory entries (32 bytes each)
        for (size_t offset = 0; offset + 32 <= data.size(); offset += 32) {
            DirEntry entry;
            std::memcpy(entry.name, &data[offset], 8);
            std::memcpy(entry.ext, &data[offset + 8], 3);
            entry.attr = data[offset + 11];
            std::memcpy(entry.reserved, &data[offset + 12], 10);
            entry.time = data[offset + 22] | (static_cast<uint16_t>(data[offset + 23]) << 8);
            entry.date = data[offset + 24] | (static_cast<uint16_t>(data[offset + 25]) << 8);
            entry.startCluster = data[offset + 26] | (static_cast<uint16_t>(data[offset + 27]) << 8);
            entry.fileSize = data[offset + 28] |
                            (static_cast<uint32_t>(data[offset + 29]) << 8) |
                            (static_cast<uint32_t>(data[offset + 30]) << 16) |
                            (static_cast<uint32_t>(data[offset + 31]) << 24);

            // Check for end of directory
            if (static_cast<uint8_t>(entry.name[0]) == DIR_END) {
                return entries;
            }

            entries.push_back(entry);
        }
    }

    return entries;
}

void MSXDOSHandler::writeDirectoryCluster(uint16_t cluster, const std::vector<DirEntry>& entries) {
    if (cluster < 2) {
        return;
    }

    size_t clusterSize = m_sectorsPerCluster * m_bytesPerSector;
    size_t entriesPerCluster = clusterSize / 32;

    auto chain = getClusterChain(cluster);
    auto fat = readFAT();

    size_t entryIdx = 0;

    for (size_t chainIdx = 0; chainIdx < chain.size() && entryIdx < entries.size(); ++chainIdx) {
        std::vector<uint8_t> data(clusterSize, 0);

        for (size_t i = 0; i < entriesPerCluster && entryIdx < entries.size(); ++i, ++entryIdx) {
            const auto& entry = entries[entryIdx];
            size_t offset = i * 32;

            std::memcpy(&data[offset], entry.name, 8);
            std::memcpy(&data[offset + 8], entry.ext, 3);
            data[offset + 11] = entry.attr;
            std::memcpy(&data[offset + 12], entry.reserved, 10);
            data[offset + 22] = entry.time & 0xFF;
            data[offset + 23] = (entry.time >> 8) & 0xFF;
            data[offset + 24] = entry.date & 0xFF;
            data[offset + 25] = (entry.date >> 8) & 0xFF;
            data[offset + 26] = entry.startCluster & 0xFF;
            data[offset + 27] = (entry.startCluster >> 8) & 0xFF;
            data[offset + 28] = entry.fileSize & 0xFF;
            data[offset + 29] = (entry.fileSize >> 8) & 0xFF;
            data[offset + 30] = (entry.fileSize >> 16) & 0xFF;
            data[offset + 31] = (entry.fileSize >> 24) & 0xFF;
        }

        writeCluster(chain[chainIdx], data);
    }

    // Allocate more clusters if needed
    while (entryIdx < entries.size()) {
        uint16_t newCluster = allocateCluster(fat);
        if (newCluster == 0) {
            break;  // No more space
        }

        setFATEntry(fat, chain.back(), newCluster);
        setFATEntry(fat, newCluster, FAT12_EOF);
        chain.push_back(newCluster);

        std::vector<uint8_t> data(clusterSize, 0);

        for (size_t i = 0; i < entriesPerCluster && entryIdx < entries.size(); ++i, ++entryIdx) {
            const auto& entry = entries[entryIdx];
            size_t offset = i * 32;

            std::memcpy(&data[offset], entry.name, 8);
            std::memcpy(&data[offset + 8], entry.ext, 3);
            data[offset + 11] = entry.attr;
            std::memcpy(&data[offset + 12], entry.reserved, 10);
            data[offset + 22] = entry.time & 0xFF;
            data[offset + 23] = (entry.time >> 8) & 0xFF;
            data[offset + 24] = entry.date & 0xFF;
            data[offset + 25] = (entry.date >> 8) & 0xFF;
            data[offset + 26] = entry.startCluster & 0xFF;
            data[offset + 27] = (entry.startCluster >> 8) & 0xFF;
            data[offset + 28] = entry.fileSize & 0xFF;
            data[offset + 29] = (entry.fileSize >> 8) & 0xFF;
            data[offset + 30] = (entry.fileSize >> 16) & 0xFF;
            data[offset + 31] = (entry.fileSize >> 24) & 0xFF;
        }

        writeCluster(newCluster, data);
        writeFAT(fat);
    }
}

int MSXDOSHandler::findEntryInDirectory(uint16_t cluster, const std::string& name) const {
    auto entries = getDirectoryEntries(cluster);
    return findDirectoryEntry(entries, name);
}

std::vector<MSXDOSHandler::DirEntry> MSXDOSHandler::getDirectoryEntries(uint16_t cluster) const {
    if (cluster == 0) {
        return readRootDirectory();
    }
    return readDirectoryCluster(cluster);
}

void MSXDOSHandler::setDirectoryEntries(uint16_t cluster, const std::vector<DirEntry>& entries) {
    if (cluster == 0) {
        writeRootDirectory(entries);
    } else {
        writeDirectoryCluster(cluster, entries);
    }
}

bool MSXDOSHandler::createDirectory(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    // Resolve parent directory and target name
    auto [parentCluster, dirName] = resolvePath(path);

    if (dirName.empty()) {
        return false;
    }

    // Check if already exists
    auto parentEntries = getDirectoryEntries(parentCluster);
    if (findDirectoryEntry(parentEntries, dirName) >= 0) {
        return false;  // Already exists
    }

    // Allocate a cluster for the new directory
    auto fat = readFAT();
    uint16_t newCluster = allocateCluster(fat);
    if (newCluster == 0) {
        return false;  // No free clusters
    }
    setFATEntry(fat, newCluster, FAT12_EOF);

    // Initialize the new directory with . and .. entries
    size_t clusterSize = m_sectorsPerCluster * m_bytesPerSector;
    std::vector<uint8_t> dirData(clusterSize, 0);

    // Create "." entry
    DirEntry dotEntry{};
    std::memset(dotEntry.name, ' ', 8);
    std::memset(dotEntry.ext, ' ', 3);
    dotEntry.name[0] = '.';
    dotEntry.attr = ATTR_DIRECTORY;
    dotEntry.startCluster = newCluster;
    dotEntry.time = (12 << 11) | (0 << 5) | 0;
    dotEntry.date = ((2024 - 1980) << 9) | (1 << 5) | 1;

    // Create ".." entry
    DirEntry dotDotEntry{};
    std::memset(dotDotEntry.name, ' ', 8);
    std::memset(dotDotEntry.ext, ' ', 3);
    dotDotEntry.name[0] = '.';
    dotDotEntry.name[1] = '.';
    dotDotEntry.attr = ATTR_DIRECTORY;
    dotDotEntry.startCluster = parentCluster;  // 0 for root
    dotDotEntry.time = (12 << 11) | (0 << 5) | 0;
    dotDotEntry.date = ((2024 - 1980) << 9) | (1 << 5) | 1;

    // Write . entry
    std::memcpy(&dirData[0], dotEntry.name, 8);
    std::memcpy(&dirData[8], dotEntry.ext, 3);
    dirData[11] = dotEntry.attr;
    dirData[22] = dotEntry.time & 0xFF;
    dirData[23] = (dotEntry.time >> 8) & 0xFF;
    dirData[24] = dotEntry.date & 0xFF;
    dirData[25] = (dotEntry.date >> 8) & 0xFF;
    dirData[26] = dotEntry.startCluster & 0xFF;
    dirData[27] = (dotEntry.startCluster >> 8) & 0xFF;

    // Write .. entry
    std::memcpy(&dirData[32], dotDotEntry.name, 8);
    std::memcpy(&dirData[40], dotDotEntry.ext, 3);
    dirData[43] = dotDotEntry.attr;
    dirData[54] = dotDotEntry.time & 0xFF;
    dirData[55] = (dotDotEntry.time >> 8) & 0xFF;
    dirData[56] = dotDotEntry.date & 0xFF;
    dirData[57] = (dotDotEntry.date >> 8) & 0xFF;
    dirData[58] = dotDotEntry.startCluster & 0xFF;
    dirData[59] = (dotDotEntry.startCluster >> 8) & 0xFF;

    writeCluster(newCluster, dirData);

    // Create entry in parent directory
    DirEntry newEntry{};
    parseFilename(dirName, newEntry.name, newEntry.ext);
    newEntry.attr = ATTR_DIRECTORY;
    newEntry.startCluster = newCluster;
    newEntry.fileSize = 0;  // Directories have size 0
    newEntry.time = (12 << 11) | (0 << 5) | 0;
    newEntry.date = ((2024 - 1980) << 9) | (1 << 5) | 1;

    // Find free slot in parent directory
    int freeSlot = -1;
    for (size_t i = 0; i < parentEntries.size(); ++i) {
        if (static_cast<uint8_t>(parentEntries[i].name[0]) == DIR_FREE ||
            static_cast<uint8_t>(parentEntries[i].name[0]) == DIR_END) {
            freeSlot = static_cast<int>(i);
            break;
        }
    }

    if (freeSlot >= 0) {
        parentEntries[freeSlot] = newEntry;
    } else {
        parentEntries.push_back(newEntry);
    }

    setDirectoryEntries(parentCluster, parentEntries);
    writeFAT(fat);

    return true;
}

bool MSXDOSHandler::deleteDirectory(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    // Resolve parent directory and target name
    auto [parentCluster, dirName] = resolvePath(path);

    if (dirName.empty()) {
        return false;
    }

    auto parentEntries = getDirectoryEntries(parentCluster);
    int idx = findDirectoryEntry(parentEntries, dirName);

    if (idx < 0) {
        return false;  // Not found
    }

    auto& entry = parentEntries[idx];

    if (!(entry.attr & ATTR_DIRECTORY)) {
        return false;  // Not a directory
    }

    // Check if directory is empty (only . and ..)
    auto dirEntries = readDirectoryCluster(entry.startCluster);
    for (const auto& dirEntry : dirEntries) {
        if (static_cast<uint8_t>(dirEntry.name[0]) == DIR_FREE) continue;
        if (static_cast<uint8_t>(dirEntry.name[0]) == DIR_END) break;

        // Skip . and ..
        if (dirEntry.name[0] == '.') {
            if (dirEntry.name[1] == ' ' || dirEntry.name[1] == '.') {
                continue;
            }
        }

        // Directory not empty
        return false;
    }

    // Free directory clusters
    auto fat = readFAT();
    freeClusterChain(fat, entry.startCluster);

    // Mark entry as deleted
    entry.name[0] = static_cast<char>(DIR_FREE);

    setDirectoryEntries(parentCluster, parentEntries);
    writeFAT(fat);

    return true;
}

} // namespace rde
