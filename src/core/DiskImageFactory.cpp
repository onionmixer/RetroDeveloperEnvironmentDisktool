#include "rdedisktool/DiskImageFactory.h"
#include "rdedisktool/FormatDetector.h"
#include <fstream>
#include <algorithm>
#include <cctype>

namespace rde {

// Static creator map accessor
std::map<DiskFormat, DiskImageFactory::CreatorFunc>& DiskImageFactory::getCreators() {
    static std::map<DiskFormat, CreatorFunc> creators;
    return creators;
}

//=============================================================================
// Format Detection (delegated to FormatDetector)
//=============================================================================

DiskFormat DiskImageFactory::detectFormat(const std::filesystem::path& path) {
    // Delegate to FormatDetector
    return rdedisktool::FormatDetector::detect(path);
}

DiskFormat DiskImageFactory::detectFormat(const std::vector<uint8_t>& data,
                                          const std::string& extension) {
    // Delegate to FormatDetector
    return rdedisktool::FormatDetector::detect(data, extension);
}

// Legacy detection methods - delegate to FormatDetector for backwards compatibility
DiskFormat DiskImageFactory::detectAppleFormat(const std::vector<uint8_t>& data,
                                               const std::string& extension) {
    return rdedisktool::FormatDetector::detectAppleFormat(data, extension);
}

DiskFormat DiskImageFactory::detectMSXFormat(const std::vector<uint8_t>& data,
                                             const std::string& extension) {
    return rdedisktool::FormatDetector::detectMSXFormat(data, extension);
}

DiskFormat DiskImageFactory::detectWOZFormat(const std::vector<uint8_t>& data) {
    return rdedisktool::FormatDetector::detectWOZFormat(data);
}

DiskFormat DiskImageFactory::detectDMKFormat(const std::vector<uint8_t>& data) {
    return rdedisktool::FormatDetector::detectDMKFormat(data);
}

DiskFormat DiskImageFactory::detectXSAFormat(const std::vector<uint8_t>& data) {
    return rdedisktool::FormatDetector::detectXSAFormat(data);
}

DiskFormat DiskImageFactory::detectDSKByContent(const std::vector<uint8_t>& data) {
    // Analyze boot sector content to determine if it's MSX or Apple II
    if (data.size() < 512) {
        return DiskFormat::Unknown;
    }

    // Check for MSX-DOS boot sector signatures
    // MSX boot sectors typically start with JMP instruction (0xEB xx 0x90 or 0xE9 xx xx)
    bool hasMSXJump = (data[0] == 0xEB && data[2] == 0x90) ||
                      (data[0] == 0xE9);

    if (hasMSXJump) {
        // Check for valid FAT12 BPB (BIOS Parameter Block)
        uint16_t bytesPerSector = data[0x0B] | (data[0x0C] << 8);
        uint8_t sectorsPerCluster = data[0x0D];
        uint8_t numberOfFATs = data[0x10];
        uint16_t rootEntries = data[0x11] | (data[0x12] << 8);

        // Validate BPB fields for FAT12
        if (bytesPerSector == 512 &&
            sectorsPerCluster > 0 && sectorsPerCluster <= 8 &&
            numberOfFATs >= 1 && numberOfFATs <= 2 &&
            rootEntries > 0 && rootEntries <= 512) {
            return DiskFormat::MSXDSK;
        }
    }

    // Check for Apple II DOS 3.3 signature
    // DOS 3.3 VTOC is at track 17, sector 0 (offset 17 * 16 * 256 = 69632)
    if (data.size() >= 143360) {
        size_t vtocOffset = 17 * 16 * 256;  // Track 17, Sector 0
        if (data.size() > vtocOffset + 256) {
            // VTOC signature: byte 1 = catalog track (usually 17)
            // byte 2 = catalog sector (usually 15)
            // byte 3 = DOS version (usually 3)
            uint8_t catalogTrack = data[vtocOffset + 1];
            uint8_t catalogSector = data[vtocOffset + 2];
            uint8_t dosVersion = data[vtocOffset + 3];

            if (catalogTrack == 17 && catalogSector <= 15 && dosVersion == 3) {
                return DiskFormat::AppleDO;
            }
        }
    }

    // Check for ProDOS signature at block 2 (volume directory)
    if (data.size() >= 143360) {
        // In ProDOS order, block 2 is at offset 2 * 512 = 1024
        // Volume directory header has storage type 0xF0 in first byte
        size_t blockOffset = 2 * 512;
        if (data.size() > blockOffset + 4) {
            uint8_t storageType = (data[blockOffset + 4] >> 4) & 0x0F;
            if (storageType == 0x0F) {  // Volume header
                return DiskFormat::ApplePO;
            }
        }
    }

    // Default: if file size matches Apple II size, assume Apple format
    if (data.size() == 143360) {
        return DiskFormat::AppleDO;
    }

    return DiskFormat::Unknown;
}

Platform DiskImageFactory::getPlatformForFormat(DiskFormat format) {
    switch (format) {
        case DiskFormat::AppleDO:
        case DiskFormat::ApplePO:
        case DiskFormat::AppleNIB:
        case DiskFormat::AppleNIB2:
        case DiskFormat::AppleWOZ1:
        case DiskFormat::AppleWOZ2:
            return Platform::AppleII;

        case DiskFormat::MSXDSK:
        case DiskFormat::MSXDMK:
        case DiskFormat::MSXXSA:
            return Platform::MSX;

        case DiskFormat::X68000XDF:
        case DiskFormat::X68000DIM:
            return Platform::X68000;

        default:
            return Platform::Unknown;
    }
}

//=============================================================================
// Image Creation
//=============================================================================

std::unique_ptr<DiskImage> DiskImageFactory::open(const std::filesystem::path& path,
                                                  DiskFormat format) {
    // Auto-detect format if not specified
    if (format == DiskFormat::Unknown) {
        format = detectFormat(path);
    }

    if (format == DiskFormat::Unknown) {
        throw InvalidFormatException("Unable to detect disk format: " + path.string());
    }

    // Check if we have a registered creator for this format
    auto& creators = getCreators();
    auto it = creators.find(format);
    if (it == creators.end()) {
        throw UnsupportedFormatException(formatToString(format));
    }

    // Create the disk image instance
    auto image = it->second();
    image->load(path);
    return image;
}

std::unique_ptr<DiskImage> DiskImageFactory::create(DiskFormat format,
                                                    const DiskGeometry& geometry) {
    if (format == DiskFormat::Unknown) {
        throw InvalidFormatException("Cannot create disk with unknown format");
    }

    auto& creators = getCreators();
    auto it = creators.find(format);
    if (it == creators.end()) {
        throw UnsupportedFormatException(formatToString(format));
    }

    auto image = it->second();

    // Use provided geometry or get default
    DiskGeometry geom = geometry;
    if (geom.tracks == 0) {
        geom = getDefaultGeometry(format);
    }

    image->create(geom);
    return image;
}

std::unique_ptr<DiskImage> DiskImageFactory::createWithDefaultGeometry(DiskFormat format) {
    return create(format, getDefaultGeometry(format));
}

//=============================================================================
// Format Information
//=============================================================================

DiskGeometry DiskImageFactory::getDefaultGeometry(DiskFormat format) {
    DiskGeometry geom;

    switch (format) {
        // Apple II formats
        case DiskFormat::AppleDO:
        case DiskFormat::ApplePO:
            geom.tracks = 35;
            geom.sides = 1;
            geom.sectorsPerTrack = 16;
            geom.bytesPerSector = 256;
            break;

        case DiskFormat::AppleNIB:
            geom.tracks = 35;
            geom.sides = 1;
            geom.sectorsPerTrack = 16;
            geom.bytesPerSector = 256;  // Logical, actual is 6656 per track
            break;

        case DiskFormat::AppleNIB2:
            geom.tracks = 35;
            geom.sides = 1;
            geom.sectorsPerTrack = 16;
            geom.bytesPerSector = 256;  // Logical, actual is 6384 per track
            break;

        case DiskFormat::AppleWOZ1:
        case DiskFormat::AppleWOZ2:
            geom.tracks = 35;
            geom.sides = 1;
            geom.sectorsPerTrack = 16;
            geom.bytesPerSector = 256;
            break;

        // MSX formats
        case DiskFormat::MSXDSK:
            // Default to 720KB double-sided
            geom.tracks = 80;
            geom.sides = 2;
            geom.sectorsPerTrack = 9;
            geom.bytesPerSector = 512;
            break;

        case DiskFormat::MSXDMK:
            geom.tracks = 80;
            geom.sides = 2;
            geom.sectorsPerTrack = 9;
            geom.bytesPerSector = 512;
            break;

        case DiskFormat::MSXXSA:
            geom.tracks = 80;
            geom.sides = 2;
            geom.sectorsPerTrack = 9;
            geom.bytesPerSector = 512;
            break;

        default:
            // Return empty geometry for unknown formats
            break;
    }

    return geom;
}

std::vector<std::string> DiskImageFactory::getExtensions(DiskFormat format) {
    switch (format) {
        case DiskFormat::AppleDO:
            return {".do", ".dsk"};
        case DiskFormat::ApplePO:
            return {".po"};
        case DiskFormat::AppleNIB:
            return {".nib"};
        case DiskFormat::AppleNIB2:
            return {".nb2"};
        case DiskFormat::AppleWOZ1:
        case DiskFormat::AppleWOZ2:
            return {".woz"};
        case DiskFormat::MSXDSK:
            return {".dsk"};
        case DiskFormat::MSXDMK:
            return {".dmk"};
        case DiskFormat::MSXXSA:
            return {".xsa"};
        default:
            return {};
    }
}

std::vector<DiskFormat> DiskImageFactory::getSupportedFormats() {
    std::vector<DiskFormat> formats;
    for (const auto& [format, creator] : getCreators()) {
        formats.push_back(format);
    }
    return formats;
}

std::vector<DiskFormat> DiskImageFactory::getFormatsForPlatform(Platform platform) {
    std::vector<DiskFormat> formats;

    switch (platform) {
        case Platform::AppleII:
            formats = {
                DiskFormat::AppleDO,
                DiskFormat::ApplePO,
                DiskFormat::AppleNIB,
                DiskFormat::AppleNIB2,
                DiskFormat::AppleWOZ1,
                DiskFormat::AppleWOZ2
            };
            break;

        case Platform::MSX:
            formats = {
                DiskFormat::MSXDSK,
                DiskFormat::MSXDMK,
                DiskFormat::MSXXSA
            };
            break;

        default:
            break;
    }

    // Filter to only supported formats
    std::vector<DiskFormat> supported;
    for (auto fmt : formats) {
        if (isFormatSupported(fmt)) {
            supported.push_back(fmt);
        }
    }

    return supported;
}

bool DiskImageFactory::isFormatSupported(DiskFormat format) {
    auto& creators = getCreators();
    return creators.find(format) != creators.end();
}

DiskFormat DiskImageFactory::getFormatFromExtension(const std::string& extension) {
    std::string ext = extension;
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Add leading dot if missing
    if (!ext.empty() && ext[0] != '.') {
        ext = "." + ext;
    }

    if (ext == ".do") return DiskFormat::AppleDO;
    if (ext == ".po") return DiskFormat::ApplePO;
    if (ext == ".nib") return DiskFormat::AppleNIB;
    if (ext == ".nb2") return DiskFormat::AppleNIB2;
    if (ext == ".woz") return DiskFormat::AppleWOZ2;  // Default to v2
    if (ext == ".dmk") return DiskFormat::MSXDMK;
    if (ext == ".xsa") return DiskFormat::MSXXSA;
    if (ext == ".xdf") return DiskFormat::X68000XDF;
    if (ext == ".dim") return DiskFormat::X68000DIM;
    if (ext == ".dsk") return DiskFormat::Unknown;  // Ambiguous

    return DiskFormat::Unknown;
}

//=============================================================================
// Format Registration
//=============================================================================

void DiskImageFactory::registerFormat(DiskFormat format, CreatorFunc creator) {
    getCreators()[format] = std::move(creator);
}

void DiskImageFactory::unregisterFormat(DiskFormat format) {
    getCreators().erase(format);
}

} // namespace rde
