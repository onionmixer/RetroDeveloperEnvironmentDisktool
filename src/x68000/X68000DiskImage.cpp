#include "rdedisktool/x68000/X68000DiskImage.h"
#include <cstring>

namespace rde {

X68000DiskImage::X68000DiskImage() {
    // Default geometry for XDF format
    m_geometry.tracks = XDF_TOTAL_TRACKS;
    m_geometry.sides = XDF_HEADS;
    m_geometry.sectorsPerTrack = XDF_SECTORS_PER_TRACK;
    m_geometry.bytesPerSector = XDF_SECTOR_SIZE;
}

FileSystemType X68000DiskImage::getFileSystemType() const {
    if (m_fileSystemDetected) {
        return m_cachedFileSystem;
    }

    m_cachedFileSystem = FileSystemType::Unknown;

    // Check for Human68k file system.
    // Accept both legacy IPL signatures and FAT-like BPB layouts used by
    // newly formatted Human68k disks.
    if (m_data.size() >= 1024) {
        // Legacy/bootable patterns.
        if (m_data[0] == 0x60) {
            m_cachedFileSystem = FileSystemType::Human68k;
        } else if (m_data.size() >= 10 &&
                 m_data[3] == 'X' && m_data[4] == '6' && m_data[5] == '8' &&
                 m_data[6] == 'I' && m_data[7] == 'P' && m_data[8] == 'L') {
            m_cachedFileSystem = FileSystemType::Human68k;
        } else if (m_data.size() >= 32) {
            // Human68k formatter used by this project writes:
            // - JMP EB xx 90
            // - OEM "HUMAN68K" at offset 3
            // - FAT-like BPB at 0x0B
            const bool hasJump = (m_data[0] == 0xEB && m_data[2] == 0x90) || (m_data[0] == 0xE9);
            const bool hasHumanOem =
                m_data[3] == 'H' && m_data[4] == 'U' && m_data[5] == 'M' && m_data[6] == 'A' &&
                m_data[7] == 'N' && m_data[8] == '6' && m_data[9] == '8' && m_data[10] == 'K';

            const uint16_t bytesPerSector = static_cast<uint16_t>(m_data[0x0B] | (m_data[0x0C] << 8));
            const uint8_t sectorsPerCluster = m_data[0x0D];
            const uint16_t reservedSectors = static_cast<uint16_t>(m_data[0x0E] | (m_data[0x0F] << 8));
            const uint8_t numberOfFATs = m_data[0x10];
            const uint16_t rootEntries = static_cast<uint16_t>(m_data[0x11] | (m_data[0x12] << 8));
            const uint16_t totalSectors = static_cast<uint16_t>(m_data[0x13] | (m_data[0x14] << 8));
            const uint16_t sectorsPerFAT = static_cast<uint16_t>(m_data[0x16] | (m_data[0x17] << 8));
            const uint16_t sectorsPerTrack = static_cast<uint16_t>(m_data[0x18] | (m_data[0x19] << 8));
            const uint16_t numberOfHeads = static_cast<uint16_t>(m_data[0x1A] | (m_data[0x1B] << 8));

            const bool bpbLooksValid =
                (bytesPerSector == 256 || bytesPerSector == 512 ||
                 bytesPerSector == 1024 || bytesPerSector == 2048) &&
                sectorsPerCluster > 0 &&
                reservedSectors > 0 &&
                numberOfFATs >= 1 &&
                rootEntries > 0 &&
                totalSectors > 0 &&
                sectorsPerFAT > 0 &&
                sectorsPerTrack > 0 &&
                numberOfHeads > 0;

            if (hasJump && hasHumanOem && bpbLooksValid) {
                m_cachedFileSystem = FileSystemType::Human68k;
            }
        }
    }

    m_fileSystemDetected = true;
    return m_cachedFileSystem;
}

void X68000DiskImage::setRawData(const std::vector<uint8_t>& data) {
    m_data = data;
    m_modified = true;
    m_fileSystemDetected = false;
}

bool X68000DiskImage::isHuman68k() const {
    return getFileSystemType() == FileSystemType::Human68k;
}

void X68000DiskImage::initGeometry(size_t tracks, size_t sides, size_t sectorsPerTrack, size_t bytesPerSector) {
    m_geometry.tracks = tracks;
    m_geometry.sides = sides;
    m_geometry.sectorsPerTrack = sectorsPerTrack;
    m_geometry.bytesPerSector = bytesPerSector;
}

size_t X68000DiskImage::calculateOffset(size_t track, size_t sector) const {
    // Default calculation for sector-based formats
    // sector is 1-indexed in X68000
    return (track * m_geometry.sectorsPerTrack + (sector - 1)) * m_geometry.bytesPerSector;
}

} // namespace rde
