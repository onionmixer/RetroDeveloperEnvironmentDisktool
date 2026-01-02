#ifndef RDEDISKTOOL_FORMAT_DETECTOR_H
#define RDEDISKTOOL_FORMAT_DETECTOR_H

#include "rdedisktool/Types.h"
#include <string>
#include <vector>
#include <filesystem>

namespace rdedisktool {

/**
 * Centralized disk format detection
 *
 * Detection order:
 * 1. Magic number detection (WOZ, DMK, XSA)
 * 2. Extension-based detection (unambiguous extensions)
 * 3. Size + extension detection (ambiguous .dsk)
 * 4. Content-based detection (boot sector analysis)
 */
class FormatDetector {
public:
    //=========================================================================
    // Main Detection Entry Points
    //=========================================================================

    /**
     * Detect disk format from file path
     * Reads file and performs all detection stages
     * @param path Path to disk image file
     * @return Detected format or DiskFormat::Unknown
     * @throws FileNotFoundException if file doesn't exist
     * @throws ReadException if file can't be read
     */
    static rde::DiskFormat detect(const std::filesystem::path& path);

    /**
     * Detect disk format from data and extension
     * @param data File content (at least first 64KB)
     * @param extension File extension (lowercase, with dot)
     * @return Detected format or DiskFormat::Unknown
     */
    static rde::DiskFormat detect(const std::vector<uint8_t>& data,
                                   const std::string& extension);

    //=========================================================================
    // Individual Detection Stages (for testing and specific needs)
    //=========================================================================

    /**
     * Detect by magic number (WOZ, XSA, etc.)
     * @param data File content
     * @return Detected format or DiskFormat::Unknown
     */
    static rde::DiskFormat detectByMagic(const std::vector<uint8_t>& data);

    /**
     * Detect by extension and size for unambiguous formats
     * @param ext File extension (lowercase)
     * @param fileSize File size in bytes
     * @return Detected format or DiskFormat::Unknown
     */
    static rde::DiskFormat detectByExtension(const std::string& ext, size_t fileSize);

    /**
     * Detect by analyzing file content (boot sector, etc.)
     * @param data File content
     * @param ext File extension (lowercase)
     * @param fileSize File size in bytes
     * @return Detected format or DiskFormat::Unknown
     */
    static rde::DiskFormat detectByContent(const std::vector<uint8_t>& data,
                                            const std::string& ext,
                                            size_t fileSize);

    //=========================================================================
    // Format-Specific Detection
    //=========================================================================

    /**
     * Detect WOZ format (WOZ1 or WOZ2)
     * @param data File content
     * @return AppleWOZ1, AppleWOZ2, or Unknown
     */
    static rde::DiskFormat detectWOZFormat(const std::vector<uint8_t>& data);

    /**
     * Detect DMK format
     * @param data File content
     * @return MSXDMK or Unknown
     */
    static rde::DiskFormat detectDMKFormat(const std::vector<uint8_t>& data);

    /**
     * Detect XSA format
     * @param data File content
     * @return MSXXSA or Unknown
     */
    static rde::DiskFormat detectXSAFormat(const std::vector<uint8_t>& data);

    /**
     * Detect Apple format (DO or PO)
     * @param data File content
     * @param ext File extension
     * @return AppleDO, ApplePO, or Unknown
     */
    static rde::DiskFormat detectAppleFormat(const std::vector<uint8_t>& data,
                                              const std::string& ext);

    /**
     * Detect MSX format
     * @param data File content
     * @param ext File extension
     * @return MSXDSK or Unknown
     */
    static rde::DiskFormat detectMSXFormat(const std::vector<uint8_t>& data,
                                            const std::string& ext);

    //=========================================================================
    // Utility Methods
    //=========================================================================

    /**
     * Get file extension (lowercase, with leading dot)
     * @param path File path
     * @return Extension like ".dsk" or empty string
     */
    static std::string getExtension(const std::filesystem::path& path);

    /**
     * Convert string to lowercase
     * @param str Input string
     * @return Lowercase string
     */
    static std::string toLower(const std::string& str);

    //=========================================================================
    // Validation Helpers
    //=========================================================================

    /**
     * Check if data contains valid MSX BPB (BIOS Parameter Block)
     * @param data File content
     * @return true if valid FAT12 BPB
     */
    static bool isValidMSXBPB(const std::vector<uint8_t>& data);

    /**
     * Check if data contains valid DOS 3.3 VTOC
     * @param data File content
     * @param fileSize File size
     * @return true if valid VTOC found
     */
    static bool isValidDOS33VTOC(const std::vector<uint8_t>& data, size_t fileSize);

    /**
     * Check if data contains valid ProDOS volume header
     * @param data File content
     * @param fileSize File size
     * @return true if valid ProDOS header found
     */
    static bool isValidProDOSHeader(const std::vector<uint8_t>& data, size_t fileSize);

private:
    // Standard disk sizes for quick identification
    static constexpr size_t APPLE_140K = 143360;     // 35*16*256
    static constexpr size_t APPLE_DOS32 = 116480;    // 35*13*256
    static constexpr size_t APPLE_NIB = 232960;      // 35*6656
    static constexpr size_t APPLE_NIB2 = 223440;     // 35*6384

    static constexpr size_t MSX_320K = 163840;       // 1S 40T 8S
    static constexpr size_t MSX_360K = 184320;       // 1S 40T 9S
    static constexpr size_t MSX_640K = 327680;       // 2S 40T 8S
    static constexpr size_t MSX_720K_40 = 368640;    // 2S 40T 9S
    static constexpr size_t MSX_720K_80 = 737280;    // 2S 80T 9S

    /**
     * Check if file size matches known MSX disk sizes
     */
    static bool isMSXDiskSize(size_t size);

    /**
     * Detect DSK format by analyzing boot sector content
     */
    static rde::DiskFormat detectDSKByContent(const std::vector<uint8_t>& data);
};

} // namespace rdedisktool

#endif // RDEDISKTOOL_FORMAT_DETECTOR_H
