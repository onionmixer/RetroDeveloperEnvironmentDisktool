#include "rdedisktool/FormatDetector.h"
#include "rdedisktool/DiskImage.h"
#include "rdedisktool/macintosh/MacintoshDiskImage.h"
#include "rdedisktool/msx/XSAHeader.h"
#include "rdedisktool/utils/BinaryReader.h"
#include <fstream>
#include <algorithm>
#include <cctype>

namespace rdedisktool {

namespace {

bool looksLikeProDOSBlock2(const uint8_t* blk) {
    // Volume header at block2 offset 4:
    // high nibble storage type 0xF, low nibble name length 1..15.
    uint8_t storageType = (blk[4] >> 4) & 0x0F;
    uint8_t nameLen = blk[4] & 0x0F;
    uint8_t entryLength = blk[0x23];
    uint8_t entriesPerBlock = blk[0x24];
    uint16_t bitmapPtr = static_cast<uint16_t>(blk[0x27] | (blk[0x28] << 8));
    uint16_t totalBlocks = static_cast<uint16_t>(blk[0x29] | (blk[0x2A] << 8));

    if (storageType != 0x0F) return false;
    if (nameLen == 0 || nameLen > 15) return false;
    if (entryLength != 0x27) return false;
    if (entriesPerBlock == 0) return false;
    if (bitmapPtr == 0 || bitmapPtr >= 280) return false;
    if (totalBlocks == 0 || totalBlocks > 280) return false;
    return true;
}

bool hasProDOSHeaderPO(const std::vector<uint8_t>& data) {
    size_t blockOffset = 2 * 512;
    return data.size() >= blockOffset + 512 &&
           looksLikeProDOSBlock2(data.data() + blockOffset);
}

bool hasProDOSHeaderDO(const std::vector<uint8_t>& data) {
    // For DOS-order images, ProDOS block2 is composed from DOS logical sectors 11 and 10.
    constexpr size_t s1 = 11;
    constexpr size_t s2 = 10;
    size_t off1 = s1 * 256;
    size_t off2 = s2 * 256;
    if (data.size() < off1 + 256 || data.size() < off2 + 256) {
        return false;
    }
    uint8_t blk2[512];
    std::copy(data.begin() + off1, data.begin() + off1 + 256, blk2);
    std::copy(data.begin() + off2, data.begin() + off2 + 256, blk2 + 256);
    return looksLikeProDOSBlock2(blk2);
}

} // namespace

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

    // Try X68000 DIM format (has header with type byte)
    rde::DiskFormat dimFormat = detectDIMFormat(data);
    if (dimFormat != rde::DiskFormat::Unknown) {
        return dimFormat;
    }

    // Try Apple Disk Copy 4.2. The 0x52 magic alone is too weak (raw images can
    // hit 0x0100 by chance), so the 7-condition header check guards against
    // false positives. Payload checksum is enforced later by the loader.
    rde::DiskFormat dc42Format = detectMacDC42Format(data, data.size());
    if (dc42Format != rde::DiskFormat::Unknown) {
        return dc42Format;
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

    // X68000 specific extensions
    if (ext == ".xdf") {
        return rde::DiskFormat::X68000XDF;
    }
    if (ext == ".dim") {
        return rde::DiskFormat::X68000DIM;
    }

    // Macintosh DC42 container. .img / .dsk are intentionally NOT mapped here
    // (they are ambiguous between Apple/MSX/Mac); content stage decides them.
    if (ext == ".image" || ext == ".dc42") {
        return rde::DiskFormat::MacDC42;
    }

    return rde::DiskFormat::Unknown;
}

rde::DiskFormat FormatDetector::detectByContent(const std::vector<uint8_t>& data,
                                                 const std::string& ext,
                                                 size_t fileSize) {
    // .dsk and .img are both ambiguous container-less raw streams; route them
    // (and extensionless files) through size-based and content-based checks.
    if (ext == ".dsk" || ext == ".img" || ext.empty()) {
        // Check MSX standard sizes first
        if (isMSXDiskSize(fileSize)) {
            const rde::DiskFormat msx = detectMSXFormat(data, ext);
            if (msx != rde::DiskFormat::Unknown) {
                return msx;
            }
            // MSXDSK rejected (not a real BPB) — fall through to other checks.
        }

        // Apple II standard sizes
        if (fileSize == APPLE_140K) {
            return detectAppleFormat(data, ext);
        }
        if (fileSize == APPLE_DOS32) {
            return rde::DiskFormat::AppleDO;
        }

        // Check X68000 XDF size
        if (fileSize == X68000_XDF) {
            return detectXDFFormat(data, fileSize);
        }

        // Last fallback: Macintosh raw image (HFS / MFS signature at 0x400).
        // Run after the platform-specific size and content checks so we never
        // reclassify a known-Apple/MSX/X68000 disk as Macintosh.
        const rde::DiskFormat mac = detectMacRawFormat(data, fileSize);
        if (mac != rde::DiskFormat::Unknown) {
            return mac;
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

    // Validate DMK header - stricter validation
    if ((writeProtect != 0x00 && writeProtect != 0xFF) ||
        numTracks == 0 || numTracks > 86 ||
        trackLength < 2560 || trackLength > 16384) {  // DMK track length typically >= 2560
        return rde::DiskFormat::Unknown;
    }

    // Additional DMK validation: check reserved bytes should be zero
    bool hasReservedZeros = true;
    for (int i = 5; i < 12; ++i) {
        if (data[i] != 0x00) {
            hasReservedZeros = false;
            break;
        }
    }
    if (!hasReservedZeros) {
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

    // For .dsk, determine actual sector order from header location.
    if (hasProDOSHeaderPO(data)) {
        return rde::DiskFormat::ApplePO;
    }
    if (hasProDOSHeaderDO(data)) {
        return rde::DiskFormat::AppleDO;
    }

    // Default to DOS order
    return rde::DiskFormat::AppleDO;
}

rde::DiskFormat FormatDetector::detectMSXFormat(const std::vector<uint8_t>& data,
                                                 const std::string& ext) {
    (void)ext;  // Currently unused, but kept for future extension

    // MSX boot sectors start with an x86 jump opcode (EB short or E9 long).
    // We use the jump byte alone — not the full BPB — as the filter:
    //   * Mac 720K MFM disks (first byte 'L' = 0x4C, "LK" boot sig) are excluded.
    //   * MSX disks with corrupt BPB metadata (e.g. zeroed bytesPerSector) still
    //     classify as MSXDSK so the filesystem layer can emit the proper
    //     "invalid_bpb_or_filesystem_init_failed" diagnostic.
    if (data.size() >= 512 && (data[0] == 0xEB || data[0] == 0xE9)) {
        return rde::DiskFormat::MSXDSK;
    }

    return rde::DiskFormat::Unknown;
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

    // Check for ProDOS headers in both possible sector orders.
    if (hasProDOSHeaderPO(data)) {
        return rde::DiskFormat::ApplePO;
    }
    if (hasProDOSHeaderDO(data)) {
        return rde::DiskFormat::AppleDO;
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
    return hasProDOSHeaderPO(data) || hasProDOSHeaderDO(data);
}

bool FormatDetector::isMSXDiskSize(size_t size) {
    return size == MSX_320K ||
           size == MSX_360K ||
           size == MSX_640K ||
           size == MSX_720K_40 ||
           size == MSX_720K_80;
}

//=============================================================================
// X68000 Format Detection
//=============================================================================

rde::DiskFormat FormatDetector::detectXDFFormat(const std::vector<uint8_t>& data, size_t fileSize) {
    // XDF has a fixed size of 1,261,568 bytes
    if (fileSize != X68000_XDF) {
        return rde::DiskFormat::Unknown;
    }

    // XDF has no magic number, but we can check for X68000 IPL signature
    // X68000 boot sector typically starts with 0x60 (BRA instruction)
    if (data.size() >= 10) {
        // Check for JMP instruction at start (common in X68000)
        if (data[0] == 0x60) {
            return rde::DiskFormat::X68000XDF;
        }
        // Check for "X68IPL" signature
        if (data[3] == 'X' && data[4] == '6' && data[5] == '8' &&
            data[6] == 'I' && data[7] == 'P' && data[8] == 'L') {
            return rde::DiskFormat::X68000XDF;
        }
    }

    // If size matches exactly, assume XDF
    return rde::DiskFormat::X68000XDF;
}

rde::DiskFormat FormatDetector::detectDIMFormat(const std::vector<uint8_t>& data) {
    // DIM requires at least 256-byte header
    if (data.size() < X68000_DIM_HEADER) {
        return rde::DiskFormat::Unknown;
    }

    // Check for "DIFC HEADER" signature at offset 0xAB (171)
    // This is a strong indicator of DIM format
    bool hasDIFCSignature = false;
    if (data.size() >= 182) {  // 171 + 11 = 182
        hasDIFCSignature = (data[171] == 'D' && data[172] == 'I' &&
                           data[173] == 'F' && data[174] == 'C');
    }

    // DIM type is stored in first byte, valid values are 0-3 and 9
    uint8_t dimType = data[0];
    if (dimType > 3 && dimType != 9) {
        // If no DIFC signature and invalid type, not a DIM file
        if (!hasDIFCSignature) {
            return rde::DiskFormat::Unknown;
        }
    }

    // Check track flags validity (bytes 1-170)
    // Track flags should be 0 or 1
    int flagCount0 = 0, flagCount1 = 0, flagCountOther = 0;
    for (size_t i = 1; i < 171; ++i) {
        if (data[i] == 0) flagCount0++;
        else if (data[i] == 1) flagCount1++;
        else flagCountOther++;
    }

    // DIM track flags should be mostly 0s and 1s
    // Allow some tolerance for corrupted files
    if (flagCountOther > 10 && !hasDIFCSignature) {
        return rde::DiskFormat::Unknown;
    }

    // If we have DIFC signature, it's definitely DIM
    if (hasDIFCSignature) {
        return rde::DiskFormat::X68000DIM;
    }

    // Additional validation: check if file size is reasonable for DIM
    // Track sizes: 2HD=8192, 2HS/2HDE=9216, 2HC=7680, 2HQ=9216
    static constexpr size_t trackSizes[10] = {
        8192, 9216, 7680, 9216, 0, 0, 0, 0, 0, 9216
    };

    size_t trackSize = trackSizes[dimType];
    if (trackSize == 0) {
        return rde::DiskFormat::Unknown;
    }

    // Minimum size: header + at least one track
    size_t minSize = X68000_DIM_HEADER + trackSize;
    if (data.size() < minSize) {
        return rde::DiskFormat::Unknown;
    }

    // Count present tracks and verify file size
    size_t presentTracks = 0;
    for (size_t i = 1; i < 171; ++i) {
        if (data[i] == 1) presentTracks++;
    }

    // Verify file size matches expected size (with some tolerance)
    size_t expectedSize = X68000_DIM_HEADER + (presentTracks * trackSize);
    if (data.size() >= expectedSize - trackSize && data.size() <= expectedSize + trackSize) {
        return rde::DiskFormat::X68000DIM;
    }

    // If mostly valid track flags and reasonable type, assume DIM
    if (flagCount1 > 50 && flagCountOther == 0 && dimType <= 3) {
        return rde::DiskFormat::X68000DIM;
    }

    return rde::DiskFormat::Unknown;
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

rde::DiskFormat FormatDetector::detectMacDC42Format(const std::vector<uint8_t>& data,
                                                     size_t fileSize) {
    // Per SPEC §285, evaluate the 7 of 8 detection conditions excluding the
    // payload checksum (which requires reading the entire file; detect()'s
    // 64KB read window cannot satisfy it for typical Mac floppies).
    constexpr size_t HEADER = 0x54;
    if (fileSize < HEADER) return rde::DiskFormat::Unknown;
    if (data.size() < HEADER) return rde::DiskFormat::Unknown;

    const uint8_t nameLen = data[0x00];
    if (nameLen > 63) return rde::DiskFormat::Unknown;

    const uint16_t magic = static_cast<uint16_t>(
        (static_cast<uint16_t>(data[0x52]) << 8) | data[0x53]);
    if (magic != 0x0100) return rde::DiskFormat::Unknown;

    const uint32_t dataSize =
        (static_cast<uint32_t>(data[0x40]) << 24) |
        (static_cast<uint32_t>(data[0x41]) << 16) |
        (static_cast<uint32_t>(data[0x42]) << 8)  |
         static_cast<uint32_t>(data[0x43]);
    if (dataSize == 0) return rde::DiskFormat::Unknown;
    if ((dataSize % 2) != 0) return rde::DiskFormat::Unknown;
    if ((dataSize % 512) != 0) return rde::DiskFormat::Unknown;

    const uint32_t tagSize =
        (static_cast<uint32_t>(data[0x44]) << 24) |
        (static_cast<uint32_t>(data[0x45]) << 16) |
        (static_cast<uint32_t>(data[0x46]) << 8)  |
         static_cast<uint32_t>(data[0x47]);

    const size_t expectedFileSize = HEADER + dataSize + tagSize;
    if (fileSize != expectedFileSize) return rde::DiskFormat::Unknown;

    return rde::DiskFormat::MacDC42;
}

rde::DiskFormat FormatDetector::detectMacRawFormat(const std::vector<uint8_t>& data,
                                                     size_t fileSize) {
    // Logical 512B-sector stream, no container. Identified by HFS or MFS
    // signature at offset 0x400 (sector 2). Run as the last fallback so
    // non-Mac raw disks of the same size never reach this branch.
    if ((fileSize % 512) != 0 || fileSize < 0x400 + 2) return rde::DiskFormat::Unknown;
    if (data.size() < 0x400 + 2) return rde::DiskFormat::Unknown;

    const uint16_t sig = static_cast<uint16_t>(
        (static_cast<uint16_t>(data[0x400]) << 8) | data[0x401]);
    if (sig == rde::MacintoshDiskImage::HFS_MDB_SIGNATURE ||
        sig == rde::MacintoshDiskImage::MFS_MDB_SIGNATURE) {
        return rde::DiskFormat::MacIMG;
    }
    return rde::DiskFormat::Unknown;
}

} // namespace rdedisktool
