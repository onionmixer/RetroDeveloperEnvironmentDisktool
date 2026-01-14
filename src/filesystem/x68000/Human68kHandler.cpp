#include "rdedisktool/filesystem/x68000/Human68kHandler.h"
#include <cstring>
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace rde {

Human68kHandler::Human68kHandler() = default;

//=============================================================================
// Sector Addressing for X68000
//=============================================================================

void Human68kHandler::logicalToPhysical(uint32_t logicalSector,
                                        size_t& track, size_t& head, size_t& sector) const {
    // X68000 2HD: 8 sectors per track, 1024 bytes per sector
    // Logical sector 0 = Track 0, Head 0, Sector 0
    // For DiskImage interface: track = cylinder * heads + head

    size_t sectorsPerCylinder = m_sectorsPerTrack * m_numberOfHeads;
    size_t cylinder = logicalSector / sectorsPerCylinder;
    size_t remaining = logicalSector % sectorsPerCylinder;

    head = remaining / m_sectorsPerTrack;
    sector = remaining % m_sectorsPerTrack;  // 0-indexed for DiskImage
    track = cylinder * m_numberOfHeads + head;
}

std::vector<uint8_t> Human68kHandler::readLogicalSector(uint32_t logicalSector) const {
    size_t track, head, sector;
    logicalToPhysical(logicalSector, track, head, sector);

    // DiskImage uses track (which includes head), side, sector
    // For X68000: track already encodes cylinder * heads + head
    size_t cylinder = track / m_numberOfHeads;
    size_t side = track % m_numberOfHeads;

    // X68000 disk images use 1-indexed sectors (1-8)
    return m_disk->readSector(cylinder, side, sector + 1);
}

void Human68kHandler::writeLogicalSector(uint32_t logicalSector, const std::vector<uint8_t>& data) {
    size_t track, head, sector;
    logicalToPhysical(logicalSector, track, head, sector);

    size_t cylinder = track / m_numberOfHeads;
    size_t side = track % m_numberOfHeads;

    // X68000 disk images use 1-indexed sectors (1-8)
    m_disk->writeSector(cylinder, side, sector + 1, data);
}

//=============================================================================
// BPB Parsing
//=============================================================================

bool Human68kHandler::parseBPB() {
    // Read boot sector (logical sector 0)
    auto bootSector = readLogicalSector(0);

    if (bootSector.size() < 32) {
        return false;
    }

    // Check for JMP instruction at start (optional for Human68k)
    // X68000 boot sectors may start differently

    // Parse BPB at offset 0x0B (same as FAT12)
    m_bytesPerSector = bootSector[0x0B] | (bootSector[0x0C] << 8);
    m_sectorsPerCluster = bootSector[0x0D];
    m_reservedSectors = bootSector[0x0E] | (bootSector[0x0F] << 8);
    m_numberOfFATs = bootSector[0x10];
    m_rootEntryCount = bootSector[0x11] | (bootSector[0x12] << 8);
    m_totalSectors = bootSector[0x13] | (bootSector[0x14] << 8);
    m_mediaDescriptor = bootSector[0x15];
    m_sectorsPerFAT = bootSector[0x16] | (bootSector[0x17] << 8);
    m_sectorsPerTrack = bootSector[0x18] | (bootSector[0x19] << 8);
    m_numberOfHeads = bootSector[0x1A] | (bootSector[0x1B] << 8);

    // Validate BPB for X68000
    // X68000 2HD uses 1024-byte sectors typically
    if (m_bytesPerSector != 256 && m_bytesPerSector != 512 &&
        m_bytesPerSector != 1024 && m_bytesPerSector != 2048) {
        // Use default X68000 2HD values
        m_bytesPerSector = 1024;
        m_sectorsPerCluster = 1;
        m_reservedSectors = 1;
        m_numberOfFATs = 2;
        m_rootEntryCount = 192;
        m_sectorsPerFAT = 2;
        m_sectorsPerTrack = 8;
        m_numberOfHeads = 2;

        // Calculate total sectors from disk geometry
        auto geom = m_disk->getGeometry();
        m_totalSectors = geom.tracks * geom.sectorsPerTrack;
    }

    // Calculate derived values
    m_rootDirSectors = ((m_rootEntryCount * 32) + (m_bytesPerSector - 1)) / m_bytesPerSector;
    m_firstDataSector = m_reservedSectors + (m_numberOfFATs * m_sectorsPerFAT) + m_rootDirSectors;
    m_dataSectors = m_totalSectors - m_firstDataSector;
    m_totalClusters = m_dataSectors / m_sectorsPerCluster;

    return true;
}

//=============================================================================
// FAT Operations
//=============================================================================

std::vector<uint8_t> Human68kHandler::readFAT() const {
    std::vector<uint8_t> fat;
    fat.reserve(m_sectorsPerFAT * m_bytesPerSector);

    for (uint16_t i = 0; i < m_sectorsPerFAT; ++i) {
        auto sector = readLogicalSector(m_reservedSectors + i);
        fat.insert(fat.end(), sector.begin(), sector.end());
    }

    return fat;
}

void Human68kHandler::writeFAT(const std::vector<uint8_t>& fat) {
    // Write to all FAT copies
    for (uint8_t fatNum = 0; fatNum < m_numberOfFATs; ++fatNum) {
        uint32_t fatStart = m_reservedSectors + (fatNum * m_sectorsPerFAT);

        for (uint16_t i = 0; i < m_sectorsPerFAT; ++i) {
            std::vector<uint8_t> sector(m_bytesPerSector, 0);
            size_t offset = i * m_bytesPerSector;
            size_t copySize = std::min(static_cast<size_t>(m_bytesPerSector), fat.size() - offset);

            if (offset < fat.size()) {
                std::copy(fat.begin() + offset, fat.begin() + offset + copySize, sector.begin());
            }

            writeLogicalSector(fatStart + i, sector);
        }
    }
}

uint16_t Human68kHandler::getFATEntry(const std::vector<uint8_t>& fat, uint16_t cluster) const {
    // FAT12 entry extraction
    size_t offset = cluster + (cluster / 2);

    if (offset + 1 >= fat.size()) {
        return FAT12_EOF;
    }

    uint16_t value = fat[offset] | (fat[offset + 1] << 8);

    if (cluster & 1) {
        value >>= 4;
    } else {
        value &= 0x0FFF;
    }

    return value;
}

void Human68kHandler::setFATEntry(std::vector<uint8_t>& fat, uint16_t cluster, uint16_t value) {
    // FAT12 entry setting
    size_t offset = cluster + (cluster / 2);

    if (offset + 1 >= fat.size()) {
        return;
    }

    if (cluster & 1) {
        fat[offset] = (fat[offset] & 0x0F) | ((value & 0x0F) << 4);
        fat[offset + 1] = (value >> 4) & 0xFF;
    } else {
        fat[offset] = value & 0xFF;
        fat[offset + 1] = (fat[offset + 1] & 0xF0) | ((value >> 8) & 0x0F);
    }
}

std::vector<uint16_t> Human68kHandler::getClusterChain(uint16_t startCluster) const {
    std::vector<uint16_t> chain;
    auto fat = readFAT();

    uint16_t cluster = startCluster;
    while (cluster >= 2 && cluster < FAT12_RESERVED) {
        chain.push_back(cluster);
        cluster = getFATEntry(fat, cluster);

        // Prevent infinite loops
        if (chain.size() > m_totalClusters) {
            break;
        }
    }

    return chain;
}

uint16_t Human68kHandler::allocateCluster(std::vector<uint8_t>& fat) {
    // Start from cluster 2 (0 and 1 are reserved)
    for (uint16_t cluster = 2; cluster < m_totalClusters + 2; ++cluster) {
        if (getFATEntry(fat, cluster) == FAT12_FREE) {
            setFATEntry(fat, cluster, FAT12_EOF);
            return cluster;
        }
    }
    return 0;  // No free clusters
}

void Human68kHandler::freeClusterChain(std::vector<uint8_t>& fat, uint16_t startCluster) {
    uint16_t cluster = startCluster;
    while (cluster >= 2 && cluster < FAT12_RESERVED) {
        uint16_t next = getFATEntry(fat, cluster);
        setFATEntry(fat, cluster, FAT12_FREE);
        cluster = next;
    }
}

//=============================================================================
// Cluster I/O
//=============================================================================

std::vector<uint8_t> Human68kHandler::readCluster(uint16_t cluster) const {
    std::vector<uint8_t> data;
    data.reserve(m_sectorsPerCluster * m_bytesPerSector);

    uint32_t firstSector = m_firstDataSector + (cluster - 2) * m_sectorsPerCluster;

    for (uint8_t i = 0; i < m_sectorsPerCluster; ++i) {
        auto sector = readLogicalSector(firstSector + i);
        data.insert(data.end(), sector.begin(), sector.end());
    }

    return data;
}

void Human68kHandler::writeCluster(uint16_t cluster, const std::vector<uint8_t>& data) {
    uint32_t firstSector = m_firstDataSector + (cluster - 2) * m_sectorsPerCluster;

    for (uint8_t i = 0; i < m_sectorsPerCluster; ++i) {
        std::vector<uint8_t> sector(m_bytesPerSector, 0);
        size_t offset = i * m_bytesPerSector;
        size_t copySize = std::min(static_cast<size_t>(m_bytesPerSector),
                                   data.size() > offset ? data.size() - offset : 0);

        if (copySize > 0) {
            std::copy(data.begin() + offset, data.begin() + offset + copySize, sector.begin());
        }

        writeLogicalSector(firstSector + i, sector);
    }
}

//=============================================================================
// Directory Operations
//=============================================================================

std::vector<Human68kHandler::DirEntry> Human68kHandler::readRootDirectory() const {
    std::vector<DirEntry> entries;

    uint32_t rootStart = m_reservedSectors + (m_numberOfFATs * m_sectorsPerFAT);

    for (uint16_t i = 0; i < m_rootDirSectors; ++i) {
        auto sector = readLogicalSector(rootStart + i);

        size_t entriesPerSector = m_bytesPerSector / sizeof(DirEntry);
        for (size_t j = 0; j < entriesPerSector; ++j) {
            DirEntry entry;
            std::memcpy(&entry, sector.data() + j * sizeof(DirEntry), sizeof(DirEntry));
            entries.push_back(entry);
        }
    }

    return entries;
}

void Human68kHandler::writeRootDirectory(const std::vector<DirEntry>& entries) {
    uint32_t rootStart = m_reservedSectors + (m_numberOfFATs * m_sectorsPerFAT);
    size_t entriesPerSector = m_bytesPerSector / sizeof(DirEntry);

    for (uint16_t i = 0; i < m_rootDirSectors; ++i) {
        std::vector<uint8_t> sector(m_bytesPerSector, 0);

        for (size_t j = 0; j < entriesPerSector; ++j) {
            size_t entryIdx = i * entriesPerSector + j;
            if (entryIdx < entries.size()) {
                std::memcpy(sector.data() + j * sizeof(DirEntry),
                           &entries[entryIdx], sizeof(DirEntry));
            }
        }

        writeLogicalSector(rootStart + i, sector);
    }
}

int Human68kHandler::findDirectoryEntry(const std::vector<DirEntry>& entries,
                                        const std::string& filename) const {
    char name[8], ext[3];
    parseFilename(filename, name, ext);

    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];

        // Skip free and end entries
        if (static_cast<uint8_t>(entry.name[0]) == DIR_FREE ||
            static_cast<uint8_t>(entry.name[0]) == DIR_END) {
            continue;
        }

        // Skip volume label
        if (entry.attr & ATTR_VOLUME_ID) {
            continue;
        }

        if (std::memcmp(entry.name, name, 8) == 0 &&
            std::memcmp(entry.ext, ext, 3) == 0) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

//=============================================================================
// Filename Handling
//=============================================================================

std::string Human68kHandler::formatFilename(const char* name, const char* ext) const {
    std::string result;

    // Copy name, trim trailing spaces
    for (int i = 7; i >= 0; --i) {
        if (name[i] != ' ') {
            result = std::string(name, i + 1);
            break;
        }
    }

    // Add extension if present
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

void Human68kHandler::parseFilename(const std::string& filename, char* name, char* ext) const {
    std::memset(name, ' ', 8);
    std::memset(ext, ' ', 3);

    // Find extension
    size_t dotPos = filename.rfind('.');
    std::string baseName, extension;

    if (dotPos != std::string::npos) {
        baseName = filename.substr(0, dotPos);
        extension = filename.substr(dotPos + 1);
    } else {
        baseName = filename;
    }

    // Convert to uppercase and copy
    for (size_t i = 0; i < std::min(baseName.length(), size_t(8)); ++i) {
        name[i] = std::toupper(static_cast<unsigned char>(baseName[i]));
    }

    for (size_t i = 0; i < std::min(extension.length(), size_t(3)); ++i) {
        ext[i] = std::toupper(static_cast<unsigned char>(extension[i]));
    }
}

//=============================================================================
// FileSystemHandler Interface Implementation
//=============================================================================

bool Human68kHandler::initialize(DiskImage* disk) {
    m_disk = disk;

    if (!m_disk || m_disk->getRawData().empty()) {
        return false;
    }

    return parseBPB();
}

std::vector<FileEntry> Human68kHandler::listFiles(const std::string& path) {
    std::vector<FileEntry> files;

    if (path.empty() || path == "/" || path == "\\") {
        // List root directory
        auto entries = readRootDirectory();

        for (const auto& entry : entries) {
            if (static_cast<uint8_t>(entry.name[0]) == DIR_FREE ||
                static_cast<uint8_t>(entry.name[0]) == DIR_END) {
                continue;
            }

            if (entry.attr & ATTR_VOLUME_ID) {
                continue;
            }

            files.push_back(dirEntryToFileEntry(entry));
        }
    } else {
        // Subdirectory listing
        auto [cluster, name] = resolvePath(path);
        if (cluster == 0) {
            return files;  // Path not found
        }

        auto entries = getDirectoryEntries(cluster);
        for (const auto& entry : entries) {
            if (static_cast<uint8_t>(entry.name[0]) == DIR_FREE ||
                static_cast<uint8_t>(entry.name[0]) == DIR_END) {
                continue;
            }

            // Skip . and .. entries
            if (entry.name[0] == '.') {
                continue;
            }

            if (entry.attr & ATTR_VOLUME_ID) {
                continue;
            }

            files.push_back(dirEntryToFileEntry(entry));
        }
    }

    return files;
}

FileEntry Human68kHandler::dirEntryToFileEntry(const DirEntry& entry) const {
    FileEntry fe;
    fe.name = formatFilename(entry.name, entry.ext);
    fe.size = entry.fileSize;
    fe.isDirectory = (entry.attr & ATTR_DIRECTORY) != 0;
    fe.attributes = entry.attr;

    // Convert DOS date/time to std::time_t
    // Date: bits 0-4 = day, 5-8 = month, 9-15 = year from 1980
    // Time: bits 0-4 = seconds/2, 5-10 = minutes, 11-15 = hours
    struct std::tm tm = {};
    tm.tm_year = ((entry.date >> 9) & 0x7F) + 80;  // Years since 1900
    tm.tm_mon = ((entry.date >> 5) & 0x0F) - 1;    // 0-11
    tm.tm_mday = entry.date & 0x1F;
    tm.tm_hour = (entry.time >> 11) & 0x1F;
    tm.tm_min = (entry.time >> 5) & 0x3F;
    tm.tm_sec = (entry.time & 0x1F) * 2;

    fe.modifiedTime = std::mktime(&tm);

    return fe;
}

std::vector<uint8_t> Human68kHandler::readFile(const std::string& filename) {
    auto entries = readRootDirectory();
    int idx = findDirectoryEntry(entries, filename);

    if (idx < 0) {
        // Try subdirectory path
        size_t lastSlash = filename.rfind('/');
        if (lastSlash == std::string::npos) {
            lastSlash = filename.rfind('\\');
        }

        if (lastSlash != std::string::npos) {
            std::string dirPath = filename.substr(0, lastSlash);
            std::string name = filename.substr(lastSlash + 1);

            auto [cluster, resolved] = resolvePath(dirPath);
            if (cluster != 0) {
                auto dirEntries = getDirectoryEntries(cluster);
                idx = findDirectoryEntry(dirEntries, name);
                if (idx >= 0) {
                    entries = dirEntries;
                }
            }
        }

        if (idx < 0) {
            throw std::runtime_error("File not found: " + filename);
        }
    }

    const auto& entry = entries[idx];
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

bool Human68kHandler::writeFile(const std::string& filename,
                               const std::vector<uint8_t>& data,
                               const FileMetadata& metadata) {
    // Delete existing file if present
    deleteFile(filename);

    auto entries = readRootDirectory();
    auto fat = readFAT();

    // Find free directory entry
    int freeIdx = -1;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (static_cast<uint8_t>(entries[i].name[0]) == DIR_FREE ||
            static_cast<uint8_t>(entries[i].name[0]) == DIR_END) {
            freeIdx = static_cast<int>(i);
            break;
        }
    }

    if (freeIdx < 0) {
        return false;  // No free directory entries
    }

    // Allocate clusters for data
    uint16_t firstCluster = 0;
    uint16_t prevCluster = 0;
    size_t bytesPerCluster = m_sectorsPerCluster * m_bytesPerSector;
    size_t clustersNeeded = (data.size() + bytesPerCluster - 1) / bytesPerCluster;

    if (clustersNeeded == 0) {
        clustersNeeded = 1;  // At least one cluster for empty files? Or 0?
    }

    for (size_t i = 0; i < clustersNeeded; ++i) {
        uint16_t cluster = allocateCluster(fat);
        if (cluster == 0) {
            // Out of space - free already allocated clusters
            if (firstCluster != 0) {
                freeClusterChain(fat, firstCluster);
            }
            return false;
        }

        if (firstCluster == 0) {
            firstCluster = cluster;
        }

        if (prevCluster != 0) {
            setFATEntry(fat, prevCluster, cluster);
        }

        // Write data to cluster
        std::vector<uint8_t> clusterData(bytesPerCluster, 0);
        size_t offset = i * bytesPerCluster;
        size_t copySize = std::min(bytesPerCluster, data.size() - offset);
        if (copySize > 0 && offset < data.size()) {
            std::copy(data.begin() + offset, data.begin() + offset + copySize, clusterData.begin());
        }
        writeCluster(cluster, clusterData);

        prevCluster = cluster;
    }

    // Create directory entry
    DirEntry& entry = entries[freeIdx];
    std::memset(&entry, 0, sizeof(DirEntry));
    parseFilename(filename, entry.name, entry.ext);
    entry.attr = ATTR_ARCHIVE;
    entry.startCluster = firstCluster;
    entry.fileSize = static_cast<uint32_t>(data.size());

    // Set current date/time (simplified)
    // Using a default date for now
    entry.date = ((2024 - 1980) << 9) | (1 << 5) | 1;  // 2024-01-01
    entry.time = (12 << 11) | (0 << 5) | 0;  // 12:00:00

    // Write FAT and directory
    writeFAT(fat);
    writeRootDirectory(entries);

    return true;
}

bool Human68kHandler::deleteFile(const std::string& filename) {
    auto entries = readRootDirectory();
    int idx = findDirectoryEntry(entries, filename);

    if (idx < 0) {
        return false;  // File not found
    }

    auto& entry = entries[idx];

    // Cannot delete directories with this method
    if (entry.attr & ATTR_DIRECTORY) {
        return false;
    }

    // Free cluster chain
    auto fat = readFAT();
    if (entry.startCluster >= 2) {
        freeClusterChain(fat, entry.startCluster);
    }

    // Mark entry as deleted
    entry.name[0] = static_cast<char>(DIR_FREE);

    writeFAT(fat);
    writeRootDirectory(entries);

    return true;
}

bool Human68kHandler::renameFile(const std::string& oldName, const std::string& newName) {
    auto entries = readRootDirectory();
    int idx = findDirectoryEntry(entries, oldName);

    if (idx < 0) {
        return false;  // File not found
    }

    // Check if new name already exists
    if (findDirectoryEntry(entries, newName) >= 0) {
        return false;  // Name collision
    }

    // Update the entry
    parseFilename(newName, entries[idx].name, entries[idx].ext);
    writeRootDirectory(entries);

    return true;
}

size_t Human68kHandler::getFreeSpace() const {
    return countFreeClusters() * m_sectorsPerCluster * m_bytesPerSector;
}

size_t Human68kHandler::getTotalSpace() const {
    return m_totalClusters * m_sectorsPerCluster * m_bytesPerSector;
}

bool Human68kHandler::fileExists(const std::string& filename) const {
    auto entries = readRootDirectory();
    return findDirectoryEntry(entries, filename) >= 0;
}

bool Human68kHandler::format(const std::string& volumeName) {
    // Initialize boot sector
    std::vector<uint8_t> bootSector(m_bytesPerSector, 0);

    // X68000 boot sector - JMP instruction
    bootSector[0] = 0xEB;
    bootSector[1] = 0x3C;
    bootSector[2] = 0x90;

    // OEM name
    std::memcpy(bootSector.data() + 3, "HUMAN68K", 8);

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

    writeLogicalSector(0, bootSector);

    // Initialize FAT
    std::vector<uint8_t> fat(m_sectorsPerFAT * m_bytesPerSector, 0);

    // First two entries are reserved
    fat[0] = m_mediaDescriptor;
    fat[1] = 0xFF;
    fat[2] = 0xFF;

    writeFAT(fat);

    // Initialize root directory
    std::vector<DirEntry> entries(m_rootEntryCount);
    std::memset(entries.data(), 0, entries.size() * sizeof(DirEntry));

    // Add volume label if provided
    if (!volumeName.empty()) {
        DirEntry& volEntry = entries[0];
        std::memset(volEntry.name, ' ', 8);
        std::memset(volEntry.ext, ' ', 3);

        size_t copyLen = std::min(volumeName.length(), size_t(11));
        for (size_t i = 0; i < copyLen; ++i) {
            if (i < 8) {
                volEntry.name[i] = std::toupper(static_cast<unsigned char>(volumeName[i]));
            } else {
                volEntry.ext[i - 8] = std::toupper(static_cast<unsigned char>(volumeName[i]));
            }
        }

        volEntry.attr = ATTR_VOLUME_ID;
    }

    writeRootDirectory(entries);

    return true;
}

std::string Human68kHandler::getVolumeName() const {
    auto entries = readRootDirectory();

    for (const auto& entry : entries) {
        if (static_cast<uint8_t>(entry.name[0]) == DIR_FREE ||
            static_cast<uint8_t>(entry.name[0]) == DIR_END) {
            continue;
        }

        if (entry.attr == ATTR_VOLUME_ID) {
            return formatFilename(entry.name, entry.ext);
        }
    }

    return "";
}

uint16_t Human68kHandler::countFreeClusters() const {
    auto fat = readFAT();
    uint16_t freeCount = 0;

    for (uint16_t cluster = 2; cluster < m_totalClusters + 2; ++cluster) {
        if (getFATEntry(fat, cluster) == FAT12_FREE) {
            ++freeCount;
        }
    }

    return freeCount;
}

Human68kHandler::ClusterInfo Human68kHandler::getClusterInfo() const {
    ClusterInfo info = {};
    auto fat = readFAT();

    info.totalClusters = m_totalClusters;

    for (uint16_t cluster = 2; cluster < m_totalClusters + 2; ++cluster) {
        uint16_t entry = getFATEntry(fat, cluster);

        if (entry == FAT12_FREE) {
            ++info.freeClusters;
        } else if (entry == FAT12_BAD) {
            ++info.badClusters;
        } else if (entry >= FAT12_RESERVED && entry < FAT12_BAD) {
            ++info.reservedClusters;
        } else {
            ++info.usedClusters;
        }
    }

    return info;
}

//=============================================================================
// Directory Operations
//=============================================================================

bool Human68kHandler::createDirectory(const std::string& path) {
    // Find parent directory
    size_t lastSlash = path.rfind('/');
    if (lastSlash == std::string::npos) {
        lastSlash = path.rfind('\\');
    }

    std::string dirName;
    std::vector<DirEntry> parentEntries;
    uint16_t parentCluster = 0;  // 0 = root directory

    if (lastSlash != std::string::npos && lastSlash > 0) {
        std::string parentPath = path.substr(0, lastSlash);
        dirName = path.substr(lastSlash + 1);

        auto [cluster, name] = resolvePath(parentPath);
        if (cluster == 0 && !parentPath.empty()) {
            return false;  // Parent not found
        }
        parentCluster = cluster;
        parentEntries = (parentCluster == 0) ? readRootDirectory() : getDirectoryEntries(parentCluster);
    } else {
        dirName = (lastSlash == 0) ? path.substr(1) : path;
        parentEntries = readRootDirectory();
    }

    // Check if directory already exists
    if (findDirectoryEntry(parentEntries, dirName) >= 0) {
        return false;
    }

    // Find free entry
    int freeIdx = -1;
    for (size_t i = 0; i < parentEntries.size(); ++i) {
        if (static_cast<uint8_t>(parentEntries[i].name[0]) == DIR_FREE ||
            static_cast<uint8_t>(parentEntries[i].name[0]) == DIR_END) {
            freeIdx = static_cast<int>(i);
            break;
        }
    }

    if (freeIdx < 0) {
        return false;
    }

    // Allocate cluster for new directory
    auto fat = readFAT();
    uint16_t newCluster = allocateCluster(fat);
    if (newCluster == 0) {
        return false;
    }

    // Create directory entry
    DirEntry& entry = parentEntries[freeIdx];
    std::memset(&entry, 0, sizeof(DirEntry));
    parseFilename(dirName, entry.name, entry.ext);
    entry.attr = ATTR_DIRECTORY;
    entry.startCluster = newCluster;
    entry.fileSize = 0;

    // Initialize new directory with . and .. entries
    std::vector<DirEntry> newDirEntries(m_bytesPerSector / sizeof(DirEntry));
    std::memset(newDirEntries.data(), 0, newDirEntries.size() * sizeof(DirEntry));

    // . entry (self)
    std::memcpy(newDirEntries[0].name, ".       ", 8);
    std::memcpy(newDirEntries[0].ext, "   ", 3);
    newDirEntries[0].attr = ATTR_DIRECTORY;
    newDirEntries[0].startCluster = newCluster;

    // .. entry (parent)
    std::memcpy(newDirEntries[1].name, "..      ", 8);
    std::memcpy(newDirEntries[1].ext, "   ", 3);
    newDirEntries[1].attr = ATTR_DIRECTORY;
    newDirEntries[1].startCluster = parentCluster;

    // Write new directory cluster
    std::vector<uint8_t> clusterData(m_sectorsPerCluster * m_bytesPerSector, 0);
    std::memcpy(clusterData.data(), newDirEntries.data(),
                std::min(clusterData.size(), newDirEntries.size() * sizeof(DirEntry)));
    writeCluster(newCluster, clusterData);

    // Write FAT and parent directory
    writeFAT(fat);

    if (parentCluster == 0) {
        writeRootDirectory(parentEntries);
    } else {
        setDirectoryEntries(parentCluster, parentEntries);
    }

    return true;
}

bool Human68kHandler::deleteDirectory(const std::string& path) {
    auto [cluster, name] = resolvePath(path);
    if (cluster == 0) {
        return false;  // Directory not found
    }

    // Check if directory is empty
    auto entries = getDirectoryEntries(cluster);
    for (const auto& entry : entries) {
        if (static_cast<uint8_t>(entry.name[0]) == DIR_FREE ||
            static_cast<uint8_t>(entry.name[0]) == DIR_END) {
            continue;
        }

        // Skip . and ..
        if (entry.name[0] == '.') {
            continue;
        }

        return false;  // Directory not empty
    }

    // Find and delete directory entry in parent
    size_t lastSlash = path.rfind('/');
    if (lastSlash == std::string::npos) {
        lastSlash = path.rfind('\\');
    }

    std::vector<DirEntry> parentEntries;
    uint16_t parentCluster = 0;
    std::string dirName;

    if (lastSlash != std::string::npos && lastSlash > 0) {
        std::string parentPath = path.substr(0, lastSlash);
        dirName = path.substr(lastSlash + 1);
        auto [pCluster, pName] = resolvePath(parentPath);
        parentCluster = pCluster;
        parentEntries = (parentCluster == 0) ? readRootDirectory() : getDirectoryEntries(parentCluster);
    } else {
        dirName = (lastSlash == 0) ? path.substr(1) : path;
        parentEntries = readRootDirectory();
    }

    int idx = findDirectoryEntry(parentEntries, dirName);
    if (idx < 0) {
        return false;
    }

    // Free cluster and mark entry as deleted
    auto fat = readFAT();
    freeClusterChain(fat, parentEntries[idx].startCluster);
    parentEntries[idx].name[0] = static_cast<char>(DIR_FREE);

    writeFAT(fat);

    if (parentCluster == 0) {
        writeRootDirectory(parentEntries);
    } else {
        setDirectoryEntries(parentCluster, parentEntries);
    }

    return true;
}

bool Human68kHandler::isDirectory(const std::string& path) const {
    auto [cluster, name] = resolvePath(path);
    return cluster != 0 || path.empty() || path == "/" || path == "\\";
}

//=============================================================================
// Subdirectory Support
//=============================================================================

std::pair<uint16_t, std::string> Human68kHandler::resolvePath(const std::string& path) const {
    if (path.empty() || path == "/" || path == "\\") {
        return {0, ""};  // Root directory
    }

    std::string cleanPath = path;
    // Remove leading slash
    if (!cleanPath.empty() && (cleanPath[0] == '/' || cleanPath[0] == '\\')) {
        cleanPath = cleanPath.substr(1);
    }

    // Split path into components
    std::vector<std::string> components;
    size_t start = 0;
    for (size_t i = 0; i <= cleanPath.length(); ++i) {
        if (i == cleanPath.length() || cleanPath[i] == '/' || cleanPath[i] == '\\') {
            if (i > start) {
                components.push_back(cleanPath.substr(start, i - start));
            }
            start = i + 1;
        }
    }

    if (components.empty()) {
        return {0, ""};
    }

    // Traverse path
    uint16_t currentCluster = 0;  // Start at root
    auto entries = readRootDirectory();

    for (size_t i = 0; i < components.size(); ++i) {
        int idx = findDirectoryEntry(entries, components[i]);
        if (idx < 0) {
            return {0, ""};  // Not found
        }

        if (!(entries[idx].attr & ATTR_DIRECTORY)) {
            if (i == components.size() - 1) {
                // Last component is a file - return parent cluster
                return {currentCluster, components[i]};
            }
            return {0, ""};  // Not a directory
        }

        currentCluster = entries[idx].startCluster;

        if (i < components.size() - 1) {
            entries = getDirectoryEntries(currentCluster);
        }
    }

    return {currentCluster, components.back()};
}

std::vector<Human68kHandler::DirEntry> Human68kHandler::readDirectoryCluster(uint16_t cluster) const {
    std::vector<DirEntry> entries;
    auto data = readCluster(cluster);

    size_t numEntries = data.size() / sizeof(DirEntry);
    entries.resize(numEntries);
    std::memcpy(entries.data(), data.data(), numEntries * sizeof(DirEntry));

    return entries;
}

void Human68kHandler::writeDirectoryCluster(uint16_t cluster, const std::vector<DirEntry>& entries) {
    std::vector<uint8_t> data(m_sectorsPerCluster * m_bytesPerSector, 0);
    size_t copySize = std::min(data.size(), entries.size() * sizeof(DirEntry));
    std::memcpy(data.data(), entries.data(), copySize);
    writeCluster(cluster, data);
}

int Human68kHandler::findEntryInDirectory(uint16_t cluster, const std::string& name) const {
    auto entries = getDirectoryEntries(cluster);
    return findDirectoryEntry(entries, name);
}

std::vector<Human68kHandler::DirEntry> Human68kHandler::getDirectoryEntries(uint16_t cluster) const {
    if (cluster == 0) {
        return readRootDirectory();
    }

    std::vector<DirEntry> allEntries;
    auto chain = getClusterChain(cluster);

    for (uint16_t c : chain) {
        auto entries = readDirectoryCluster(c);
        allEntries.insert(allEntries.end(), entries.begin(), entries.end());
    }

    return allEntries;
}

void Human68kHandler::setDirectoryEntries(uint16_t cluster, const std::vector<DirEntry>& entries) {
    if (cluster == 0) {
        writeRootDirectory(entries);
        return;
    }

    auto chain = getClusterChain(cluster);
    size_t entriesPerCluster = (m_sectorsPerCluster * m_bytesPerSector) / sizeof(DirEntry);

    for (size_t i = 0; i < chain.size(); ++i) {
        std::vector<DirEntry> clusterEntries;
        size_t start = i * entriesPerCluster;
        size_t end = std::min(start + entriesPerCluster, entries.size());

        if (start < entries.size()) {
            clusterEntries.assign(entries.begin() + start, entries.begin() + end);
        }

        // Pad to full cluster
        clusterEntries.resize(entriesPerCluster);
        writeDirectoryCluster(chain[i], clusterEntries);
    }
}

} // namespace rde
