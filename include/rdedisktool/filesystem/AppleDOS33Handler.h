#ifndef RDEDISKTOOL_FILESYSTEM_APPLEDOS33HANDLER_H
#define RDEDISKTOOL_FILESYSTEM_APPLEDOS33HANDLER_H

#include "rdedisktool/FileSystemHandler.h"
#include "rdedisktool/apple/AppleDiskImage.h"
#include <vector>
#include <string>

namespace rde {

/**
 * Apple II DOS 3.3 File System Handler
 *
 * Supports the DOS 3.3 file system structure:
 * - VTOC (Volume Table of Contents) at Track 17, Sector 0
 * - Catalog sectors at Track 17, Sectors 15-1
 * - Track/Sector list for each file
 *
 * Layout:
 * - Track 0: DOS boot sectors
 * - Track 1-16: User data
 * - Track 17: VTOC + Catalog
 * - Track 18-34: User data
 */
class AppleDOS33Handler : public FileSystemHandler {
public:
    AppleDOS33Handler();
    ~AppleDOS33Handler() override = default;

    // FileSystemHandler interface
    FileSystemType getType() const override;
    bool initialize(DiskImage* disk) override;
    std::vector<FileEntry> listFiles(const std::string& path = "") override;
    std::vector<uint8_t> readFile(const std::string& filename) override;
    bool writeFile(const std::string& filename,
                  const std::vector<uint8_t>& data,
                  const FileMetadata& metadata = {}) override;
    bool deleteFile(const std::string& filename) override;
    bool renameFile(const std::string& oldName,
                   const std::string& newName) override;
    size_t getFreeSpace() const override;
    size_t getTotalSpace() const override;
    bool fileExists(const std::string& filename) const override;
    bool format(const std::string& volumeName = "") override;
    std::string getVolumeName() const override;
    ValidationResult validateExtended() const override;

private:
    // Constants
    static constexpr size_t SECTOR_SIZE = 256;
    static constexpr size_t SECTORS_PER_TRACK = 16;
    static constexpr size_t VTOC_TRACK = 17;
    static constexpr size_t VTOC_SECTOR = 0;
    static constexpr size_t CATALOG_TRACK = 17;
    static constexpr size_t FIRST_CATALOG_SECTOR = 15;
    static constexpr size_t DIR_ENTRY_SIZE = 35;
    static constexpr size_t ENTRIES_PER_SECTOR = 7;
    static constexpr size_t MAX_TRACKS = 35;

    // File type codes
    static constexpr uint8_t FILETYPE_TEXT = 0x00;
    static constexpr uint8_t FILETYPE_INTEGER = 0x01;
    static constexpr uint8_t FILETYPE_APPLESOFT = 0x02;
    static constexpr uint8_t FILETYPE_BINARY = 0x04;
    static constexpr uint8_t FILETYPE_STYPE = 0x08;
    static constexpr uint8_t FILETYPE_RELOCATABLE = 0x10;
    static constexpr uint8_t FILETYPE_A = 0x20;
    static constexpr uint8_t FILETYPE_B = 0x40;

    // File flags
    static constexpr uint8_t FLAG_LOCKED = 0x80;
    static constexpr uint8_t FLAG_DELETED = 0xFF;

    // VTOC structure
    struct VTOC {
        uint8_t firstCatalogTrack;
        uint8_t firstCatalogSector;
        uint8_t dosRelease;
        uint8_t volumeNumber;
        uint8_t maxTSPairs;
        uint8_t lastTrackAllocated;
        int8_t allocationDirection;
        uint8_t tracksPerDisk;
        uint8_t sectorsPerTrack;
        uint16_t bytesPerSector;
        uint8_t trackBitmap[MAX_TRACKS][4];
    };

    // Catalog entry structure
    struct CatalogEntry {
        uint8_t trackSectorListTrack;
        uint8_t trackSectorListSector;
        uint8_t fileType;
        char filename[30];
        uint16_t sectorCount;
    };

    // Track/Sector pair
    struct TSPair {
        uint8_t track;
        uint8_t sector;
    };

    // Cached VTOC
    VTOC m_vtoc;

    // Helper methods
    bool parseVTOC();
    void writeVTOC();
    std::vector<uint8_t> readSector(size_t track, size_t sector) const;
    void writeSector(size_t track, size_t sector, const std::vector<uint8_t>& data);

    std::vector<CatalogEntry> readCatalog() const;
    void writeCatalogEntry(size_t track, size_t sector, size_t entryIndex, const CatalogEntry& entry);
    int findCatalogEntry(const std::string& filename) const;

    std::vector<TSPair> readTSList(uint8_t track, uint8_t sector) const;
    void writeTSList(uint8_t track, uint8_t sector, const std::vector<TSPair>& pairs);

    bool isSectorFree(size_t track, size_t sector) const;
    void markSectorUsed(size_t track, size_t sector);
    void markSectorFree(size_t track, size_t sector);
    TSPair allocateSector();
    size_t countFreeSectors() const;

    std::string formatFilename(const char* name) const;
    void parseFilename(const std::string& filename, char* name) const;
    std::string fileTypeToString(uint8_t type) const;

    FileEntry catalogEntryToFileEntry(const CatalogEntry& entry) const;
};

} // namespace rde

#endif // RDEDISKTOOL_FILESYSTEM_APPLEDOS33HANDLER_H
