#include "rdedisktool/DiskImage.h"

namespace rde {

SectorBuffer DiskImage::readBlock(size_t blockNumber) {
    // Default implementation: convert block number to track/sector
    // This works for 512-byte block formats
    // Subclasses should override for platform-specific implementations

    const auto& geom = getGeometry();

    if (geom.bytesPerSector == 0 || geom.sectorsPerTrack == 0) {
        throw InvalidFormatException("Invalid disk geometry for block access");
    }

    // Calculate sectors per block (typically 2 for 256-byte sectors, 1 for 512-byte)
    size_t sectorsPerBlock = 512 / geom.bytesPerSector;
    if (sectorsPerBlock == 0) sectorsPerBlock = 1;

    size_t totalSectorsPerTrack = geom.sectorsPerTrack * geom.sides;
    size_t startSector = blockNumber * sectorsPerBlock;

    size_t track = startSector / totalSectorsPerTrack;
    size_t sectorInTrack = startSector % totalSectorsPerTrack;
    size_t side = sectorInTrack / geom.sectorsPerTrack;
    size_t sector = sectorInTrack % geom.sectorsPerTrack;

    if (track >= geom.tracks) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }

    SectorBuffer result;
    result.reserve(512);

    for (size_t i = 0; i < sectorsPerBlock; ++i) {
        auto sectorData = readSector(track, side, sector + i);
        result.insert(result.end(), sectorData.begin(), sectorData.end());
    }

    return result;
}

void DiskImage::writeBlock(size_t blockNumber, const SectorBuffer& data) {
    const auto& geom = getGeometry();

    if (geom.bytesPerSector == 0 || geom.sectorsPerTrack == 0) {
        throw InvalidFormatException("Invalid disk geometry for block access");
    }

    size_t sectorsPerBlock = 512 / geom.bytesPerSector;
    if (sectorsPerBlock == 0) sectorsPerBlock = 1;

    size_t totalSectorsPerTrack = geom.sectorsPerTrack * geom.sides;
    size_t startSector = blockNumber * sectorsPerBlock;

    size_t track = startSector / totalSectorsPerTrack;
    size_t sectorInTrack = startSector % totalSectorsPerTrack;
    size_t side = sectorInTrack / geom.sectorsPerTrack;
    size_t sector = sectorInTrack % geom.sectorsPerTrack;

    if (track >= geom.tracks) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }

    for (size_t i = 0; i < sectorsPerBlock; ++i) {
        size_t offset = i * geom.bytesPerSector;
        SectorBuffer sectorData(
            data.begin() + offset,
            data.begin() + std::min(offset + geom.bytesPerSector, data.size())
        );

        // Pad to full sector size if necessary
        if (sectorData.size() < geom.bytesPerSector) {
            sectorData.resize(geom.bytesPerSector, 0);
        }

        writeSector(track, side, sector + i, sectorData);
    }
}

size_t DiskImage::getTotalBlocks() const {
    const auto& geom = getGeometry();

    if (geom.bytesPerSector == 0) {
        return 0;
    }

    size_t totalBytes = geom.totalSize();
    return totalBytes / 512;  // Standard 512-byte blocks
}

} // namespace rde
