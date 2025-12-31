/**
 * FileSystemHandler Factory Implementation
 *
 * Creates appropriate file system handlers based on disk type.
 */

#include "rdedisktool/FileSystemHandler.h"
#include "rdedisktool/filesystem/MSXDOSHandler.h"
#include "rdedisktool/filesystem/AppleDOS33Handler.h"
#include "rdedisktool/filesystem/AppleProDOSHandler.h"
#include "rdedisktool/DiskImage.h"

namespace rde {

std::unique_ptr<FileSystemHandler> FileSystemHandler::create(DiskImage* disk) {
    if (!disk) {
        return nullptr;
    }

    // Determine file system type from disk format
    DiskFormat format = disk->getFormat();

    // MSX disk formats
    if (format == DiskFormat::MSXDSK || format == DiskFormat::MSXDMK) {
        auto handler = std::make_unique<MSXDOSHandler>();
        if (handler->initialize(disk)) {
            return handler;
        }
        return nullptr;
    }

    // Apple disk formats
    if (format == DiskFormat::AppleDO || format == DiskFormat::ApplePO ||
        format == DiskFormat::AppleNIB || format == DiskFormat::AppleWOZ1 ||
        format == DiskFormat::AppleWOZ2) {
        // Detect file system type from disk content
        FileSystemType fsType = disk->getFileSystemType();

        if (fsType == FileSystemType::ProDOS) {
            auto prodos = std::make_unique<AppleProDOSHandler>();
            if (prodos->initialize(disk)) {
                return prodos;
            }
        }

        // Try DOS 3.3 as fallback
        auto dos33 = std::make_unique<AppleDOS33Handler>();
        if (dos33->initialize(disk)) {
            return dos33;
        }
    }

    return nullptr;
}

std::unique_ptr<FileSystemHandler> FileSystemHandler::createForType(FileSystemType type) {
    switch (type) {
        case FileSystemType::MSXDOS1:
        case FileSystemType::MSXDOS2:
        case FileSystemType::FAT12:
            return std::make_unique<MSXDOSHandler>();

        case FileSystemType::DOS33:
            return std::make_unique<AppleDOS33Handler>();

        case FileSystemType::ProDOS:
            return std::make_unique<AppleProDOSHandler>();

        default:
            return nullptr;
    }
}

} // namespace rde
