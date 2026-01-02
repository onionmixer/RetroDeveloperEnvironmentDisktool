#include "rdedisktool/msx/MSXDiskImage.h"
#include <cstring>
#include <algorithm>

namespace rde {

MSXDiskImage::MSXDiskImage() {
    // Default to 720KB geometry
    initGeometry(TRACKS_80, SIDES_2, SECTORS_9);
}

void MSXDiskImage::initGeometry(size_t tracks, size_t sides, size_t sectors) {
    m_geometry.tracks = tracks;
    m_geometry.sides = sides;
    m_geometry.sectorsPerTrack = sectors;
    m_geometry.bytesPerSector = BYTES_PER_SECTOR;
}

void MSXDiskImage::initGeometryFromSize(size_t fileSize) {
    // Determine geometry from file size
    if (fileSize == 163840) {  // 320KB (actually used as 320K)
        initGeometry(TRACKS_40, SIDES_1, SECTORS_8);
    } else if (fileSize == 184320) {  // 360KB
        initGeometry(TRACKS_40, SIDES_1, SECTORS_9);
    } else if (fileSize == 327680) {  // 640KB (2 sides, 40 tracks, 8 sectors)
        initGeometry(TRACKS_40, SIDES_2, SECTORS_8);
    } else if (fileSize == 368640) {  // 720KB (2 sides, 40 tracks, 9 sectors)
        initGeometry(TRACKS_40, SIDES_2, SECTORS_9);
    } else if (fileSize == 655360) {  // 640KB (2 sides, 80 tracks, 8 sectors)
        initGeometry(TRACKS_80, SIDES_2, SECTORS_8);
    } else if (fileSize == 737280) {  // 720KB
        initGeometry(TRACKS_80, SIDES_2, SECTORS_9);
    } else {
        // Default to 720KB if unknown
        initGeometry(TRACKS_80, SIDES_2, SECTORS_9);
    }
}

void MSXDiskImage::setRawData(const std::vector<uint8_t>& data) {
    m_data = data;
    m_modified = true;
    m_fileSystemDetected = false;
}

FileSystemType MSXDiskImage::getFileSystemType() const {
    if (!m_fileSystemDetected) {
        m_cachedFileSystem = detectFileSystem();
        m_fileSystemDetected = true;
    }
    return m_cachedFileSystem;
}

FileSystemType MSXDiskImage::detectFileSystem() const {
    const auto* bootData = getBootSector();
    if (!bootData) {
        return FileSystemType::Unknown;
    }

    const auto* boot = reinterpret_cast<const MSXBootSector*>(bootData);

    // Check for valid boot sector
    // First byte should be JMP instruction (0xEB or 0xE9)
    if (boot->jumpInstruction[0] != 0xEB && boot->jumpInstruction[0] != 0xE9) {
        return FileSystemType::Unknown;
    }

    // Check bytes per sector
    if (boot->bytesPerSector != 512) {
        return FileSystemType::Unknown;
    }

    // Check media descriptor
    if (boot->mediaDescriptor < 0xF0) {
        return FileSystemType::Unknown;
    }

    // Determine FAT type based on cluster count
    uint16_t clusters = getTotalClusters();

    if (clusters < 4085) {
        // Check for MSX-DOS signature in OEM name
        std::string oemName = getOEMName();
        if (oemName.find("MSX") != std::string::npos) {
            if (oemName.find("2") != std::string::npos) {
                return FileSystemType::MSXDOS2;
            }
            return FileSystemType::MSXDOS1;
        }
        return FileSystemType::FAT12;
    } else if (clusters < 65525) {
        return FileSystemType::FAT16;
    }

    return FileSystemType::Unknown;
}

const uint8_t* MSXDiskImage::getBootSector() const {
    if (m_data.size() < BYTES_PER_SECTOR) {
        return nullptr;
    }
    return m_data.data();
}

uint8_t MSXDiskImage::getMediaDescriptor() const {
    const auto* boot = getBootSector();
    if (!boot) return 0;

    return boot[21];  // Offset 0x15 in boot sector
}

std::string MSXDiskImage::getOEMName() const {
    const auto* boot = getBootSector();
    if (!boot) return "";

    return std::string(reinterpret_cast<const char*>(boot + 3), 8);
}

void MSXDiskImage::detectGeometry() {
    const auto* boot = getBootSector();
    if (!boot) {
        initGeometryFromSize(m_data.size());
        return;
    }

    const auto* bpb = reinterpret_cast<const MSXBootSector*>(boot);

    // Validate boot sector before using it
    if (bpb->bytesPerSector == 512 &&
        bpb->sectorsPerTrack > 0 && bpb->sectorsPerTrack <= 18 &&
        bpb->numberOfHeads > 0 && bpb->numberOfHeads <= 2) {

        m_geometry.bytesPerSector = bpb->bytesPerSector;
        m_geometry.sectorsPerTrack = bpb->sectorsPerTrack;
        m_geometry.sides = bpb->numberOfHeads;

        // Calculate tracks from total sectors
        uint32_t totalSectors = bpb->totalSectors16;
        if (totalSectors == 0) {
            totalSectors = bpb->totalSectors32;
        }

        if (totalSectors > 0 && m_geometry.sectorsPerTrack > 0 && m_geometry.sides > 0) {
            m_geometry.tracks = totalSectors / (m_geometry.sectorsPerTrack * m_geometry.sides);
        }
    } else {
        initGeometryFromSize(m_data.size());
    }
}

bool MSXDiskImage::isMSXDOS() const {
    FileSystemType fs = getFileSystemType();
    return fs == FileSystemType::MSXDOS1 || fs == FileSystemType::MSXDOS2;
}

uint8_t MSXDiskImage::getSectorsPerCluster() const {
    const auto* boot = getBootSector();
    if (!boot) return 2;
    return boot[13];  // Offset 0x0D
}

uint16_t MSXDiskImage::getReservedSectors() const {
    const auto* boot = getBootSector();
    if (!boot) return 1;
    return boot[14] | (boot[15] << 8);  // Offset 0x0E-0x0F
}

uint8_t MSXDiskImage::getNumberOfFATs() const {
    const auto* boot = getBootSector();
    if (!boot) return 2;
    return boot[16];  // Offset 0x10
}

uint16_t MSXDiskImage::getRootEntryCount() const {
    const auto* boot = getBootSector();
    if (!boot) return 112;
    return boot[17] | (boot[18] << 8);  // Offset 0x11-0x12
}

uint16_t MSXDiskImage::getSectorsPerFAT() const {
    const auto* boot = getBootSector();
    if (!boot) return 3;
    return boot[22] | (boot[23] << 8);  // Offset 0x16-0x17
}

uint16_t MSXDiskImage::getTotalClusters() const {
    const auto* boot = getBootSector();
    if (!boot) return 0;

    const auto* bpb = reinterpret_cast<const MSXBootSector*>(boot);

    uint32_t totalSectors = bpb->totalSectors16;
    if (totalSectors == 0) {
        totalSectors = bpb->totalSectors32;
    }

    // If BPB doesn't have totalSectors, calculate from disk size
    if (totalSectors == 0) {
        totalSectors = static_cast<uint32_t>(m_data.size() / BYTES_PER_SECTOR);
    }

    size_t firstDataSector = getFirstDataSector();
    if (firstDataSector >= totalSectors) {
        return 0;
    }

    uint32_t dataSectors = totalSectors - static_cast<uint32_t>(firstDataSector);

    return static_cast<uint16_t>(dataSectors / getSectorsPerCluster());
}

size_t MSXDiskImage::getBytesPerCluster() const {
    return static_cast<size_t>(getSectorsPerCluster()) * BYTES_PER_SECTOR;
}

size_t MSXDiskImage::getFirstDataSector() const {
    // First data sector = reserved + (numFATs * sectorsPerFAT) + rootDirSectors
    size_t rootDirSectors = (getRootEntryCount() * 32 + BYTES_PER_SECTOR - 1) /
                            BYTES_PER_SECTOR;

    return getReservedSectors() +
           (getNumberOfFATs() * getSectorsPerFAT()) +
           rootDirSectors;
}

size_t MSXDiskImage::calculateOffset(size_t track, size_t side, size_t sector) const {
    // MSX uses linear sector layout: track-side-sector
    // Sector numbers are typically 1-based on MSX, but we use 0-based internally
    size_t linearSector = (track * m_geometry.sides + side) * m_geometry.sectorsPerTrack +
                          sector;
    return linearSector * BYTES_PER_SECTOR;
}

} // namespace rde
