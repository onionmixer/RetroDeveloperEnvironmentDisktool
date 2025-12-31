#include "rdedisktool/DiskImageFactory.h"
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
// Format Detection
//=============================================================================

DiskFormat DiskImageFactory::detectFormat(const std::filesystem::path& path) {
    // Check if file exists
    if (!std::filesystem::exists(path)) {
        throw FileNotFoundException(path.string());
    }

    // Read file content
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw ReadException("Cannot open file: " + path.string());
    }

    // Get file size
    file.seekg(0, std::ios::end);
    size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    // Read data (limit to 64KB for format detection)
    size_t readSize = std::min(fileSize, size_t(65536));
    std::vector<uint8_t> data(readSize);
    file.read(reinterpret_cast<char*>(data.data()), readSize);

    // Store full file size for size-based detection
    data.resize(fileSize);  // Resize to actual size for size checks

    // Get extension (lowercase)
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    return detectFormat(data, extension);
}

DiskFormat DiskImageFactory::detectFormat(const std::vector<uint8_t>& data,
                                          const std::string& extension) {
    if (data.empty()) {
        return DiskFormat::Unknown;
    }

    // Try WOZ format first (has clear magic number)
    DiskFormat wozFormat = detectWOZFormat(data);
    if (wozFormat != DiskFormat::Unknown) {
        return wozFormat;
    }

    // Try DMK format
    DiskFormat dmkFormat = detectDMKFormat(data);
    if (dmkFormat != DiskFormat::Unknown) {
        return dmkFormat;
    }

    // Try XSA format (has magic number "PCK\x08")
    DiskFormat xsaFormat = detectXSAFormat(data);
    if (xsaFormat != DiskFormat::Unknown) {
        return xsaFormat;
    }

    // Detect based on extension and size
    std::string ext = extension;
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Apple II formats by extension
    if (ext == ".do" || ext == ".dsk") {
        return detectAppleFormat(data, ext);
    }
    if (ext == ".po") {
        return DiskFormat::ApplePO;
    }
    if (ext == ".nib") {
        // NIB files are 232960 bytes (6656 * 35 tracks)
        if (data.size() == 232960) {
            return DiskFormat::AppleNIB;
        }
        // NB2 variant is 6384 bytes per track
        if (data.size() == 223440) {
            return DiskFormat::AppleNIB2;
        }
    }
    if (ext == ".nb2") {
        return DiskFormat::AppleNIB2;
    }
    if (ext == ".woz") {
        return detectWOZFormat(data);
    }

    // MSX formats by extension
    if (ext == ".dmk") {
        return DiskFormat::MSXDMK;
    }
    if (ext == ".xsa") {
        return DiskFormat::MSXXSA;
    }

    // For .dsk extension, need to determine if it's Apple II or MSX
    if (ext == ".dsk" || ext.empty()) {
        // Check size to distinguish
        size_t fileSize = data.size();

        // Apple II standard sizes
        if (fileSize == 143360) {  // 35 tracks * 16 sectors * 256 bytes
            return detectAppleFormat(data, ext);
        }
        if (fileSize == 116480) {  // 35 tracks * 13 sectors * 256 bytes (DOS 3.2)
            return DiskFormat::AppleDO;
        }

        // MSX standard sizes
        if (fileSize == 163840 ||   // 1S 40T 8S (320KB single-sided)
            fileSize == 184320 ||   // 1S 40T 9S (360KB)
            fileSize == 327680 ||   // 2S 40T 8S (640KB double-sided)
            fileSize == 368640 ||   // 2S 40T 9S (720KB)
            fileSize == 737280) {   // 2S 80T 9S (1.44MB - actually 720KB DS/DD)
            return detectMSXFormat(data, ext);
        }
    }

    return DiskFormat::Unknown;
}

DiskFormat DiskImageFactory::detectAppleFormat(const std::vector<uint8_t>& data,
                                               const std::string& extension) {
    // Default to DOS order for .do and .dsk
    if (extension == ".do") {
        return DiskFormat::AppleDO;
    }
    if (extension == ".po") {
        return DiskFormat::ApplePO;
    }

    // For .dsk, try to detect ProDOS vs DOS 3.3 by examining boot sector
    if (data.size() >= 512) {
        // ProDOS volumes typically have specific signatures
        // Check for ProDOS signature at block 0
        // For now, default to DOS order
    }

    return DiskFormat::AppleDO;
}

DiskFormat DiskImageFactory::detectMSXFormat(const std::vector<uint8_t>& data,
                                             const std::string& extension) {
    // Check for MSX boot sector signature
    if (data.size() >= 512) {
        // MSX boot sectors typically start with JMP instruction (0xEB or 0xE9)
        if (data[0] == 0xEB || data[0] == 0xE9) {
            return DiskFormat::MSXDSK;
        }

        // Check OEM name for MSX signatures
        // Bytes 3-10 often contain "MSX" or manufacturer name
    }

    return DiskFormat::MSXDSK;
}

DiskFormat DiskImageFactory::detectWOZFormat(const std::vector<uint8_t>& data) {
    // WOZ magic number: "WOZ1" or "WOZ2"
    if (data.size() < 8) {
        return DiskFormat::Unknown;
    }

    if (data[0] == 'W' && data[1] == 'O' && data[2] == 'Z') {
        if (data[3] == '1') {
            return DiskFormat::AppleWOZ1;
        }
        if (data[3] == '2') {
            return DiskFormat::AppleWOZ2;
        }
    }

    return DiskFormat::Unknown;
}

DiskFormat DiskImageFactory::detectDMKFormat(const std::vector<uint8_t>& data) {
    // DMK format detection
    // DMK header is 16 bytes with specific structure
    if (data.size() < 16) {
        return DiskFormat::Unknown;
    }

    // DMK header structure:
    // Byte 0: Write protect (0x00 = writable, 0xFF = protected)
    // Byte 1: Number of tracks
    // Bytes 2-3: Track length (little-endian)
    // Byte 4: Flags

    uint8_t writeProtect = data[0];
    uint8_t numTracks = data[1];
    uint16_t trackLength = data[2] | (data[3] << 8);

    // Validate DMK header
    if ((writeProtect != 0x00 && writeProtect != 0xFF) ||
        numTracks == 0 || numTracks > 86 ||
        trackLength < 128 || trackLength > 16384) {
        return DiskFormat::Unknown;
    }

    // Check if file size matches expected DMK size
    size_t expectedSize = 16 + (numTracks * trackLength);
    if (data.size() >= expectedSize) {
        return DiskFormat::MSXDMK;
    }

    return DiskFormat::Unknown;
}

DiskFormat DiskImageFactory::detectXSAFormat(const std::vector<uint8_t>& data) {
    // XSA magic number: "PCK\x08"
    if (data.size() < 4) {
        return DiskFormat::Unknown;
    }

    if (data[0] == 'P' && data[1] == 'C' &&
        data[2] == 'K' && data[3] == 0x08) {
        return DiskFormat::MSXXSA;
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
