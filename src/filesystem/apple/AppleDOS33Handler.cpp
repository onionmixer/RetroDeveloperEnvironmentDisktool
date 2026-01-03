/**
 * Apple II DOS 3.3 File System Handler
 *
 * Full implementation of DOS 3.3 file system operations.
 *
 * Structure:
 * - Track 0: DOS boot sectors
 * - Track 17, Sector 0: VTOC (Volume Table of Contents)
 * - Track 17, Sectors 15-1: Catalog (directory)
 * - Other sectors: File data
 */

#include "rdedisktool/filesystem/AppleDOS33Handler.h"
#include "rdedisktool/Exceptions.h"
#include "rdedisktool/utils/BinaryReader.h"
#include <algorithm>
#include <cstring>
#include <cctype>

namespace rde {

AppleDOS33Handler::AppleDOS33Handler() = default;

FileSystemType AppleDOS33Handler::getType() const {
    return FileSystemType::DOS33;
}

bool AppleDOS33Handler::initialize(DiskImage* disk) {
    if (!disk) {
        return false;
    }
    m_disk = disk;
    return parseVTOC();
}

bool AppleDOS33Handler::parseVTOC() {
    if (!m_disk) {
        return false;
    }

    auto vtocData = readSector(VTOC_TRACK, VTOC_SECTOR);
    if (vtocData.size() < SECTOR_SIZE) {
        return false;
    }

    // Parse VTOC using BinaryReader
    rdedisktool::BinaryReader reader(vtocData);
    m_vtoc.firstCatalogTrack = reader.readU8(0x01);
    m_vtoc.firstCatalogSector = reader.readU8(0x02);
    m_vtoc.dosRelease = reader.readU8(0x03);
    m_vtoc.volumeNumber = reader.readU8(0x06);
    m_vtoc.maxTSPairs = reader.readU8(0x27);
    m_vtoc.lastTrackAllocated = reader.readU8(0x30);
    m_vtoc.allocationDirection = reader.readS8(0x31);
    m_vtoc.tracksPerDisk = reader.readU8(0x34);
    m_vtoc.sectorsPerTrack = reader.readU8(0x35);
    m_vtoc.bytesPerSector = reader.readU16LE(0x36);

    // Validate
    if (m_vtoc.tracksPerDisk == 0 || m_vtoc.sectorsPerTrack == 0) {
        // Use defaults
        m_vtoc.tracksPerDisk = 35;
        m_vtoc.sectorsPerTrack = 16;
        m_vtoc.bytesPerSector = 256;
    }

    // Read track bitmap (4 bytes per track, starting at offset 0x38)
    for (size_t t = 0; t < MAX_TRACKS && t < m_vtoc.tracksPerDisk; ++t) {
        size_t offset = 0x38 + (t * 4);
        if (offset + 4 <= vtocData.size()) {
            for (int i = 0; i < 4; ++i) {
                m_vtoc.trackBitmap[t][i] = vtocData[offset + i];
            }
        }
    }

    return true;
}

void AppleDOS33Handler::writeVTOC() {
    std::vector<uint8_t> vtocData(SECTOR_SIZE, 0);

    // Write VTOC header
    vtocData[0x01] = m_vtoc.firstCatalogTrack;
    vtocData[0x02] = m_vtoc.firstCatalogSector;
    vtocData[0x03] = m_vtoc.dosRelease;
    vtocData[0x06] = m_vtoc.volumeNumber;
    vtocData[0x27] = m_vtoc.maxTSPairs;
    vtocData[0x30] = m_vtoc.lastTrackAllocated;
    vtocData[0x31] = static_cast<uint8_t>(m_vtoc.allocationDirection);
    vtocData[0x34] = m_vtoc.tracksPerDisk;
    vtocData[0x35] = m_vtoc.sectorsPerTrack;
    vtocData[0x36] = m_vtoc.bytesPerSector & 0xFF;
    vtocData[0x37] = (m_vtoc.bytesPerSector >> 8) & 0xFF;

    // Write track bitmap
    for (size_t t = 0; t < MAX_TRACKS && t < m_vtoc.tracksPerDisk; ++t) {
        size_t offset = 0x38 + (t * 4);
        for (int i = 0; i < 4; ++i) {
            vtocData[offset + i] = m_vtoc.trackBitmap[t][i];
        }
    }

    writeSector(VTOC_TRACK, VTOC_SECTOR, vtocData);
}

std::vector<uint8_t> AppleDOS33Handler::readSector(size_t track, size_t sector) const {
    if (!m_disk) {
        return {};
    }
    // DOS 3.3 and Apple II disk images use 0-based sector numbers
    return m_disk->readSector(track, 0, sector);
}

void AppleDOS33Handler::writeSector(size_t track, size_t sector, const std::vector<uint8_t>& data) {
    if (!m_disk) {
        return;
    }
    // DOS 3.3 and Apple II disk images use 0-based sector numbers
    m_disk->writeSector(track, 0, sector, data);
}

std::vector<AppleDOS33Handler::CatalogEntry> AppleDOS33Handler::readCatalog() const {
    std::vector<CatalogEntry> entries;

    uint8_t catTrack = m_vtoc.firstCatalogTrack;
    uint8_t catSector = m_vtoc.firstCatalogSector;

    while (catTrack != 0 || catSector != 0) {
        auto sectorData = readSector(catTrack, catSector);
        if (sectorData.size() < SECTOR_SIZE) {
            break;
        }

        // Get next catalog sector
        uint8_t nextTrack = sectorData[0x01];
        uint8_t nextSector = sectorData[0x02];

        // Parse entries (7 per sector, starting at offset 0x0B)
        for (size_t i = 0; i < ENTRIES_PER_SECTOR; ++i) {
            size_t offset = 0x0B + (i * DIR_ENTRY_SIZE);
            if (offset + DIR_ENTRY_SIZE > sectorData.size()) {
                break;
            }

            CatalogEntry entry;
            entry.trackSectorListTrack = sectorData[offset];
            entry.trackSectorListSector = sectorData[offset + 1];
            entry.fileType = sectorData[offset + 2];
            std::memcpy(entry.filename, &sectorData[offset + 3], 30);
            entry.sectorCount = sectorData[offset + 33] |
                               (static_cast<uint16_t>(sectorData[offset + 34]) << 8);

            // Skip empty entries
            if (entry.trackSectorListTrack != 0 || entry.fileType != 0) {
                entries.push_back(entry);
            }
        }

        catTrack = nextTrack;
        catSector = nextSector;
    }

    return entries;
}

void AppleDOS33Handler::writeCatalogEntry(size_t track, size_t sector, size_t entryIndex,
                                          const CatalogEntry& entry) {
    auto sectorData = readSector(track, sector);
    if (sectorData.size() < SECTOR_SIZE) {
        return;
    }

    size_t offset = 0x0B + (entryIndex * DIR_ENTRY_SIZE);
    if (offset + DIR_ENTRY_SIZE > sectorData.size()) {
        return;
    }

    sectorData[offset] = entry.trackSectorListTrack;
    sectorData[offset + 1] = entry.trackSectorListSector;
    sectorData[offset + 2] = entry.fileType;
    std::memcpy(&sectorData[offset + 3], entry.filename, 30);
    sectorData[offset + 33] = entry.sectorCount & 0xFF;
    sectorData[offset + 34] = (entry.sectorCount >> 8) & 0xFF;

    writeSector(track, sector, sectorData);
}

int AppleDOS33Handler::findCatalogEntry(const std::string& filename) const {
    char searchName[30];
    parseFilename(filename, searchName);

    auto entries = readCatalog();
    for (size_t i = 0; i < entries.size(); ++i) {
        // Skip deleted entries (DOS 3.3: trackSectorListTrack is set to 0xFF)
        if (entries[i].trackSectorListTrack == FLAG_DELETED) {
            continue;
        }

        // Compare filenames (ignore high bit)
        bool match = true;
        for (int j = 0; j < 30; ++j) {
            char c1 = searchName[j] & 0x7F;
            char c2 = entries[i].filename[j] & 0x7F;
            if (c1 != c2) {
                match = false;
                break;
            }
        }
        if (match) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

std::vector<AppleDOS33Handler::TSPair> AppleDOS33Handler::readTSList(uint8_t track, uint8_t sector) const {
    std::vector<TSPair> pairs;

    while (track != 0 || sector != 0) {
        auto sectorData = readSector(track, sector);
        if (sectorData.size() < SECTOR_SIZE) {
            break;
        }

        // Get next T/S list sector
        uint8_t nextTrack = sectorData[0x01];
        uint8_t nextSector = sectorData[0x02];

        // Read T/S pairs (up to 122 pairs per sector, starting at offset 0x0C)
        for (size_t i = 0; i < 122; ++i) {
            size_t offset = 0x0C + (i * 2);
            if (offset + 2 > sectorData.size()) {
                break;
            }

            TSPair pair;
            pair.track = sectorData[offset];
            pair.sector = sectorData[offset + 1];

            // Track 0, Sector 0 is not a valid data sector
            if (pair.track != 0 || pair.sector != 0) {
                pairs.push_back(pair);
            }
        }

        track = nextTrack;
        sector = nextSector;
    }

    return pairs;
}

void AppleDOS33Handler::writeTSList(uint8_t track, uint8_t sector, const std::vector<TSPair>& pairs) {
    std::vector<uint8_t> sectorData(SECTOR_SIZE, 0);

    // Write T/S pairs (up to 122 pairs per sector)
    size_t pairIndex = 0;
    for (size_t i = 0; i < 122 && pairIndex < pairs.size(); ++i, ++pairIndex) {
        size_t offset = 0x0C + (i * 2);
        sectorData[offset] = pairs[pairIndex].track;
        sectorData[offset + 1] = pairs[pairIndex].sector;
    }

    // If more pairs needed, allocate next T/S list sector
    if (pairIndex < pairs.size()) {
        TSPair nextTSList = allocateSector();
        sectorData[0x01] = nextTSList.track;
        sectorData[0x02] = nextTSList.sector;

        writeSector(track, sector, sectorData);

        // Write remaining pairs to next sector
        std::vector<TSPair> remaining(pairs.begin() + pairIndex, pairs.end());
        writeTSList(nextTSList.track, nextTSList.sector, remaining);
    } else {
        writeSector(track, sector, sectorData);
    }
}

bool AppleDOS33Handler::isSectorFree(size_t track, size_t sector) const {
    if (track >= MAX_TRACKS || sector >= 16) {
        return false;
    }

    // Bitmap: bytes 0-1 contain sector bits
    // Bit = 1 means free, bit = 0 means used
    // Sector 15 = bit 7 of byte 0, sector 0 = bit 0 of byte 1
    int byteIndex = (15 - sector) / 8;
    int bitIndex = (15 - sector) % 8;

    return (m_vtoc.trackBitmap[track][byteIndex] & (1 << bitIndex)) != 0;
}

void AppleDOS33Handler::markSectorUsed(size_t track, size_t sector) {
    if (track >= MAX_TRACKS || sector >= 16) {
        return;
    }

    int byteIndex = (15 - sector) / 8;
    int bitIndex = (15 - sector) % 8;

    m_vtoc.trackBitmap[track][byteIndex] &= ~(1 << bitIndex);
}

void AppleDOS33Handler::markSectorFree(size_t track, size_t sector) {
    if (track >= MAX_TRACKS || sector >= 16) {
        return;
    }

    int byteIndex = (15 - sector) / 8;
    int bitIndex = (15 - sector) % 8;

    m_vtoc.trackBitmap[track][byteIndex] |= (1 << bitIndex);
}

AppleDOS33Handler::TSPair AppleDOS33Handler::allocateSector() {
    // Allocation follows the direction in VTOC
    int track = m_vtoc.lastTrackAllocated;
    int direction = m_vtoc.allocationDirection;

    // Search for free sector
    for (int t = 0; t < static_cast<int>(m_vtoc.tracksPerDisk); ++t) {
        // Skip track 0 and VTOC track
        if (track == 0 || track == static_cast<int>(VTOC_TRACK)) {
            track += direction;
            if (track < 0) track = m_vtoc.tracksPerDisk - 1;
            if (track >= static_cast<int>(m_vtoc.tracksPerDisk)) track = 1;
            continue;
        }

        for (int s = 0; s < static_cast<int>(m_vtoc.sectorsPerTrack); ++s) {
            if (isSectorFree(track, s)) {
                markSectorUsed(track, s);
                m_vtoc.lastTrackAllocated = track;
                return {static_cast<uint8_t>(track), static_cast<uint8_t>(s)};
            }
        }

        track += direction;
        if (track < 0) {
            track = m_vtoc.tracksPerDisk - 1;
            direction = -1;
        }
        if (track >= static_cast<int>(m_vtoc.tracksPerDisk)) {
            track = 1;
            direction = 1;
        }
    }

    // No free sectors
    return {0, 0};
}

size_t AppleDOS33Handler::countFreeSectors() const {
    size_t count = 0;

    for (size_t t = 0; t < m_vtoc.tracksPerDisk; ++t) {
        // Skip track 0 (DOS) and track 17 (catalog)
        if (t == 0 || t == VTOC_TRACK) {
            continue;
        }

        for (size_t s = 0; s < m_vtoc.sectorsPerTrack; ++s) {
            if (isSectorFree(t, s)) {
                ++count;
            }
        }
    }

    return count;
}

std::string AppleDOS33Handler::formatFilename(const char* name) const {
    std::string result;

    for (int i = 29; i >= 0; --i) {
        char c = name[i] & 0x7F;  // Strip high bit
        if (c != ' ' && c != 0) {
            result = std::string(name, i + 1);
            break;
        }
    }

    // Strip high bit from all characters
    for (char& c : result) {
        c &= 0x7F;
    }

    return result;
}

void AppleDOS33Handler::parseFilename(const std::string& filename, char* name) const {
    // Initialize with spaces (high bit set for Apple II)
    std::memset(name, ' ' | 0x80, 30);

    // Copy and set high bit
    size_t len = std::min(filename.length(), static_cast<size_t>(30));
    for (size_t i = 0; i < len; ++i) {
        name[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(filename[i]))) | 0x80;
    }
}

std::string AppleDOS33Handler::fileTypeToString(uint8_t type) const {
    uint8_t baseType = type & 0x7F;  // Remove locked flag

    switch (baseType) {
        case FILETYPE_TEXT: return "T";
        case FILETYPE_INTEGER: return "I";
        case FILETYPE_APPLESOFT: return "A";
        case FILETYPE_BINARY: return "B";
        case FILETYPE_STYPE: return "S";
        case FILETYPE_RELOCATABLE: return "R";
        case FILETYPE_A: return "a";
        case FILETYPE_B: return "b";
        default: return "?";
    }
}

FileEntry AppleDOS33Handler::catalogEntryToFileEntry(const CatalogEntry& entry) const {
    FileEntry fe;
    fe.name = formatFilename(entry.filename);
    fe.size = entry.sectorCount * SECTOR_SIZE;
    fe.fileType = entry.fileType & 0x7F;
    fe.isDirectory = false;
    // DOS 3.3: Entry is deleted when trackSectorListTrack is 0xFF
    fe.isDeleted = (entry.trackSectorListTrack == FLAG_DELETED);
    fe.attributes = entry.fileType;

    return fe;
}

std::vector<FileEntry> AppleDOS33Handler::listFiles(const std::string& /*path*/) {
    std::vector<FileEntry> files;
    auto entries = readCatalog();

    for (const auto& entry : entries) {
        // Skip deleted entries (DOS 3.3: trackSectorListTrack is set to 0xFF)
        if (entry.trackSectorListTrack == FLAG_DELETED) {
            continue;
        }
        // Skip empty entries
        if (entry.trackSectorListTrack == 0 && entry.trackSectorListSector == 0) {
            continue;
        }

        files.push_back(catalogEntryToFileEntry(entry));
    }

    return files;
}

std::vector<uint8_t> AppleDOS33Handler::readFile(const std::string& filename) {
    int index = findCatalogEntry(filename);
    if (index < 0) {
        throw FileNotFoundException(filename);
    }

    auto entries = readCatalog();
    const auto& entry = entries[index];

    // Read T/S list
    auto tsList = readTSList(entry.trackSectorListTrack, entry.trackSectorListSector);

    // Read all data sectors
    std::vector<uint8_t> data;
    data.reserve(tsList.size() * SECTOR_SIZE);

    for (const auto& ts : tsList) {
        auto sectorData = readSector(ts.track, ts.sector);
        data.insert(data.end(), sectorData.begin(), sectorData.end());
    }

    // Calculate actual file size based on file type
    uint8_t fileType = entry.fileType & 0x7F;

    switch (fileType) {
        case FILETYPE_BINARY:
            // Binary files: first 2 bytes are address, next 2 are length
            if (data.size() >= 4) {
                uint16_t length = data[2] | (static_cast<uint16_t>(data[3]) << 8);
                // Validate that the length is reasonable (not larger than data minus header)
                if (length > 0 && static_cast<size_t>(length) + 4 <= data.size()) {
                    data.resize(static_cast<size_t>(length) + 4);
                } else {
                    // Invalid binary header - fall back to trimming nulls
                    size_t actualEnd = data.size();
                    while (actualEnd > 0 && data[actualEnd - 1] == 0x00) {
                        actualEnd--;
                    }
                    if (actualEnd < data.size()) {
                        data.resize(actualEnd);
                    }
                }
            }
            break;

        case FILETYPE_APPLESOFT:
        case FILETYPE_INTEGER:
            // BASIC files: first 2 bytes are length
            if (data.size() >= 2) {
                uint16_t length = data[0] | (static_cast<uint16_t>(data[1]) << 8);
                if (static_cast<size_t>(length) + 2 <= data.size()) {
                    data.resize(static_cast<size_t>(length) + 2);
                }
            }
            break;

        case FILETYPE_TEXT:
        default:
            // Text and other files: trim trailing nulls
            {
                size_t actualEnd = data.size();
                while (actualEnd > 0 && data[actualEnd - 1] == 0x00) {
                    actualEnd--;
                }
                if (actualEnd < data.size()) {
                    data.resize(actualEnd);
                }
            }
            break;
    }

    return data;
}

bool AppleDOS33Handler::writeFile(const std::string& filename,
                                   const std::vector<uint8_t>& data,
                                   const FileMetadata& metadata) {
    // Check if file already exists
    int existingIndex = findCatalogEntry(filename);
    if (existingIndex >= 0) {
        // Delete existing file first
        deleteFile(filename);
    }

    // Determine file type
    uint8_t fileType = metadata.fileType != 0 ? metadata.fileType : FILETYPE_BINARY;

    // Prepare file data - add header for Binary/Applesoft/Integer files if load address specified
    std::vector<uint8_t> fileData;
    bool needsHeader = (fileType == FILETYPE_BINARY ||
                        fileType == FILETYPE_APPLESOFT ||
                        fileType == FILETYPE_INTEGER) &&
                       metadata.loadAddress != 0;

    if (needsHeader) {
        // Check if data already has a valid header (load address + length)
        // by checking if first 4 bytes could be a header
        bool hasExistingHeader = false;
        if (data.size() >= 4) {
            uint16_t existingLen = static_cast<uint16_t>(data[2]) |
                                   (static_cast<uint16_t>(data[3]) << 8);
            // If the length in header matches remaining data size, assume header exists
            if (existingLen == data.size() - 4) {
                hasExistingHeader = true;
            }
        }

        if (!hasExistingHeader) {
            // Add 4-byte header: load address (2 bytes) + length (2 bytes)
            uint16_t loadAddr = metadata.loadAddress;
            uint16_t fileLen = static_cast<uint16_t>(data.size());

            fileData.reserve(data.size() + 4);
            fileData.push_back(loadAddr & 0xFF);          // Load address low byte
            fileData.push_back((loadAddr >> 8) & 0xFF);   // Load address high byte
            fileData.push_back(fileLen & 0xFF);           // Length low byte
            fileData.push_back((fileLen >> 8) & 0xFF);    // Length high byte
            fileData.insert(fileData.end(), data.begin(), data.end());
        } else {
            fileData = data;  // Use data as-is (already has header)
        }
    } else {
        fileData = data;  // No header needed
    }

    // Calculate sectors needed
    size_t sectorsNeeded = (fileData.size() + SECTOR_SIZE - 1) / SECTOR_SIZE;
    if (sectorsNeeded == 0) {
        sectorsNeeded = 1;
    }

    // Allocate T/S list sector
    TSPair tsListSector = allocateSector();
    if (tsListSector.track == 0 && tsListSector.sector == 0) {
        return false; // Disk full
    }

    // Allocate data sectors
    std::vector<TSPair> dataSectors;
    for (size_t i = 0; i < sectorsNeeded; ++i) {
        TSPair sector = allocateSector();
        if (sector.track == 0 && sector.sector == 0) {
            // Disk full - free allocated sectors
            for (const auto& s : dataSectors) {
                markSectorFree(s.track, s.sector);
            }
            markSectorFree(tsListSector.track, tsListSector.sector);
            return false;
        }
        dataSectors.push_back(sector);
    }

    // Write data sectors
    size_t offset = 0;
    for (const auto& ts : dataSectors) {
        std::vector<uint8_t> sectorData(SECTOR_SIZE, 0);
        size_t copySize = std::min(static_cast<size_t>(SECTOR_SIZE), fileData.size() - offset);
        if (offset < fileData.size()) {
            std::copy(fileData.begin() + offset, fileData.begin() + offset + copySize, sectorData.begin());
        }
        writeSector(ts.track, ts.sector, sectorData);
        offset += SECTOR_SIZE;
    }

    // Write T/S list
    writeTSList(tsListSector.track, tsListSector.sector, dataSectors);

    // Find free catalog entry
    uint8_t catTrack = m_vtoc.firstCatalogTrack;
    uint8_t catSector = m_vtoc.firstCatalogSector;
    bool entryWritten = false;

    while (!entryWritten && (catTrack != 0 || catSector != 0)) {
        auto sectorData = readSector(catTrack, catSector);
        if (sectorData.size() < SECTOR_SIZE) {
            break;
        }

        uint8_t nextTrack = sectorData[0x01];
        uint8_t nextSector = sectorData[0x02];

        for (size_t i = 0; i < ENTRIES_PER_SECTOR; ++i) {
            size_t entryOffset = 0x0B + (i * DIR_ENTRY_SIZE);
            uint8_t tsTrack = sectorData[entryOffset];

            // Check for free (tsTrack == 0) or deleted (tsTrack == 0xFF) entry
            if (tsTrack == 0 || tsTrack == FLAG_DELETED) {
                CatalogEntry newEntry;
                newEntry.trackSectorListTrack = tsListSector.track;
                newEntry.trackSectorListSector = tsListSector.sector;
                newEntry.fileType = fileType;
                parseFilename(filename, newEntry.filename);
                // Calculate T/S list sectors: each can hold 122 data sector pairs
                size_t tsListSectors = (sectorsNeeded + 121) / 122;
                newEntry.sectorCount = static_cast<uint16_t>(sectorsNeeded + tsListSectors);

                writeCatalogEntry(catTrack, catSector, i, newEntry);
                entryWritten = true;
                break;
            }
        }

        catTrack = nextTrack;
        catSector = nextSector;
    }

    if (!entryWritten) {
        // No free catalog entries - free all allocated sectors
        for (const auto& s : dataSectors) {
            markSectorFree(s.track, s.sector);
        }
        markSectorFree(tsListSector.track, tsListSector.sector);
        return false;
    }

    // Write updated VTOC
    writeVTOC();

    return true;
}

bool AppleDOS33Handler::deleteFile(const std::string& filename) {
    int index = findCatalogEntry(filename);
    if (index < 0) {
        return false;
    }

    auto entries = readCatalog();
    const auto& entry = entries[index];

    // Free T/S list sectors and data sectors
    uint8_t tsTrack = entry.trackSectorListTrack;
    uint8_t tsSector = entry.trackSectorListSector;

    while (tsTrack != 0 || tsSector != 0) {
        auto sectorData = readSector(tsTrack, tsSector);
        if (sectorData.size() < SECTOR_SIZE) {
            break;
        }

        uint8_t nextTrack = sectorData[0x01];
        uint8_t nextSector = sectorData[0x02];

        // Free data sectors in this T/S list
        for (size_t i = 0; i < 122; ++i) {
            size_t offset = 0x0C + (i * 2);
            uint8_t dataTrack = sectorData[offset];
            uint8_t dataSector = sectorData[offset + 1];

            if (dataTrack != 0 || dataSector != 0) {
                markSectorFree(dataTrack, dataSector);
            }
        }

        // Free this T/S list sector
        markSectorFree(tsTrack, tsSector);

        tsTrack = nextTrack;
        tsSector = nextSector;
    }

    // Mark catalog entry as deleted
    uint8_t catTrack = m_vtoc.firstCatalogTrack;
    uint8_t catSector = m_vtoc.firstCatalogSector;
    int entryCount = 0;

    while (catTrack != 0 || catSector != 0) {
        auto sectorData = readSector(catTrack, catSector);
        if (sectorData.size() < SECTOR_SIZE) {
            break;
        }

        uint8_t nextTrack = sectorData[0x01];
        uint8_t nextSector = sectorData[0x02];

        for (size_t i = 0; i < ENTRIES_PER_SECTOR; ++i) {
            if (entryCount == index) {
                // Found the entry - mark as deleted
                // DOS 3.3 standard deletion:
                // - offset+0 (T/S list track): Set to 0xFF to mark as deleted
                // - offset+3 (first char of filename): Store original T/S track for recovery
                size_t offset = 0x0B + (i * DIR_ENTRY_SIZE);
                sectorData[offset + 3] = entry.trackSectorListTrack;  // Save T/S track for recovery
                sectorData[offset] = FLAG_DELETED;  // Mark entry as deleted (0xFF)
                sectorData[offset + 1] = 0;  // Clear T/S list sector
                writeSector(catTrack, catSector, sectorData);

                // Write updated VTOC
                writeVTOC();
                return true;
            }

            size_t entryOffset = 0x0B + (i * DIR_ENTRY_SIZE);
            if (sectorData[entryOffset] != 0 || sectorData[entryOffset + 2] != 0) {
                ++entryCount;
            }
        }

        catTrack = nextTrack;
        catSector = nextSector;
    }

    return false;
}

bool AppleDOS33Handler::renameFile(const std::string& oldName, const std::string& newName) {
    int index = findCatalogEntry(oldName);
    if (index < 0) {
        return false;
    }

    // Check if new name already exists
    if (findCatalogEntry(newName) >= 0) {
        return false;
    }

    // Find and update the catalog entry
    uint8_t catTrack = m_vtoc.firstCatalogTrack;
    uint8_t catSector = m_vtoc.firstCatalogSector;
    int entryCount = 0;

    while (catTrack != 0 || catSector != 0) {
        auto sectorData = readSector(catTrack, catSector);
        if (sectorData.size() < SECTOR_SIZE) {
            break;
        }

        uint8_t nextTrack = sectorData[0x01];
        uint8_t nextSector = sectorData[0x02];

        for (size_t i = 0; i < ENTRIES_PER_SECTOR; ++i) {
            size_t entryOffset = 0x0B + (i * DIR_ENTRY_SIZE);
            if (sectorData[entryOffset] != 0 || sectorData[entryOffset + 2] != 0) {
                if (entryCount == index) {
                    // Found the entry - update filename
                    char newFilename[30];
                    parseFilename(newName, newFilename);
                    std::memcpy(&sectorData[entryOffset + 3], newFilename, 30);
                    writeSector(catTrack, catSector, sectorData);
                    return true;
                }
                ++entryCount;
            }
        }

        catTrack = nextTrack;
        catSector = nextSector;
    }

    return false;
}

size_t AppleDOS33Handler::getFreeSpace() const {
    return countFreeSectors() * SECTOR_SIZE;
}

size_t AppleDOS33Handler::getTotalSpace() const {
    // Exclude track 0 (DOS) and track 17 (catalog)
    size_t usableTracks = m_vtoc.tracksPerDisk - 2;
    return usableTracks * m_vtoc.sectorsPerTrack * SECTOR_SIZE;
}

bool AppleDOS33Handler::fileExists(const std::string& filename) const {
    return findCatalogEntry(filename) >= 0;
}

bool AppleDOS33Handler::format(const std::string& /*volumeName*/) {
    if (!m_disk) {
        return false;
    }

    auto geom = m_disk->getGeometry();

    // Initialize VTOC
    std::memset(&m_vtoc, 0, sizeof(VTOC));
    m_vtoc.firstCatalogTrack = CATALOG_TRACK;
    m_vtoc.firstCatalogSector = FIRST_CATALOG_SECTOR;
    m_vtoc.dosRelease = 3;  // DOS 3.3
    m_vtoc.volumeNumber = 254;  // Default volume number
    m_vtoc.maxTSPairs = 122;
    m_vtoc.lastTrackAllocated = VTOC_TRACK;
    m_vtoc.allocationDirection = 1;
    m_vtoc.tracksPerDisk = static_cast<uint8_t>(geom.tracks);
    m_vtoc.sectorsPerTrack = static_cast<uint8_t>(geom.sectorsPerTrack);
    m_vtoc.bytesPerSector = static_cast<uint16_t>(geom.bytesPerSector);

    // Initialize track bitmap - all sectors free except track 0 and 17
    for (size_t t = 0; t < MAX_TRACKS; ++t) {
        if (t < m_vtoc.tracksPerDisk) {
            if (t == 0 || t == VTOC_TRACK) {
                // Track 0 (DOS) and track 17 (catalog) are used
                m_vtoc.trackBitmap[t][0] = 0x00;
                m_vtoc.trackBitmap[t][1] = 0x00;
            } else {
                // All sectors free
                m_vtoc.trackBitmap[t][0] = 0xFF;
                m_vtoc.trackBitmap[t][1] = 0xFF;
            }
            m_vtoc.trackBitmap[t][2] = 0x00;
            m_vtoc.trackBitmap[t][3] = 0x00;
        }
    }

    // Write VTOC
    writeVTOC();

    // Initialize catalog sectors (15 down to 1)
    for (int s = static_cast<int>(FIRST_CATALOG_SECTOR); s >= 1; --s) {
        std::vector<uint8_t> catSector(SECTOR_SIZE, 0);

        // Next catalog sector
        if (s > 1) {
            catSector[0x01] = CATALOG_TRACK;
            catSector[0x02] = s - 1;
        }

        writeSector(CATALOG_TRACK, s, catSector);
    }

    return true;
}

std::string AppleDOS33Handler::getVolumeName() const {
    // DOS 3.3 doesn't have a volume name in the traditional sense
    // Return the volume number as a string
    return "DISK VOLUME " + std::to_string(m_vtoc.volumeNumber);
}

ValidationResult AppleDOS33Handler::validateExtended() const {
    ValidationResult result;

    if (!m_disk) {
        result.addError("Disk image not loaded");
        return result;
    }

    // 1. Validate VTOC structure
    if (m_vtoc.tracksPerDisk == 0 || m_vtoc.tracksPerDisk > MAX_TRACKS) {
        result.addError("Invalid tracks per disk: " + std::to_string(m_vtoc.tracksPerDisk), "VTOC");
    }

    if (m_vtoc.sectorsPerTrack == 0 || m_vtoc.sectorsPerTrack > SECTORS_PER_TRACK) {
        result.addError("Invalid sectors per track: " + std::to_string(m_vtoc.sectorsPerTrack), "VTOC");
    }

    if (m_vtoc.bytesPerSector != SECTOR_SIZE) {
        result.addWarning("Non-standard bytes per sector: " + std::to_string(m_vtoc.bytesPerSector), "VTOC");
    }

    if (m_vtoc.firstCatalogTrack >= m_vtoc.tracksPerDisk) {
        result.addError("First catalog track out of range: " + std::to_string(m_vtoc.firstCatalogTrack), "VTOC");
    }

    if (m_vtoc.firstCatalogSector >= m_vtoc.sectorsPerTrack) {
        result.addError("First catalog sector out of range: " + std::to_string(m_vtoc.firstCatalogSector), "VTOC");
    }

    // 2. Validate catalog chain and track used sectors
    std::vector<std::vector<bool>> usedSectors(m_vtoc.tracksPerDisk,
                                                std::vector<bool>(m_vtoc.sectorsPerTrack, false));

    // Mark Track 0 as used (boot sectors)
    for (size_t s = 0; s < m_vtoc.sectorsPerTrack; ++s) {
        usedSectors[0][s] = true;
    }

    // Mark VTOC sector as used
    usedSectors[VTOC_TRACK][VTOC_SECTOR] = true;

    // Validate catalog chain
    uint8_t catTrack = m_vtoc.firstCatalogTrack;
    uint8_t catSector = m_vtoc.firstCatalogSector;
    size_t catalogSectorCount = 0;
    const size_t maxCatalogSectors = 15;  // DOS 3.3 uses sectors 15 down to 1

    while ((catTrack != 0 || catSector != 0) && catalogSectorCount < maxCatalogSectors + 1) {
        if (catTrack >= m_vtoc.tracksPerDisk || catSector >= m_vtoc.sectorsPerTrack) {
            result.addError("Catalog chain points to invalid sector: T" +
                           std::to_string(catTrack) + "/S" + std::to_string(catSector), "Catalog");
            break;
        }

        if (usedSectors[catTrack][catSector] && catTrack != VTOC_TRACK) {
            result.addWarning("Catalog sector already marked as used: T" +
                             std::to_string(catTrack) + "/S" + std::to_string(catSector), "Catalog");
        }
        usedSectors[catTrack][catSector] = true;
        ++catalogSectorCount;

        auto sectorData = readSector(catTrack, catSector);
        if (sectorData.size() < SECTOR_SIZE) {
            result.addError("Failed to read catalog sector: T" +
                           std::to_string(catTrack) + "/S" + std::to_string(catSector), "Catalog");
            break;
        }

        catTrack = sectorData[0x01];
        catSector = sectorData[0x02];
    }

    if (catalogSectorCount > maxCatalogSectors) {
        result.addError("Catalog chain too long (possible loop): " +
                       std::to_string(catalogSectorCount) + " sectors", "Catalog");
    }

    // 3. Validate each file's T/S list and track sectors
    auto catalog = readCatalog();
    size_t fileCount = 0;

    for (const auto& entry : catalog) {
        // Skip deleted or empty entries
        if (entry.trackSectorListTrack == 0 && entry.trackSectorListSector == 0) {
            continue;
        }
        if (entry.trackSectorListTrack == FLAG_DELETED) {
            continue;
        }

        std::string filename = formatFilename(entry.filename);
        ++fileCount;

        // Validate T/S list track/sector
        if (entry.trackSectorListTrack >= m_vtoc.tracksPerDisk ||
            entry.trackSectorListSector >= m_vtoc.sectorsPerTrack) {
            result.addError("File has invalid T/S list pointer: T" +
                           std::to_string(entry.trackSectorListTrack) + "/S" +
                           std::to_string(entry.trackSectorListSector), filename);
            continue;
        }

        // Read and validate T/S list
        auto tsList = readTSList(entry.trackSectorListTrack, entry.trackSectorListSector);
        size_t actualSectorCount = 0;

        // Mark T/S list sector as used
        uint8_t tsTrack = entry.trackSectorListTrack;
        uint8_t tsSector = entry.trackSectorListSector;
        size_t tsListCount = 0;
        const size_t maxTSLists = 128;  // Reasonable limit to detect loops

        while (tsTrack != 0 || tsSector != 0) {
            if (tsListCount >= maxTSLists) {
                result.addError("T/S list chain too long (possible loop)", filename);
                break;
            }

            if (tsTrack >= m_vtoc.tracksPerDisk || tsSector >= m_vtoc.sectorsPerTrack) {
                result.addError("T/S list chain points to invalid sector: T" +
                               std::to_string(tsTrack) + "/S" + std::to_string(tsSector), filename);
                break;
            }

            if (usedSectors[tsTrack][tsSector]) {
                result.addWarning("Sector referenced multiple times: T" +
                                 std::to_string(tsTrack) + "/S" + std::to_string(tsSector), filename);
            }
            usedSectors[tsTrack][tsSector] = true;
            ++tsListCount;

            auto tsData = readSector(tsTrack, tsSector);
            if (tsData.size() < SECTOR_SIZE) {
                result.addError("Failed to read T/S list sector", filename);
                break;
            }

            // Next T/S list sector
            tsTrack = tsData[0x01];
            tsSector = tsData[0x02];
        }

        // Validate each data sector in T/S list
        for (const auto& ts : tsList) {
            if (ts.track == 0 && ts.sector == 0) {
                continue;  // Empty slot
            }

            if (ts.track >= m_vtoc.tracksPerDisk || ts.sector >= m_vtoc.sectorsPerTrack) {
                result.addError("File references invalid sector: T" +
                               std::to_string(ts.track) + "/S" + std::to_string(ts.sector), filename);
                continue;
            }

            if (usedSectors[ts.track][ts.sector]) {
                result.addWarning("Data sector referenced multiple times: T" +
                                 std::to_string(ts.track) + "/S" + std::to_string(ts.sector), filename);
            }
            usedSectors[ts.track][ts.sector] = true;
            ++actualSectorCount;
        }

        // Compare sector count (DOS 3.3 includes T/S list sectors in count)
        size_t totalSectorCount = actualSectorCount + tsListCount;
        if (totalSectorCount != entry.sectorCount) {
            result.addWarning("Sector count mismatch: catalog says " +
                             std::to_string(entry.sectorCount) + ", found " +
                             std::to_string(totalSectorCount) + " (data: " +
                             std::to_string(actualSectorCount) + ", T/S list: " +
                             std::to_string(tsListCount) + ")", filename);
        }
    }

    // 4. Verify bitmap consistency
    for (size_t t = 0; t < m_vtoc.tracksPerDisk; ++t) {
        for (size_t s = 0; s < m_vtoc.sectorsPerTrack; ++s) {
            bool bitmapSaysFree = isSectorFree(t, s);
            bool shouldBeFree = !usedSectors[t][s];

            if (bitmapSaysFree && !shouldBeFree) {
                result.addError("Sector T" + std::to_string(t) + "/S" + std::to_string(s) +
                               " is used but marked free in bitmap");
            }
            // Note: sectors marked used but not found may be orphaned, not necessarily errors
        }
    }

    // 5. Verify Track 17 protection (VTOC/Catalog area)
    for (size_t s = 1; s <= FIRST_CATALOG_SECTOR; ++s) {
        if (isSectorFree(VTOC_TRACK, s)) {
            // Catalog sectors should be marked as used
            result.addWarning("Catalog sector T17/S" + std::to_string(s) +
                             " is marked free (should be reserved)", "VTOC");
        }
    }

    if (result.errorCount == 0 && result.warningCount == 0) {
        result.addInfo("All validation checks passed");
    } else {
        result.addInfo("Found " + std::to_string(fileCount) + " file(s)");
    }

    return result;
}

} // namespace rde
