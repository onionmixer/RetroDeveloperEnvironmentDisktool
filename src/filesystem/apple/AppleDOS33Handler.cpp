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
#include "rdedisktool/apple/AppleDiskImage.h"
#include "rdedisktool/Exceptions.h"
#include "rdedisktool/utils/BinaryReader.h"
#include <algorithm>
#include <cstring>
#include <cctype>
#include <unordered_set>

namespace rde {

namespace {

// Catalog and track/sector lists are chains of sectors. A corrupt link that
// leads back to a sector already read would loop forever, so a repeat is an
// error (not a silent stop, which could let a write go on).
class SectorChainGuard {
public:
    explicit SectorChainGuard(const char* what) : m_what(what) {}
    void visit(uint8_t track, uint8_t sector) {
        if (!m_seen.insert(static_cast<uint16_t>(track << 8 | sector)).second) {
            throw ReadException(std::string("DOS 3.3 ") + m_what + " chain loops back to T" +
                                std::to_string(track) + " S" + std::to_string(sector));
        }
    }
private:
    const char* m_what;
    std::unordered_set<uint16_t> m_seen;
};

} // namespace

AppleDOS33Handler::AppleDOS33Handler() = default;

FileSystemType AppleDOS33Handler::getType() const {
    return m_dos32 ? FileSystemType::DOS32 : FileSystemType::DOS33;
}

void AppleDOS33Handler::requireWritable() const {
    if (m_dos32) {
        throw UnsupportedFormatException("DOS 3.2 (13-sector) disks are read-only");
    }
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
    m_dos32 = false;

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

    // Only a plausible VTOC makes this a DOS 3.3 disk: an unformatted or
    // foreign disk must not be treated (and written) as one. Looser than
    // AppleDiskImage::isDOS33, so every detected DOS 3.3 disk passes.
    // A 13-sector image holds DOS 3.2 (same layout, 13 sectors per track).
    const auto geom = m_disk->getGeometry();
    const size_t maxTracks = std::min<size_t>(geom.tracks, MAX_TRACKS);
    const size_t spt = geom.sectorsPerTrack == 13 ? 13 : SECTORS_PER_TRACK;
    if (m_vtoc.sectorsPerTrack != spt ||
        m_vtoc.tracksPerDisk == 0 || m_vtoc.tracksPerDisk > maxTracks ||
        m_vtoc.firstCatalogTrack == 0 || m_vtoc.firstCatalogTrack >= m_vtoc.tracksPerDisk ||
        m_vtoc.firstCatalogSector >= spt) {
        return false;
    }
    m_dos32 = spt == 13;

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

// DOS 3.3 uses its own logical sector numbers; ProDOS-order images (.po)
// store the same physical sector under a different number.
static size_t dosSectorInImage(DiskImage* disk, size_t sector) {
    auto* apple = dynamic_cast<AppleDiskImage*>(disk);
    if (!apple || apple->getSectorOrder() != SectorOrder::ProDOS || sector >= 16) {
        return sector;
    }
    return apple->physicalToLogical(AppleInterleave::DOS33_INTERLEAVE[sector]);
}

std::vector<uint8_t> AppleDOS33Handler::readSector(size_t track, size_t sector) const {
    if (!m_disk) {
        return {};
    }
    // DOS 3.3 and Apple II disk images use 0-based sector numbers
    return m_disk->readSector(track, 0, dosSectorInImage(m_disk, sector));
}

void AppleDOS33Handler::writeSector(size_t track, size_t sector, const std::vector<uint8_t>& data) {
    if (!m_disk) {
        return;
    }
    requireWritable();
    // DOS 3.3 and Apple II disk images use 0-based sector numbers
    m_disk->writeSector(track, 0, dosSectorInImage(m_disk, sector), data);
}

std::vector<AppleDOS33Handler::CatalogEntry> AppleDOS33Handler::readCatalog() const {
    std::vector<CatalogEntry> entries;

    uint8_t catTrack = m_vtoc.firstCatalogTrack;
    uint8_t catSector = m_vtoc.firstCatalogSector;

    SectorChainGuard chain("catalog");
    while (catTrack != 0 || catSector != 0) {
        chain.visit(catTrack, catSector);
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

    // A disk has 560 sectors; a longer chain can only be a loop
    size_t listsRead = 0;
    SectorChainGuard chain("track/sector list");
    while ((track != 0 || sector != 0) && listsRead++ < MAX_TRACKS * SECTORS_PER_TRACK) {
        chain.visit(track, sector);
        auto sectorData = readSector(track, sector);
        if (sectorData.size() < SECTOR_SIZE) {
            break;
        }

        // Read T/S pairs (122 per sector, starting at offset 0x0C)
        for (size_t i = 0; i < AppleConstants::DOS33::TS_PAIRS_PER_SECTOR; ++i) {
            size_t offset = AppleConstants::DOS33::TS_LIST_DATA_OFFSET + (i * 2);
            pairs.push_back({sectorData[offset], sectorData[offset + 1]});
        }

        // Next T/S list sector
        track = sectorData[0x01];
        sector = sectorData[0x02];
    }

    // (0,0) entries after the last data sector are unused; (0,0) entries
    // before it are holes of a sparse (random-access) file
    while (!pairs.empty() && pairs.back().track == 0 && pairs.back().sector == 0) {
        pairs.pop_back();
    }
    return pairs;
}

void AppleDOS33Handler::writeTSList(const std::vector<TSPair>& lists, const std::vector<TSPair>& pairs) {
    constexpr size_t PER_LIST = AppleConstants::DOS33::TS_PAIRS_PER_SECTOR;

    for (size_t l = 0; l < lists.size(); ++l) {
        std::vector<uint8_t> sectorData(SECTOR_SIZE, 0);

        // Link to the next T/S list sector
        if (l + 1 < lists.size()) {
            sectorData[0x01] = lists[l + 1].track;
            sectorData[0x02] = lists[l + 1].sector;
        }

        // Relative sector number in the file of the first pair in this list;
        // the DOS 3.3 File Manager uses it to find the list for a sector
        const size_t firstSector = l * PER_LIST;
        sectorData[0x05] = static_cast<uint8_t>(firstSector & 0xFF);
        sectorData[0x06] = static_cast<uint8_t>((firstSector >> 8) & 0xFF);

        for (size_t i = 0; i < PER_LIST && firstSector + i < pairs.size(); ++i) {
            size_t offset = AppleConstants::DOS33::TS_LIST_DATA_OFFSET + (i * 2);
            sectorData[offset] = pairs[firstSector + i].track;
            sectorData[offset + 1] = pairs[firstSector + i].sector;
        }

        writeSector(lists[l].track, lists[l].sector, sectorData);
    }
}

bool AppleDOS33Handler::isSectorFree(size_t track, size_t sector) const {
    if (track >= MAX_TRACKS || sector >= m_vtoc.sectorsPerTrack || sector >= 16) {
        return false;
    }

    // DOS 3.2 (13 sectors): bytes 0-1 form a big-endian word and sector s is
    // its bit s+3 (bit = 1 means free). Inferred from the free sectors of
    // real DOS 3.2 masters; only used for the free count (read-only).
    if (m_dos32) {
        const size_t bit = sector + 3;
        return (m_vtoc.trackBitmap[track][bit >= 8 ? 0 : 1] & (1 << (bit % 8))) != 0;
    }

    // Bitmap: bytes 0-1 contain sector bits
    // Bit = 1 means free, bit = 0 means used
    // Byte 0 bit k = sector 8+k, byte 1 bit k = sector k
    // (sector 15 = bit 7 of byte 0, sector 0 = bit 0 of byte 1)
    int byteIndex = sector >= 8 ? 0 : 1;
    int bitIndex = static_cast<int>(sector % 8);

    return (m_vtoc.trackBitmap[track][byteIndex] & (1 << bitIndex)) != 0;
}

void AppleDOS33Handler::markSectorUsed(size_t track, size_t sector) {
    if (track >= MAX_TRACKS || sector >= 16) {
        return;
    }
    requireWritable();

    int byteIndex = sector >= 8 ? 0 : 1;
    int bitIndex = static_cast<int>(sector % 8);

    m_vtoc.trackBitmap[track][byteIndex] &= ~(1 << bitIndex);
}

void AppleDOS33Handler::markSectorFree(size_t track, size_t sector) {
    if (track >= MAX_TRACKS || sector >= 16) {
        return;
    }
    requireWritable();

    int byteIndex = sector >= 8 ? 0 : 1;
    int bitIndex = static_cast<int>(sector % 8);

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

// Sectors in use by the VTOC, the catalog chain and every live file (T/S
// lists and data) that the bitmap shows as free are marked used. Disks
// written by older versions of this tool stored the bitmap bits of a byte in
// reverse order; without this, a new file would overwrite them.
size_t AppleDOS33Handler::markReferencedSectorsUsed() {
    size_t fixed = 0;
    auto claim = [&](size_t track, size_t sector) {
        if (track < m_vtoc.tracksPerDisk && sector < m_vtoc.sectorsPerTrack &&
            isSectorFree(track, sector)) {
            markSectorUsed(track, sector);
            ++fixed;
        }
    };
    const size_t maxChain = MAX_TRACKS * SECTORS_PER_TRACK;

    claim(VTOC_TRACK, VTOC_SECTOR);
    std::vector<TSPair> fileLists;
    uint8_t catTrack = m_vtoc.firstCatalogTrack;
    uint8_t catSector = m_vtoc.firstCatalogSector;
    for (size_t n = 0; (catTrack != 0 || catSector != 0) && n < maxChain; ++n) {
        claim(catTrack, catSector);
        const auto cat = readSector(catTrack, catSector);
        for (size_t i = 0; i < ENTRIES_PER_SECTOR; ++i) {
            const size_t o = 0x0B + i * DIR_ENTRY_SIZE;
            const uint8_t t = cat[o];
            if ((t == 0 && cat[o + 1] == 0) || t == FLAG_DELETED) {
                continue;  // empty or deleted entry
            }
            fileLists.push_back({t, cat[o + 1]});
        }
        catTrack = cat[0x01];
        catSector = cat[0x02];
    }

    for (const TSPair& first : fileLists) {
        uint8_t t = first.track, s = first.sector;
        for (size_t n = 0; (t != 0 || s != 0) && n < maxChain; ++n) {
            if (t >= m_vtoc.tracksPerDisk || s >= m_vtoc.sectorsPerTrack) {
                break;
            }
            claim(t, s);
            const auto list = readSector(t, s);
            for (size_t i = 0; i < AppleConstants::DOS33::TS_PAIRS_PER_SECTOR; ++i) {
                const size_t o = AppleConstants::DOS33::TS_LIST_DATA_OFFSET + i * 2;
                if (list[o] != 0 || list[o + 1] != 0) {
                    claim(list[o], list[o + 1]);
                }
            }
            t = list[0x01];
            s = list[0x02];
        }
    }
    return fixed;
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
    fe.typeName = fileTypeToString(entry.fileType);
    fe.locked = (entry.fileType & 0x80) != 0;  // DOS LOCK sets bit 7

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

std::vector<uint8_t> AppleDOS33Handler::readFileBytes(const std::string& filename,
                                                       uint8_t& fileType) const {
    int index = findCatalogEntry(filename);
    if (index < 0) {
        throw FileNotFoundException(filename);
    }

    auto entries = readCatalog();
    const auto& entry = entries[index];
    fileType = entry.fileType & 0x7F;

    // All data sectors in file order; holes read as zeros
    auto tsList = readTSList(entry.trackSectorListTrack, entry.trackSectorListSector);
    std::vector<uint8_t> data;
    data.reserve(tsList.size() * SECTOR_SIZE);
    for (const auto& ts : tsList) {
        if (ts.track == 0 && ts.sector == 0) {
            data.insert(data.end(), SECTOR_SIZE, 0);
        } else {
            auto sectorData = readSector(ts.track, ts.sector);
            sectorData.resize(SECTOR_SIZE, 0);
            data.insert(data.end(), sectorData.begin(), sectorData.end());
        }
    }
    return data;
}

std::vector<uint8_t> AppleDOS33Handler::readFileRaw(const std::string& filename) {
    uint8_t fileType = 0;
    return readFileBytes(filename, fileType);
}

std::vector<uint8_t> AppleDOS33Handler::readFile(const std::string& filename) {
    uint8_t fileType = 0;
    std::vector<uint8_t> data = readFileBytes(filename, fileType);

    // The file body as a program would see it
    switch (fileType) {
        case FILETYPE_BINARY: {
            // Load address (2 bytes) and length (2 bytes), then the body
            if (data.size() < 4) {
                throw InvalidFormatException("B file '" + filename + "' has no header");
            }
            const size_t length = data[2] | (static_cast<size_t>(data[3]) << 8);
            if (length + 4 > data.size()) {
                throw InvalidFormatException("B file '" + filename +
                                             "': header length exceeds the file data");
            }
            return std::vector<uint8_t>(data.begin() + 4, data.begin() + 4 + length);
        }

        case FILETYPE_APPLESOFT:
        case FILETYPE_INTEGER: {
            // Length (2 bytes), then the program
            if (data.size() < 2) {
                throw InvalidFormatException("BASIC file '" + filename + "' has no header");
            }
            const size_t length = data[0] | (static_cast<size_t>(data[1]) << 8);
            if (length + 2 > data.size()) {
                throw InvalidFormatException("BASIC file '" + filename +
                                             "': header length exceeds the file data");
            }
            return std::vector<uint8_t>(data.begin() + 2, data.begin() + 2 + length);
        }

        case FILETYPE_TEXT: {
            // A sequential text file ends at its first $00
            auto end = std::find(data.begin(), data.end(), 0x00);
            data.erase(end, data.end());
            return data;
        }

        default:
            // DOS does not record the length of other file types
            return data;
    }
}

uint8_t AppleDOS33Handler::resolveFileType(const FileMetadata& metadata) {
    std::string name;
    for (char c : metadata.fileTypeName) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            name += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }

    auto isDosType = [](unsigned long v) {
        return v == FILETYPE_TEXT || v == FILETYPE_INTEGER || v == FILETYPE_APPLESOFT ||
               v == FILETYPE_BINARY || v == FILETYPE_STYPE || v == FILETYPE_RELOCATABLE ||
               v == FILETYPE_A || v == FILETYPE_B;
    };

    if (name.empty()) {
        // Callers that pass only a numeric type use DOS 3.3 codes
        if (metadata.fileType == 0) {
            return FILETYPE_BINARY;
        }
        if (!isDosType(metadata.fileType)) {
            throw DiskException(DiskError::InvalidParameter, "Not a DOS 3.3 file type: $" +
                                         std::to_string(metadata.fileType));
        }
        return metadata.fileType;
    }

    if (name == "T" || name == "TXT") return FILETYPE_TEXT;
    if (name == "I" || name == "INT") return FILETYPE_INTEGER;
    if (name == "A" || name == "BAS") return FILETYPE_APPLESOFT;
    if (name == "B" || name == "BIN") return FILETYPE_BINARY;
    if (name == "S") return FILETYPE_STYPE;
    if (name == "R" || name == "REL") return FILETYPE_RELOCATABLE;

    std::string hex;
    if (name.size() > 1 && name[0] == '$') {
        hex = name.substr(1);
    } else if (name.size() > 2 && name[0] == '0' && name[1] == 'X') {
        hex = name.substr(2);
    }
    if (!hex.empty() && hex.size() <= 2 &&
        hex.find_first_not_of("0123456789ABCDEF") == std::string::npos) {
        const unsigned long value = std::stoul(hex, nullptr, 16);
        if (isDosType(value)) {
            return static_cast<uint8_t>(value);
        }
    }

    throw DiskException(DiskError::InvalidParameter, "File type '" + metadata.fileTypeName +
                                 "' is not available on DOS 3.3 "
                                 "(use T/I/A/B/S/R or $00/$01/$02/$04/$08/$10/$20/$40)");
}

bool AppleDOS33Handler::writeFile(const std::string& filename,
                                   const std::vector<uint8_t>& data,
                                   const FileMetadata& metadata) {
    m_lastWriteWarnings.clear();
    requireWritable();

    // Resolve the type and build the on-disk bytes before touching the disk
    const uint8_t fileType = resolveFileType(metadata);
    const bool hasLengthHeader = fileType == FILETYPE_BINARY ||
                                 fileType == FILETYPE_APPLESOFT ||
                                 fileType == FILETYPE_INTEGER;

    std::vector<uint8_t> fileData;
    if (metadata.rawData) {
        // Already the DOS file bytes; check that the header fits the data
        const size_t headerSize = fileType == FILETYPE_BINARY ? 4 : 2;
        if (hasLengthHeader) {
            if (data.size() < headerSize) {
                throw DiskException(DiskError::InvalidParameter, "Raw file is shorter than its DOS header");
            }
            const size_t lengthAt = headerSize - 2;
            const size_t length = data[lengthAt] | (static_cast<size_t>(data[lengthAt + 1]) << 8);
            if (length + headerSize > data.size()) {
                throw DiskException(DiskError::InvalidParameter, "Raw file: header length exceeds the data");
            }
        }
        fileData = data;
    } else {
        if (hasLengthHeader && data.size() > 0xFFFF) {
            throw DiskException(DiskError::InvalidParameter, "File too large for a DOS 3.3 length field (max 65535 bytes)");
        }
        const uint16_t length = static_cast<uint16_t>(data.size());

        if (fileType == FILETYPE_BINARY) {
            uint16_t loadAddr = metadata.loadAddress;
            if (!metadata.loadAddressSet) {
                loadAddr = 0x2000;
                m_lastWriteWarnings.push_back("no load address given for B file; using $2000");
            }
            if (static_cast<size_t>(loadAddr) + data.size() > 0x10000) {
                throw DiskException(DiskError::InvalidParameter, "B file does not fit in memory at its load address");
            }
            if (data.size() > 0x7FFF) {
                m_lastWriteWarnings.push_back(
                    "B file is longer than DOS 3.3 BSAVE allows ($7FFF bytes)");
            }
            fileData = {static_cast<uint8_t>(loadAddr & 0xFF), static_cast<uint8_t>(loadAddr >> 8),
                        static_cast<uint8_t>(length & 0xFF), static_cast<uint8_t>(length >> 8)};
        } else if (hasLengthHeader) {
            fileData = {static_cast<uint8_t>(length & 0xFF), static_cast<uint8_t>(length >> 8)};
        }
        fileData.insert(fileData.end(), data.begin(), data.end());

        if (fileType != FILETYPE_BINARY && metadata.loadAddressSet) {
            m_lastWriteWarnings.push_back("load address is only used for B files; ignored");
        }
        if (fileType == FILETYPE_TEXT &&
            std::find(data.begin(), data.end(), 0x00) != data.end()) {
            m_lastWriteWarnings.push_back(
                "text file contains $00; DOS 3.3 sequential reads stop there");
        }
    }

    // Check if file already exists
    int existingIndex = findCatalogEntry(filename);
    if (existingIndex >= 0) {
        // Delete existing file first
        deleteFile(filename);
    }

    // Never hand out a sector that a live file still uses (after the
    // overwrite delete, so the replaced file's sectors can be reused)
    if (const size_t fixed = markReferencedSectorsUsed()) {
        m_lastWriteWarnings.push_back(
            std::to_string(fixed) + " sector(s) in use were marked free in the VTOC bitmap "
            "(disk written by an older rdedisktool?); marked them used");
    }

    // Sectors needed: data, plus one T/S list sector per 122 data sectors
    size_t sectorsNeeded = (fileData.size() + SECTOR_SIZE - 1) / SECTOR_SIZE;
    if (sectorsNeeded == 0) {
        sectorsNeeded = 1;
    }
    constexpr size_t PER_LIST = AppleConstants::DOS33::TS_PAIRS_PER_SECTOR;
    const size_t listsNeeded = (sectorsNeeded + PER_LIST - 1) / PER_LIST;

    // Allocate every sector up front so a full disk leaves nothing behind
    std::vector<TSPair> tsLists;
    std::vector<TSPair> dataSectors;
    auto releaseAll = [&]() {
        for (const auto& s : dataSectors) markSectorFree(s.track, s.sector);
        for (const auto& s : tsLists) markSectorFree(s.track, s.sector);
    };
    for (size_t i = 0; i < listsNeeded + sectorsNeeded; ++i) {
        TSPair sector = allocateSector();
        if (sector.track == 0 && sector.sector == 0) {
            releaseAll();
            return false;  // Disk full
        }
        (tsLists.size() < listsNeeded ? tsLists : dataSectors).push_back(sector);
    }

    // Write data sectors
    size_t offset = 0;
    for (const auto& ts : dataSectors) {
        std::vector<uint8_t> sectorData(SECTOR_SIZE, 0);
        if (offset < fileData.size()) {
            size_t copySize = std::min(static_cast<size_t>(SECTOR_SIZE), fileData.size() - offset);
            std::copy(fileData.begin() + offset, fileData.begin() + offset + copySize, sectorData.begin());
        }
        writeSector(ts.track, ts.sector, sectorData);
        offset += SECTOR_SIZE;
    }

    // Write T/S list chain
    writeTSList(tsLists, dataSectors);
    const TSPair tsListSector = tsLists.front();

    // Find free catalog entry
    uint8_t catTrack = m_vtoc.firstCatalogTrack;
    uint8_t catSector = m_vtoc.firstCatalogSector;
    bool entryWritten = false;

    SectorChainGuard chain("catalog");
    while (!entryWritten && (catTrack != 0 || catSector != 0)) {
        chain.visit(catTrack, catSector);
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
                newEntry.sectorCount = static_cast<uint16_t>(sectorsNeeded + listsNeeded);

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
        releaseAll();
        return false;
    }

    // Write updated VTOC
    writeVTOC();

    return true;
}

bool AppleDOS33Handler::deleteFile(const std::string& filename) {
    requireWritable();
    int index = findCatalogEntry(filename);
    if (index < 0) {
        return false;
    }

    auto entries = readCatalog();
    const auto& entry = entries[index];

    // Free T/S list sectors and data sectors
    uint8_t tsTrack = entry.trackSectorListTrack;
    uint8_t tsSector = entry.trackSectorListSector;

    SectorChainGuard chain("track/sector list");
    while (tsTrack != 0 || tsSector != 0) {
        chain.visit(tsTrack, tsSector);
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

    SectorChainGuard catalogChain("catalog");
    while (catTrack != 0 || catSector != 0) {
        catalogChain.visit(catTrack, catSector);
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
    requireWritable();
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

    SectorChainGuard chain("catalog");
    while (catTrack != 0 || catSector != 0) {
        chain.visit(catTrack, catSector);
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
    if (geom.sectorsPerTrack != SECTORS_PER_TRACK) {
        throw UnsupportedFormatException("DOS 3.3 format needs 16 sectors per track "
                                         "(13-sector DOS 3.2 disks are read-only)");
    }

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

        // Read and validate T/S list (a list that loops back is reported below)
        std::vector<TSPair> tsList;
        try {
            tsList = readTSList(entry.trackSectorListTrack, entry.trackSectorListSector);
        } catch (const ReadException&) {
        }
        size_t actualSectorCount = 0;

        // Mark T/S list sector as used
        uint8_t tsTrack = entry.trackSectorListTrack;
        uint8_t tsSector = entry.trackSectorListSector;
        size_t tsListCount = 0;
        const size_t maxTSLists = 128;  // Reasonable limit to detect loops
        std::unordered_set<uint16_t> tsListsSeen;

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

            if (!tsListsSeen.insert(static_cast<uint16_t>(tsTrack << 8 | tsSector)).second) {
                result.addError("T/S list chain loops back to T" + std::to_string(tsTrack) +
                                "/S" + std::to_string(tsSector), filename);
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
