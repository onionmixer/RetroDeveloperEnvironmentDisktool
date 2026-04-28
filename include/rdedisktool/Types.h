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
    MSX,
    X68000,
    Macintosh       // Classic Macintosh (System 6/7-era floppies)
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
    MSXXSA,         // XSA compressed (.xsa)
    // X68000 formats
    X68000XDF,      // X68000 XDF format (.xdf)
    X68000DIM,      // X68000 DIM format (.dim)
    // Macintosh formats
    MacIMG,         // Raw 512-byte sector image (.img / .dsk)
    MacDC42,        // Apple Disk Copy 4.2 container (.image / .dc42)
    MacMOOF         // Applesauce MOOF (.moof) — bitstream/flux GCR/MFM
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
    FAT16,          // FAT16
    // X68000
    Human68k,       // Human68k file system
    // Macintosh
    HFS,            // Hierarchical File System
    MFS             // Macintosh File System (flat)
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

// X68000 DIM disk types
enum class X68000DIMType : uint8_t {
    DIM_2HD  = 0,   // 1024 bytes/sector, 8 sectors/track
    DIM_2HS  = 1,   // 1024 bytes/sector, 9 sectors/track
    DIM_2HC  = 2,   // 512 bytes/sector, 15 sectors/track
    DIM_2HDE = 3,   // 1024 bytes/sector, 9 sectors/track (extended)
    DIM_2HQ  = 9    // 512 bytes/sector, 18 sectors/track
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

// Validation severity level
enum class ValidationSeverity {
    Info,       // Informational
    Warning,    // Non-critical issue
    Error       // Critical issue
};

// Validation issue
struct ValidationIssue {
    ValidationSeverity severity = ValidationSeverity::Error;
    std::string message;
    std::string location;  // Optional: e.g., "Block 2", "Sector 0/0/1"

    ValidationIssue(ValidationSeverity sev, const std::string& msg,
                   const std::string& loc = "")
        : severity(sev), message(msg), location(loc) {}
};

// Validation result
struct ValidationResult {
    bool isValid = true;                       // Overall validity
    std::vector<ValidationIssue> issues;       // List of issues found
    size_t errorCount = 0;                     // Number of errors
    size_t warningCount = 0;                   // Number of warnings

    void addError(const std::string& message, const std::string& location = "") {
        issues.emplace_back(ValidationSeverity::Error, message, location);
        ++errorCount;
        isValid = false;
    }

    void addWarning(const std::string& message, const std::string& location = "") {
        issues.emplace_back(ValidationSeverity::Warning, message, location);
        ++warningCount;
    }

    void addInfo(const std::string& message, const std::string& location = "") {
        issues.emplace_back(ValidationSeverity::Info, message, location);
    }
};

// Helper functions
inline const char* platformToString(Platform p) {
    switch (p) {
        case Platform::Unknown: return "Unknown";
        case Platform::AppleII: return "Apple II";
        case Platform::MSX: return "MSX";
        case Platform::X68000: return "X68000";
        case Platform::Macintosh: return "Macintosh";
    }
    return "Unknown";
}

inline const char* formatToString(DiskFormat f) {
    switch (f) {
        case DiskFormat::Unknown: return "Unknown";
        case DiskFormat::AppleDO: return "Apple II DOS Order";
        case DiskFormat::ApplePO: return "Apple II ProDOS Order";
        case DiskFormat::AppleNIB: return "Apple II Nibble";
        case DiskFormat::AppleNIB2: return "Apple II Nibble 6384";
        case DiskFormat::AppleWOZ1: return "Apple II WOZ v1";
        case DiskFormat::AppleWOZ2: return "Apple II WOZ v2";
        case DiskFormat::MSXDSK: return "MSX DSK";
        case DiskFormat::MSXDMK: return "MSX DMK";
        case DiskFormat::MSXXSA: return "MSX XSA";
        case DiskFormat::X68000XDF: return "X68000 XDF";
        case DiskFormat::X68000DIM: return "X68000 DIM";
        case DiskFormat::MacIMG: return "Macintosh Raw Image";
        case DiskFormat::MacDC42: return "Apple Disk Copy 4.2";
        case DiskFormat::MacMOOF: return "Applesauce MOOF";
    }
    return "Unknown";
}

// Returns the canonical identifier (matches the C++ enum value name) for the format.
// Useful for stable, machine-readable output (e.g. `list-formats`) where translated
// human strings from formatToString() would be too noisy.
inline const char* formatToIdentifier(DiskFormat f) {
    switch (f) {
        case DiskFormat::Unknown: return "Unknown";
        case DiskFormat::AppleDO: return "AppleDO";
        case DiskFormat::ApplePO: return "ApplePO";
        case DiskFormat::AppleNIB: return "AppleNIB";
        case DiskFormat::AppleNIB2: return "AppleNIB2";
        case DiskFormat::AppleWOZ1: return "AppleWOZ1";
        case DiskFormat::AppleWOZ2: return "AppleWOZ2";
        case DiskFormat::MSXDSK: return "MSXDSK";
        case DiskFormat::MSXDMK: return "MSXDMK";
        case DiskFormat::MSXXSA: return "MSXXSA";
        case DiskFormat::X68000XDF: return "X68000XDF";
        case DiskFormat::X68000DIM: return "X68000DIM";
        case DiskFormat::MacIMG: return "MacIMG";
        case DiskFormat::MacDC42: return "MacDC42";
        case DiskFormat::MacMOOF: return "MacMOOF";
    }
    return "Unknown";
}

inline const char* formatToExtension(DiskFormat f) {
    switch (f) {
        case DiskFormat::Unknown: return "";
        case DiskFormat::AppleDO: return ".dsk";
        case DiskFormat::ApplePO: return ".po";
        case DiskFormat::AppleNIB: return ".nib";
        case DiskFormat::AppleNIB2: return ".nb2";
        case DiskFormat::AppleWOZ1:
        case DiskFormat::AppleWOZ2: return ".woz";
        case DiskFormat::MSXDSK: return ".dsk";
        case DiskFormat::MSXDMK: return ".dmk";
        case DiskFormat::MSXXSA: return ".xsa";
        case DiskFormat::X68000XDF: return ".xdf";
        case DiskFormat::X68000DIM: return ".dim";
        case DiskFormat::MacIMG: return ".img";
        case DiskFormat::MacDC42: return ".image";
        case DiskFormat::MacMOOF: return ".moof";
    }
    return "";
}

inline DiskFormat stringToFormat(const std::string& s) {
    // Apple II formats
    if (s == "do" || s == "dos" || s == "appledo") return DiskFormat::AppleDO;
    if (s == "po" || s == "prodos" || s == "applepo") return DiskFormat::ApplePO;
    if (s == "nib" || s == "nibble") return DiskFormat::AppleNIB;
    if (s == "nb2" || s == "nibble2") return DiskFormat::AppleNIB2;
    if (s == "woz" || s == "woz1") return DiskFormat::AppleWOZ1;
    if (s == "woz2") return DiskFormat::AppleWOZ2;
    // MSX formats
    if (s == "dsk" || s == "msxdsk" || s == "msx") return DiskFormat::MSXDSK;
    if (s == "dmk" || s == "msxdmk") return DiskFormat::MSXDMK;
    if (s == "xsa" || s == "msxxsa") return DiskFormat::MSXXSA;
    // X68000 formats
    if (s == "xdf" || s == "x68000xdf" || s == "x68k") return DiskFormat::X68000XDF;
    if (s == "dim" || s == "x68000dim") return DiskFormat::X68000DIM;
    // Macintosh formats — explicit mac_* prefix to avoid colliding with the
    // ambiguous .img / .dsk extensions shared with other platforms.
    if (s == "mac_img" || s == "macimg") return DiskFormat::MacIMG;
    if (s == "mac_dc42" || s == "macdc42" || s == "dc42") return DiskFormat::MacDC42;
    if (s == "mac_moof" || s == "macmoof" || s == "moof") return DiskFormat::MacMOOF;
    return DiskFormat::Unknown;
}

inline const char* fileSystemTypeToString(FileSystemType f) {
    switch (f) {
        case FileSystemType::Unknown: return "Unknown";
        case FileSystemType::DOS33: return "DOS 3.3";
        case FileSystemType::ProDOS: return "ProDOS";
        case FileSystemType::MSXDOS1: return "MSX-DOS 1";
        case FileSystemType::MSXDOS2: return "MSX-DOS 2";
        case FileSystemType::FAT12: return "FAT12";
        case FileSystemType::FAT16: return "FAT16";
        case FileSystemType::Human68k: return "Human68k";
        case FileSystemType::HFS: return "HFS";
        case FileSystemType::MFS: return "MFS";
    }
    return "Unknown";
}

} // namespace rde

#endif // RDEDISKTOOL_TYPES_H
