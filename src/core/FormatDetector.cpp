#include "rdedisktool/FormatDetector.h"
#include "rdedisktool/DiskImage.h"
#include "rdedisktool/msx/XSAHeader.h"
#include "rdedisktool/utils/BinaryReader.h"
#include <fstream>
#include <algorithm>
#include <cctype>

namespace rdedisktool {

//=============================================================================
// Main Detection Entry Points
//=============================================================================

rde::DiskFormat FormatDetector::detect(const std::filesystem::path& path) {
    // Check if file exists
    if (!std::filesystem::exists(path)) {
        throw rde::FileNotFoundException(path.string());
    }

    // Read file content
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw rde::ReadException("Cannot open file: " + path.string());
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
    data.resize(fileSize);

    // Get extension
    std::string extension = getExtension(path);

    return detect(data, extension);
}

rde::DiskFormat FormatDetector::detect(const std::vector<uint8_t>& data,
                                        const std::string& extension) {
    if (data.empty()) {
        return rde::DiskFormat::Unknown;
    }

    // Stage 1: Magic number detection (highest priority)
    rde::DiskFormat magicFormat = detectByMagic(data);
    if (magicFormat != rde::DiskFormat::Unknown) {
        return magicFormat;
    }

    // Stage 2: Extension-based detection
    rde::DiskFormat extFormat = detectByExtension(extension, data.size());
    if (extFormat != rde::DiskFormat::Unknown) {
        return extFormat;
    }

    // Stage 3: Content-based detection for ambiguous cases
    return detectByContent(data, extension, data.size());
}

//=============================================================================
// Detection Stages
//=============================================================================

rde::DiskFormat FormatDetector::detectByMagic(const std::vector<uint8_t>& data) {
    // Try WOZ format first (clear magic number)
    rde::DiskFormat wozFormat = detectWOZFormat(data);
    if (wozFormat != rde::DiskFormat::Unknown) {
        return wozFormat;
    }

    // Try DMK format
    rde::DiskFormat dmkFormat = detectDMKFormat(data);
    if (dmkFormat != rde::DiskFormat::Unknown) {
        return dmkFormat;
    }

    // Try XSA format (using XSAHeader)
    rde::DiskFormat xsaFormat = detectXSAFormat(data);
    if (xsaFormat != rde::DiskFormat::Unknown) {
        return xsaFormat;
    }

    return rde::DiskFormat::Unknown;
}

rde::DiskFormat FormatDetector::detectByExtension(const std::string& ext, size_t fileSize) {
    // Apple II specific extensions (unambiguous)
    if (ext == ".do") {
        return rde::DiskFormat::AppleDO;
    }
    if (ext == ".po") {
        return rde::DiskFormat::ApplePO;
    }
    if (ext == ".nib") {
        if (fileSize == APPLE_NIB) {
            return rde::DiskFormat::AppleNIB;
        }
        if (fileSize == APPLE_NIB2) {
            return rde::DiskFormat::AppleNIB2;
        }
    }
    if (ext == ".nb2") {
        return rde::DiskFormat::AppleNIB2;
    }
    if (ext == ".woz") {
        // Will be detected by magic in practice, but extension indicates Apple format
        return rde::DiskFormat::Unknown;  // Let magic detection handle it
    }

    // MSX specific extensions (unambiguous)
    if (ext == ".dmk") {
        return rde::DiskFormat::MSXDMK;
    }
    if (ext == ".xsa") {
        return rde::DiskFormat::MSXXSA;
    }

    return rde::DiskFormat::Unknown;
}

rde::DiskFormat FormatDetector::detectByContent(const std::vector<uint8_t>& data,
                                                 const std::string& ext,
                                                 size_t fileSize) {
    // For .dsk extension (or empty), need to determine platform
    if (ext == ".dsk" || ext.empty()) {
        // Check MSX standard sizes first
        if (isMSXDiskSize(fileSize)) {
            return detectMSXFormat(data, ext);
        }

        // Apple II standard sizes
        if (fileSize == APPLE_140K) {
            return detectAppleFormat(data, ext);
        }
        if (fileSize == APPLE_DOS32) {
            return rde::DiskFormat::AppleDO;
        }

        // Non-standard sizes: analyze content
        return detectDSKByContent(data);
    }

    return rde::DiskFormat::Unknown;
}

//=============================================================================
// Format-Specific Detection
//=============================================================================

rde::DiskFormat FormatDetector::detectWOZFormat(const std::vector<uint8_t>& data) {
    if (data.size() < 8) {
        return rde::DiskFormat::Unknown;
    }

    if (data[0] == 'W' && data[1] == 'O' && data[2] == 'Z') {
        if (data[3] == '1') {
            return rde::DiskFormat::AppleWOZ1;
        }
        if (data[3] == '2') {
            return rde::DiskFormat::AppleWOZ2;
        }
    }

    return rde::DiskFormat::Unknown;
}

rde::DiskFormat FormatDetector::detectDMKFormat(const std::vector<uint8_t>& data) {
    if (data.size() < 16) {
        return rde::DiskFormat::Unknown;
    }

    // DMK header validation
    uint8_t writeProtect = data[0];
    uint8_t numTracks = data[1];
    uint16_t trackLength = data[2] | (data[3] << 8);

    // Validate DMK header
    if ((writeProtect != 0x00 && writeProtect != 0xFF) ||
        numTracks == 0 || numTracks > 86 ||
        trackLength < 128 || trackLength > 16384) {
        return rde::DiskFormat::Unknown;
    }

    // Check if file size matches expected DMK size
    size_t expectedSize = 16 + (numTracks * trackLength);
    if (data.size() >= expectedSize) {
        return rde::DiskFormat::MSXDMK;
    }

    return rde::DiskFormat::Unknown;
}

rde::DiskFormat FormatDetector::detectXSAFormat(const std::vector<uint8_t>& data) {
    // Use XSAHeader for detection
    if (XSAHeader::isXSAFormat(data)) {
        return rde::DiskFormat::MSXXSA;
    }
    return rde::DiskFormat::Unknown;
}

rde::DiskFormat FormatDetector::detectAppleFormat(const std::vector<uint8_t>& data,
                                                   const std::string& ext) {
    // Unambiguous extensions
    if (ext == ".do") {
        return rde::DiskFormat::AppleDO;
    }
    if (ext == ".po") {
        return rde::DiskFormat::ApplePO;
    }

    // For .dsk, check for ProDOS signature
    if (isValidProDOSHeader(data, data.size())) {
        return rde::DiskFormat::ApplePO;
    }

    // Default to DOS order
    return rde::DiskFormat::AppleDO;
}

rde::DiskFormat FormatDetector::detectMSXFormat(const std::vector<uint8_t>& data,
                                                 const std::string& ext) {
    (void)ext;  // Currently unused, but kept for future extension

    // Check for MSX boot sector signature
    if (data.size() >= 512) {
        if (data[0] == 0xEB || data[0] == 0xE9) {
            return rde::DiskFormat::MSXDSK;
        }
    }

    return rde::DiskFormat::MSXDSK;
}

rde::DiskFormat FormatDetector::detectDSKByContent(const std::vector<uint8_t>& data) {
    if (data.size() < 512) {
        return rde::DiskFormat::Unknown;
    }

    // Check for MSX-DOS boot sector
    if (isValidMSXBPB(data)) {
        return rde::DiskFormat::MSXDSK;
    }

    // Check for DOS 3.3 VTOC
    if (isValidDOS33VTOC(data, data.size())) {
        return rde::DiskFormat::AppleDO;
    }

    // Check for ProDOS
    if (isValidProDOSHeader(data, data.size())) {
        return rde::DiskFormat::ApplePO;
    }

    // Default: if size matches Apple II, assume Apple format
    if (data.size() == APPLE_140K) {
        return rde::DiskFormat::AppleDO;
    }

    return rde::DiskFormat::Unknown;
}

//=============================================================================
// Validation Helpers
//=============================================================================

bool FormatDetector::isValidMSXBPB(const std::vector<uint8_t>& data) {
    if (data.size() < 512) {
        return false;
    }

    // Check for JMP instruction
    bool hasJump = (data[0] == 0xEB && data[2] == 0x90) || (data[0] == 0xE9);
    if (!hasJump) {
        return false;
    }

    // Use BinaryReader for endian-safe reading
    BinaryReader reader(data);

    uint16_t bytesPerSector = reader.readU16LE(0x0B);
    uint8_t sectorsPerCluster = reader.readU8(0x0D);
    uint8_t numberOfFATs = reader.readU8(0x10);
    uint16_t rootEntries = reader.readU16LE(0x11);

    // Validate BPB fields for FAT12
    return bytesPerSector == 512 &&
           sectorsPerCluster > 0 && sectorsPerCluster <= 8 &&
           numberOfFATs >= 1 && numberOfFATs <= 2 &&
           rootEntries > 0 && rootEntries <= 512;
}

bool FormatDetector::isValidDOS33VTOC(const std::vector<uint8_t>& data, size_t fileSize) {
    if (fileSize < APPLE_140K) {
        return false;
    }

    // VTOC is at track 17, sector 0
    size_t vtocOffset = 17 * 16 * 256;
    if (data.size() <= vtocOffset + 256) {
        return false;
    }

    // VTOC signature
    uint8_t catalogTrack = data[vtocOffset + 1];
    uint8_t catalogSector = data[vtocOffset + 2];
    uint8_t dosVersion = data[vtocOffset + 3];

    return catalogTrack == 17 && catalogSector <= 15 && dosVersion == 3;
}

bool FormatDetector::isValidProDOSHeader(const std::vector<uint8_t>& data, size_t fileSize) {
    if (fileSize < APPLE_140K) {
        return false;
    }

    // Volume directory at block 2 (offset 1024)
    size_t blockOffset = 2 * 512;
    if (data.size() <= blockOffset + 4) {
        return false;
    }

    // Check for volume header storage type (0xF in high nibble)
    uint8_t storageType = (data[blockOffset + 4] >> 4) & 0x0F;
    return storageType == 0x0F;
}

bool FormatDetector::isMSXDiskSize(size_t size) {
    return size == MSX_320K ||
           size == MSX_360K ||
           size == MSX_640K ||
           size == MSX_720K_40 ||
           size == MSX_720K_80;
}

//=============================================================================
// Utility Methods
//=============================================================================

std::string FormatDetector::getExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    return toLower(ext);
}

std::string FormatDetector::toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

} // namespace rdedisktool
