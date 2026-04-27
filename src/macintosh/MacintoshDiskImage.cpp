#include "rdedisktool/macintosh/MacintoshDiskImage.h"
#include "rdedisktool/Exceptions.h"

namespace rde {

MacintoshDiskImage::MacintoshDiskImage() {
    m_geometry.tracks = 0;
    m_geometry.sides = 0;
    m_geometry.sectorsPerTrack = 0;
    m_geometry.bytesPerSector = SECTOR_SIZE;
}

void MacintoshDiskImage::setRawData(const std::vector<uint8_t>& data) {
    m_data = data;
    m_modified = true;
    m_fileSystemDetected = false;
}

FileSystemType MacintoshDiskImage::getFileSystemType() const {
    if (m_fileSystemDetected) {
        return m_cachedFileSystem;
    }
    m_fileSystemDetected = true;
    m_cachedFileSystem = FileSystemType::Unknown;

    // MDB lives at file offset 0x400 (sector 2 in a 512B layout).
    if (m_data.size() < 0x400 + 2) {
        return m_cachedFileSystem;
    }
    const uint16_t sig = (static_cast<uint16_t>(m_data[0x400]) << 8) |
                          static_cast<uint16_t>(m_data[0x401]);
    if (sig == HFS_MDB_SIGNATURE) {
        m_cachedFileSystem = FileSystemType::HFS;
    } else if (sig == MFS_MDB_SIGNATURE) {
        m_cachedFileSystem = FileSystemType::MFS;
    }
    return m_cachedFileSystem;
}

void MacintoshDiskImage::initGeometryFromSize(size_t totalBytes) {
    // Logical 512B-sector grid. Track/side layout is informational only —
    // raw IMG / DC42 do not preserve physical track structure for GCR media.
    m_geometry.bytesPerSector = SECTOR_SIZE;
    if (totalBytes == 0 || (totalBytes % SECTOR_SIZE) != 0) {
        m_geometry.tracks = 0;
        m_geometry.sides = 0;
        m_geometry.sectorsPerTrack = 0;
        return;
    }
    const size_t totalSectors = totalBytes / SECTOR_SIZE;
    if (totalBytes == SIZE_400K) {
        m_geometry.tracks = 80;
        m_geometry.sides = 1;
        m_geometry.sectorsPerTrack = 10;
    } else if (totalBytes == SIZE_800K) {
        m_geometry.tracks = 80;
        m_geometry.sides = 2;
        m_geometry.sectorsPerTrack = 10;  // Variable in physical GCR; logical avg.
    } else if (totalBytes == SIZE_720K) {
        m_geometry.tracks = 80;
        m_geometry.sides = 2;
        m_geometry.sectorsPerTrack = 9;
    } else if (totalBytes == SIZE_1440K) {
        m_geometry.tracks = 80;
        m_geometry.sides = 2;
        m_geometry.sectorsPerTrack = 18;
    } else {
        // Non-standard size: collapse into a single linear track.
        m_geometry.tracks = 1;
        m_geometry.sides = 1;
        m_geometry.sectorsPerTrack = totalSectors;
    }
}

} // namespace rde
