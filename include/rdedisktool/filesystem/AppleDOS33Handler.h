#ifndef RDEDISKTOOL_FILESYSTEM_APPLEDOS33HANDLER_H
#define RDEDISKTOOL_FILESYSTEM_APPLEDOS33HANDLER_H

#include "rdedisktool/FileSystemHandler.h"
#include "rdedisktool/apple/AppleDiskImage.h"
#include "rdedisktool/apple/AppleConstants.h"
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
    // Constants from AppleConstants::DOS33
    static constexpr size_t SECTOR_SIZE = AppleConstants::DOS33::SECTOR_SIZE;
    static constexpr size_t SECTORS_PER_TRACK = AppleConstants::DOS33::SECTORS_PER_TRACK;
    static constexpr size_t VTOC_TRACK = AppleConstants::DOS33::VTOC_TRACK;
    static constexpr size_t VTOC_SECTOR = AppleConstants::DOS33::VTOC_SECTOR;
    static constexpr size_t CATALOG_TRACK = AppleConstants::DOS33::CATALOG_TRACK;
    static constexpr size_t FIRST_CATALOG_SECTOR = AppleConstants::DOS33::FIRST_CATALOG_SECTOR;
    static constexpr size_t DIR_ENTRY_SIZE = AppleConstants::DOS33::DIR_ENTRY_SIZE;
    static constexpr size_t ENTRIES_PER_SECTOR = AppleConstants::DOS33::ENTRIES_PER_SECTOR;
    static constexpr size_t MAX_TRACKS = AppleConstants::DOS33::MAX_TRACKS;

    // File type codes from AppleConstants::DOS33
    static constexpr uint8_t FILETYPE_TEXT = AppleConstants::DOS33::FILETYPE_TEXT;
    static constexpr uint8_t FILETYPE_INTEGER = AppleConstants::DOS33::FILETYPE_INTEGER;
    static constexpr uint8_t FILETYPE_APPLESOFT = AppleConstants::DOS33::FILETYPE_APPLESOFT;
    static constexpr uint8_t FILETYPE_BINARY = AppleConstants::DOS33::FILETYPE_BINARY;
    static constexpr uint8_t FILETYPE_STYPE = AppleConstants::DOS33::FILETYPE_STYPE;
    static constexpr uint8_t FILETYPE_RELOCATABLE = AppleConstants::DOS33::FILETYPE_RELOCATABLE;
    static constexpr uint8_t FILETYPE_A = AppleConstants::DOS33::FILETYPE_A;
    static constexpr uint8_t FILETYPE_B = AppleConstants::DOS33::FILETYPE_B;

    // File flags from AppleConstants::DOS33
    static constexpr uint8_t FLAG_LOCKED = AppleConstants::DOS33::FLAG_LOCKED;
    static constexpr uint8_t FLAG_DELETED = AppleConstants::DOS33::FLAG_DELETED;

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
