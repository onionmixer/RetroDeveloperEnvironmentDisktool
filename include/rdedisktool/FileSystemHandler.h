#ifndef RDEDISKTOOL_FILESYSTEMHANDLER_H
#define RDEDISKTOOL_FILESYSTEMHANDLER_H

#include "rdedisktool/Types.h"
#include "rdedisktool/DiskImage.h"
#include <vector>
#include <string>
#include <memory>

namespace rde {

/**
 * Abstract base class for file system handlers
 *
 * Provides a unified interface for reading and writing files
 * to different disk image file systems (DOS 3.3, ProDOS, MSX-DOS, etc.)
 */
class FileSystemHandler {
public:
    virtual ~FileSystemHandler() = default;

    /**
     * Get the file system type this handler manages
     */
    virtual FileSystemType getType() const = 0;

    /**
     * Initialize the handler with a disk image
     * @return true if initialization succeeded
     */
    virtual bool initialize(DiskImage* disk) = 0;

    /**
     * List files in a directory
     * @param path Directory path (empty = root)
     * @return Vector of file entries
     */
    virtual std::vector<FileEntry> listFiles(const std::string& path = "") = 0;

    /**
     * Read a file from the disk
     * @param filename File name or path
     * @return File contents
     */
    virtual std::vector<uint8_t> readFile(const std::string& filename) = 0;

    /**
     * Write a file to the disk
     * @param filename File name or path
     * @param data File contents
     * @param metadata Optional file metadata
     * @return true on success
     */
    virtual bool writeFile(const std::string& filename,
                          const std::vector<uint8_t>& data,
                          const FileMetadata& metadata = {}) = 0;

    /**
     * Delete a file from the disk
     * @param filename File name or path
     * @return true on success
     */
    virtual bool deleteFile(const std::string& filename) = 0;

    /**
     * Rename a file
     * @param oldName Current file name
     * @param newName New file name
     * @return true on success
     */
    virtual bool renameFile(const std::string& oldName,
                           const std::string& newName) = 0;

    /**
     * Get free space on disk in bytes
     */
    virtual size_t getFreeSpace() const = 0;

    /**
     * Get total usable space on disk in bytes
     */
    virtual size_t getTotalSpace() const = 0;

    /**
     * Check if a file exists
     */
    virtual bool fileExists(const std::string& filename) const = 0;

    /**
     * Format the disk with this file system
     * @param volumeName Optional volume name
     * @return true on success
     */
    virtual bool format(const std::string& volumeName = "") = 0;

    /**
     * Set the disk image without parsing (for formatting blank disks)
     * @param disk Disk image to associate with this handler
     */
    void setDisk(DiskImage* disk) { m_disk = disk; }

    /**
     * Get the volume name/label
     */
    virtual std::string getVolumeName() const = 0;

    /**
     * Perform extended file system validation
     * @return ValidationResult containing all issues found
     */
    virtual ValidationResult validateExtended() const;

    //=========================================================================
    // Directory Operations (optional - may not be supported by all file systems)
    //=========================================================================

    /**
     * Check if this file system supports directories
     * @return true if directories are supported
     */
    virtual bool supportsDirectories() const { return false; }

    /**
     * Create a directory
     * @param path Directory path
     * @return true on success, false if not supported or failed
     */
    virtual bool createDirectory(const std::string& path) {
        (void)path;
        return false;
    }

    /**
     * Delete a directory
     * @param path Directory path
     * @return true on success, false if not supported, failed, or not empty
     */
    virtual bool deleteDirectory(const std::string& path) {
        (void)path;
        return false;
    }

    /**
     * Check if a path is a directory
     * @param path Path to check
     * @return true if path is a directory
     */
    virtual bool isDirectory(const std::string& path) const {
        (void)path;
        return false;
    }

    /**
     * Create appropriate handler for a disk's file system
     * @param disk Disk image to create handler for
     * @return Handler instance or nullptr if unsupported
     */
    static std::unique_ptr<FileSystemHandler> create(DiskImage* disk);

    /**
     * Create handler for a specific file system type
     * @param type File system type
     * @return Handler instance or nullptr if unsupported
     */
    static std::unique_ptr<FileSystemHandler> createForType(FileSystemType type);

protected:
    DiskImage* m_disk = nullptr;
};

} // namespace rde

#endif // RDEDISKTOOL_FILESYSTEMHANDLER_H
