#ifndef RDEDISKTOOL_DISKIMAGEFACTORY_H
#define RDEDISKTOOL_DISKIMAGEFACTORY_H

#include "rdedisktool/DiskImage.h"
#include "rdedisktool/Types.h"
#include <memory>
#include <filesystem>
#include <functional>
#include <map>
#include <string>

namespace rde {

/**
 * Factory class for creating and detecting disk image formats
 *
 * This class provides static methods for:
 * - Detecting disk image format from file content and extension
 * - Creating appropriate DiskImage subclass instances
 * - Creating new blank disk images
 */
class DiskImageFactory {
public:
    // Type alias for creator functions
    using CreatorFunc = std::function<std::unique_ptr<DiskImage>()>;

    //=========================================================================
    // Format Detection
    //=========================================================================

    /**
     * Detect the disk format from a file
     * @param path Path to the disk image file
     * @return Detected disk format
     */
    static DiskFormat detectFormat(const std::filesystem::path& path);

    /**
     * Detect the disk format from raw data
     * @param data Raw file data
     * @param extension File extension (for disambiguation)
     * @return Detected disk format
     */
    static DiskFormat detectFormat(const std::vector<uint8_t>& data,
                                   const std::string& extension = "");

    /**
     * Get the platform for a given disk format
     */
    static Platform getPlatformForFormat(DiskFormat format);

    //=========================================================================
    // Image Creation
    //=========================================================================

    /**
     * Open an existing disk image file
     * @param path Path to the disk image file
     * @param format Optional format hint (Unknown = auto-detect)
     * @return Pointer to the disk image
     * @throws FileNotFoundException if the file doesn't exist
     * @throws InvalidFormatException if the format is unrecognized
     * @throws UnsupportedFormatException if the format is not yet implemented
     */
    static std::unique_ptr<DiskImage> open(const std::filesystem::path& path,
                                           DiskFormat format = DiskFormat::Unknown);

    /**
     * Create a new blank disk image
     * @param format Target disk format
     * @param geometry Disk geometry (use defaults if empty)
     * @return Pointer to the new disk image
     */
    static std::unique_ptr<DiskImage> create(DiskFormat format,
                                             const DiskGeometry& geometry = {});

    /**
     * Create a new blank disk image with standard geometry for format
     * @param format Target disk format
     * @return Pointer to the new disk image
     */
    static std::unique_ptr<DiskImage> createWithDefaultGeometry(DiskFormat format);

    //=========================================================================
    // Format Information
    //=========================================================================

    /**
     * Get default geometry for a disk format
     */
    static DiskGeometry getDefaultGeometry(DiskFormat format);

    /**
     * Get supported file extensions for a format
     */
    static std::vector<std::string> getExtensions(DiskFormat format);

    /**
     * Get all supported disk formats
     */
    static std::vector<DiskFormat> getSupportedFormats();

    /**
     * Get supported formats for a platform
     */
    static std::vector<DiskFormat> getFormatsForPlatform(Platform platform);

    /**
     * Check if a format is supported
     */
    static bool isFormatSupported(DiskFormat format);

    /**
     * Get format from file extension
     */
    static DiskFormat getFormatFromExtension(const std::string& extension);

    //=========================================================================
    // Format Registration (for extensibility)
    //=========================================================================

    /**
     * Register a new format handler
     * @param format The disk format to register
     * @param creator Function that creates instances of the handler
     */
    static void registerFormat(DiskFormat format, CreatorFunc creator);

    /**
     * Unregister a format handler
     */
    static void unregisterFormat(DiskFormat format);

private:
    // Private constructor - all methods are static
    DiskImageFactory() = default;

    // Format detection helpers
    static DiskFormat detectAppleFormat(const std::vector<uint8_t>& data,
                                        const std::string& extension);
    static DiskFormat detectMSXFormat(const std::vector<uint8_t>& data,
                                      const std::string& extension);
    static DiskFormat detectWOZFormat(const std::vector<uint8_t>& data);
    static DiskFormat detectDMKFormat(const std::vector<uint8_t>& data);
    static DiskFormat detectXSAFormat(const std::vector<uint8_t>& data);

    // Registered format creators
    static std::map<DiskFormat, CreatorFunc>& getCreators();
};

} // namespace rde

#endif // RDEDISKTOOL_DISKIMAGEFACTORY_H
