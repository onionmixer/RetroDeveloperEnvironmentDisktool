#ifndef RDEDISKTOOL_TYPES_H
#define RDEDISKTOOL_TYPES_H

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <ctime>

namespace rde {

// Platform enumeration
enum class Platform {
    Unknown,
    AppleII,
    MSX
};

// Disk format enumeration
enum class DiskFormat {
    Unknown,
    // Apple II formats
    AppleDO,        // DOS Order (.do, .dsk)
    ApplePO,        // ProDOS Order (.po)
    AppleNIB,       // Nibble format (.nib)
    AppleNIB2,      // Nibble 6384 format (.nb2)
    AppleWOZ1,      // WOZ v1 (.woz)
    AppleWOZ2,      // WOZ v2 (.woz)
    // MSX formats
    MSXDSK,         // Standard DSK (.dsk)
    MSXDMK,         // DMK format (.dmk)
    MSXXSA          // XSA compressed (.xsa)
};

// File system type
enum class FileSystemType {
    Unknown,
    // Apple II
    DOS33,          // DOS 3.3
    ProDOS,         // ProDOS
    // MSX
    MSXDOS1,        // MSX-DOS 1
    MSXDOS2,        // MSX-DOS 2
    FAT12,          // FAT12
    FAT16           // FAT16
};

// Sector order for Apple II
enum class SectorOrder {
    DOS,            // DOS 3.3 order
    ProDOS,         // ProDOS order
    Physical        // Physical order
};

// File type for Apple II DOS 3.3
enum class AppleDOS33FileType : uint8_t {
    Text = 0x00,
    IntegerBasic = 0x01,
    ApplesoftBasic = 0x02,
    Binary = 0x04,
    SType = 0x08,
    Relocatable = 0x10,
    AFile = 0x20,
    BFile = 0x40
};

// File type for Apple II ProDOS
enum class AppleProDOSFileType : uint8_t {
    Unknown = 0x00,
    Bad = 0x01,
    Text = 0x04,
    Binary = 0x06,
    Directory = 0x0F,
    ApplesoftBasic = 0xFC,
    IntegerBasic = 0xFA,
    System = 0xFF
};

// MSX file attributes
enum class MSXFileAttrib : uint8_t {
    ReadOnly = 0x01,
    Hidden = 0x02,
    System = 0x04,
    VolumeLabel = 0x08,
    Directory = 0x10,
    Archive = 0x20
};

// Disk geometry
struct DiskGeometry {
    size_t tracks = 0;
    size_t sides = 0;
    size_t sectorsPerTrack = 0;
    size_t bytesPerSector = 0;

    size_t totalSectors() const {
        return tracks * sides * sectorsPerTrack;
    }

    size_t totalSize() const {
        return totalSectors() * bytesPerSector;
    }
};

// File entry information
struct FileEntry {
    std::string name;
    size_t size = 0;
    uint16_t loadAddress = 0;
    uint16_t execAddress = 0;
    uint8_t fileType = 0;
    uint8_t attributes = 0;
    std::optional<std::time_t> createdTime;
    std::optional<std::time_t> modifiedTime;
    bool isDirectory = false;
    bool isDeleted = false;
};

// File metadata for adding files
struct FileMetadata {
    std::string targetName;
    uint8_t fileType = 0;
    uint16_t loadAddress = 0;
    uint16_t execAddress = 0;
    uint8_t attributes = 0;
    bool readOnly = false;
    bool hidden = false;
    std::optional<std::time_t> timestamp;
};

// Sector buffer type
using SectorBuffer = std::vector<uint8_t>;

// Track buffer type
using TrackBuffer = std::vector<uint8_t>;

// Helper functions
inline const char* platformToString(Platform p) {
    switch (p) {
        case Platform::AppleII: return "Apple II";
        case Platform::MSX: return "MSX";
        default: return "Unknown";
    }
}

inline const char* formatToString(DiskFormat f) {
    switch (f) {
        case DiskFormat::AppleDO: return "Apple II DOS Order";
        case DiskFormat::ApplePO: return "Apple II ProDOS Order";
        case DiskFormat::AppleNIB: return "Apple II Nibble";
        case DiskFormat::AppleNIB2: return "Apple II Nibble 6384";
        case DiskFormat::AppleWOZ1: return "Apple II WOZ v1";
        case DiskFormat::AppleWOZ2: return "Apple II WOZ v2";
        case DiskFormat::MSXDSK: return "MSX DSK";
        case DiskFormat::MSXDMK: return "MSX DMK";
        case DiskFormat::MSXXSA: return "MSX XSA";
        default: return "Unknown";
    }
}

inline const char* formatToExtension(DiskFormat f) {
    switch (f) {
        case DiskFormat::AppleDO: return ".dsk";
        case DiskFormat::ApplePO: return ".po";
        case DiskFormat::AppleNIB: return ".nib";
        case DiskFormat::AppleNIB2: return ".nb2";
        case DiskFormat::AppleWOZ1:
        case DiskFormat::AppleWOZ2: return ".woz";
        case DiskFormat::MSXDSK: return ".dsk";
        case DiskFormat::MSXDMK: return ".dmk";
        case DiskFormat::MSXXSA: return ".xsa";
        default: return "";
    }
}

} // namespace rde

#endif // RDEDISKTOOL_TYPES_H
