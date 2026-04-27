/**
 * FileSystemHandler Factory Implementation
 *
 * Creates appropriate file system handlers based on disk type.
 */

#include "rdedisktool/FileSystemHandler.h"
#include "rdedisktool/filesystem/MSXDOSHandler.h"
#include "rdedisktool/filesystem/AppleDOS33Handler.h"
#include "rdedisktool/filesystem/AppleProDOSHandler.h"
#include "rdedisktool/filesystem/x68000/Human68kHandler.h"
#include "rdedisktool/filesystem/MacintoshHFSHandler.h"
#include "rdedisktool/filesystem/MacintoshMFSHandler.h"
#include "rdedisktool/DiskImage.h"

namespace rde {

std::unique_ptr<FileSystemHandler> FileSystemHandler::create(DiskImage* disk) {
    if (!disk) {
        return nullptr;
    }

    // Determine file system type from disk format
    DiskFormat format = disk->getFormat();

    // MSX disk formats
    if (format == DiskFormat::MSXDSK || format == DiskFormat::MSXDMK ||
        format == DiskFormat::MSXXSA) {
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

    // X68000 disk formats
    if (format == DiskFormat::X68000XDF || format == DiskFormat::X68000DIM) {
        auto handler = std::make_unique<Human68kHandler>();
        if (handler->initialize(disk)) {
            return handler;
        }
        return nullptr;
    }

    // Macintosh disk formats. The actual filesystem (HFS or MFS) is decided by
    // the MDB signature at sector 2 (offset 0x400) — see MacintoshDiskImage.
    if (format == DiskFormat::MacIMG || format == DiskFormat::MacDC42) {
        const FileSystemType fs = disk->getFileSystemType();
        if (fs == FileSystemType::HFS) {
            auto h = std::make_unique<MacintoshHFSHandler>();
            if (h->initialize(disk)) return h;
        }
        if (fs == FileSystemType::MFS) {
            auto m = std::make_unique<MacintoshMFSHandler>();
            if (m->initialize(disk)) return m;
        }
        return nullptr;
    }

    return nullptr;
}

ValidationResult FileSystemHandler::validateExtended() const {
    ValidationResult result;
    result.addInfo("Basic validation passed (no extended validation available for this filesystem)");
    return result;
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

        case FileSystemType::Human68k:
            return std::make_unique<Human68kHandler>();

        case FileSystemType::HFS:
            return std::make_unique<MacintoshHFSHandler>();
        case FileSystemType::MFS:
            return std::make_unique<MacintoshMFSHandler>();

        case FileSystemType::Unknown:
        case FileSystemType::FAT16:
            return nullptr;
    }
    return nullptr;
}

} // namespace rde
