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

    // Check for Human68k file system
    // Human68k uses a specific boot sector format
    if (m_data.size() >= 1024) {
        // Check for X68000 IPL signature or Human68k boot sector
        // Human68k boot sector typically starts with JMP instruction (0x60)
        if (m_data[0] == 0x60) {
            m_cachedFileSystem = FileSystemType::Human68k;
        }
        // Also check for "X68IPL" signature
        else if (m_data.size() >= 10 &&
                 m_data[3] == 'X' && m_data[4] == '6' && m_data[5] == '8' &&
                 m_data[6] == 'I' && m_data[7] == 'P' && m_data[8] == 'L') {
            m_cachedFileSystem = FileSystemType::Human68k;
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
