#ifndef RDEDISKTOOL_DISKIMAGE_H
#define RDEDISKTOOL_DISKIMAGE_H

#include "rdedisktool/Types.h"
#include "rdedisktool/Exceptions.h"
#include <string>
#include <vector>
#include <memory>
#include <filesystem>

namespace rde {

/**
 * Abstract base class for all disk image formats
 *
 * This class provides a unified interface for reading, writing, and manipulating
 * disk images from various platforms (Apple II, MSX).
 */
class DiskImage {
public:
    virtual ~DiskImage() = default;

    // Prevent copying, allow moving
    DiskImage(const DiskImage&) = delete;
    DiskImage& operator=(const DiskImage&) = delete;
    DiskImage(DiskImage&&) = default;
    DiskImage& operator=(DiskImage&&) = default;

    //=========================================================================
    // Factory and I/O Methods
    //=========================================================================

    /**
     * Load a disk image from file
     * @param path Path to the disk image file
     * @throws InvalidFormatException if the file format is invalid
     * @throws ReadException if the file cannot be read
     */
    virtual void load(const std::filesystem::path& path) = 0;

    /**
     * Save the disk image to file
     * @param path Path to save the disk image (empty = use original path)
     * @throws WriteException if the file cannot be written
     * @throws WriteProtectedException if the image is write-protected
     */
    virtual void save(const std::filesystem::path& path = {}) = 0;

    /**
     * Create a new blank disk image with the specified geometry
     * @param geometry Disk geometry parameters
     */
    virtual void create(const DiskGeometry& geometry) = 0;

    //=========================================================================
    // Image Information
    //=========================================================================

    /**
     * Get the platform this disk image is for
     */
    virtual Platform getPlatform() const = 0;

    /**
     * Get the disk format type
     */
    virtual DiskFormat getFormat() const = 0;

    /**
     * Get the detected or configured file system type
     */
    virtual FileSystemType getFileSystemType() const = 0;

    /**
     * Get the disk geometry
     */
    virtual DiskGeometry getGeometry() const = 0;

    /**
     * Check if the image is write-protected
     */
    virtual bool isWriteProtected() const = 0;

    /**
     * Set write protection status
     */
    virtual void setWriteProtected(bool protect) = 0;

    /**
     * Check if the image has been modified since loading
     */
    virtual bool isModified() const = 0;

    /**
     * Get the file path of the loaded image
     */
    virtual std::filesystem::path getFilePath() const = 0;

    //=========================================================================
    // Low-Level Sector Access
    //=========================================================================

    /**
     * Read a sector from the disk image
     * @param track Track number (0-based)
     * @param side Side number (0-based, 0 for single-sided)
     * @param sector Sector number (0-based or 1-based depending on format)
     * @return Sector data
     * @throws SectorNotFoundException if the sector doesn't exist
     * @throws CRCException if CRC verification fails
     */
    virtual SectorBuffer readSector(size_t track, size_t side, size_t sector) = 0;

    /**
     * Write a sector to the disk image
     * @param track Track number
     * @param side Side number
     * @param sector Sector number
     * @param data Sector data to write
     * @throws SectorNotFoundException if the sector doesn't exist
     * @throws WriteProtectedException if the image is write-protected
     */
    virtual void writeSector(size_t track, size_t side, size_t sector,
                            const SectorBuffer& data) = 0;

    /**
     * Read an entire track (raw data)
     * @param track Track number
     * @param side Side number
     * @return Raw track data
     */
    virtual TrackBuffer readTrack(size_t track, size_t side) = 0;

    /**
     * Write an entire track (raw data)
     * @param track Track number
     * @param side Side number
     * @param data Raw track data to write
     */
    virtual void writeTrack(size_t track, size_t side, const TrackBuffer& data) = 0;

    //=========================================================================
    // Logical Block Access (for sector-based formats)
    //=========================================================================

    /**
     * Read a logical block (ProDOS style block addressing)
     * @param blockNumber Block number (0-based)
     * @return Block data (typically 512 bytes)
     */
    virtual SectorBuffer readBlock(size_t blockNumber);

    /**
     * Write a logical block
     * @param blockNumber Block number
     * @param data Block data to write
     */
    virtual void writeBlock(size_t blockNumber, const SectorBuffer& data);

    /**
     * Get total number of blocks
     */
    virtual size_t getTotalBlocks() const;

    //=========================================================================
    // Raw Image Data Access
    //=========================================================================

    /**
     * Get raw image data (for debugging or direct manipulation)
     */
    virtual const std::vector<uint8_t>& getRawData() const = 0;

    /**
     * Set raw image data
     */
    virtual void setRawData(const std::vector<uint8_t>& data) = 0;

    //=========================================================================
    // Format Conversion
    //=========================================================================

    /**
     * Check if conversion to the specified format is supported
     */
    virtual bool canConvertTo(DiskFormat format) const = 0;

    /**
     * Convert to another disk format
     * @param format Target format
     * @return New disk image in the target format
     * @throws UnsupportedFormatException if conversion is not supported
     */
    virtual std::unique_ptr<DiskImage> convertTo(DiskFormat format) const = 0;

    //=========================================================================
    // Validation and Diagnostics
    //=========================================================================

    /**
     * Validate the disk image structure
     * @return true if the image is valid
     */
    virtual bool validate() const = 0;

    /**
     * Get detailed diagnostic information about the image
     */
    virtual std::string getDiagnostics() const = 0;

protected:
    DiskImage() = default;

    // Common state
    std::filesystem::path m_filePath;
    std::vector<uint8_t> m_data;
    DiskGeometry m_geometry;
    bool m_writeProtected = false;
    bool m_modified = false;
};

} // namespace rde

#endif // RDEDISKTOOL_DISKIMAGE_H
