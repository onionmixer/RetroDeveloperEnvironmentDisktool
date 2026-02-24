#include "rdedisktool/apple/AppleDiskImage.h"
#include <algorithm>

namespace rde {

AppleDiskImage::AppleDiskImage() {
    initGeometry();
}

void AppleDiskImage::initGeometry(size_t tracks, size_t sectors) {
    m_geometry.tracks = tracks;
    m_geometry.sides = 1;  // Apple II floppies are single-sided
    m_geometry.sectorsPerTrack = sectors;
    m_geometry.bytesPerSector = BYTES_PER_SECTOR;
}

void AppleDiskImage::setRawData(const std::vector<uint8_t>& data) {
    m_data = data;
    m_modified = true;
    m_fileSystemDetected = false;
}

FileSystemType AppleDiskImage::getFileSystemType() const {
    if (!m_fileSystemDetected) {
        m_cachedFileSystem = detectFileSystem();
        m_fileSystemDetected = true;
    }
    return m_cachedFileSystem;
}

FileSystemType AppleDiskImage::detectFileSystem() const {
    if (m_data.size() < DISK_SIZE_140K) {
        return FileSystemType::Unknown;
    }

    // Check for ProDOS signature
    // ProDOS stores volume directory at block 2 (track 0, sectors 4-5)
    // The first two bytes of block 2 should be 0x00 and storage type/name length

    // First, check for DOS 3.3 VTOC at track 17, sector 0
    if (isDOS33()) {
        return FileSystemType::DOS33;
    }

    if (isProDOS()) {
        return FileSystemType::ProDOS;
    }

    return FileSystemType::Unknown;
}

bool AppleDiskImage::isDOS33() const {
    // DOS 3.3 VTOC is at track 17, sector 0
    // VTOC structure:
    // +0x00: Track number of first catalog sector (usually 17)
    // +0x01: Sector number of first catalog sector (usually 15)
    // +0x06: Volume number (1-254)
    // +0x27: Number of tracks per disk (usually 35)
    // +0x30: Number of sectors per track (usually 16)

    size_t vtocOffset = calculateOffset(17, 0);
    if (vtocOffset + 0x35 >= m_data.size()) {
        return false;
    }

    uint8_t catalogTrack = m_data[vtocOffset + 0x01];
    uint8_t catalogSector = m_data[vtocOffset + 0x02];
    uint8_t volumeNum = m_data[vtocOffset + 0x06];
    uint8_t numTracks = m_data[vtocOffset + 0x34];
    uint8_t numSectors = m_data[vtocOffset + 0x35];

    // Validate VTOC values
    if (catalogTrack == 17 &&
        catalogSector >= 1 && catalogSector <= 15 &&
        volumeNum >= 1 && volumeNum <= 254 &&
        numTracks == 35 &&
        numSectors == 16) {
        return true;
    }

    return false;
}

bool AppleDiskImage::isProDOS() const {
    auto looksLikeProDOSBlock2 = [](const uint8_t* blk) -> bool {
        uint8_t storageType = (blk[0x04] >> 4) & 0x0F;
        uint8_t nameLen = blk[0x04] & 0x0F;
        uint8_t entryLength = blk[0x23];
        uint8_t entriesPerBlock = blk[0x24];
        uint16_t bitmapPtr = static_cast<uint16_t>(blk[0x27] | (blk[0x28] << 8));
        uint16_t totalBlocks = static_cast<uint16_t>(blk[0x29] | (blk[0x2A] << 8));

        return storageType == 0x0F &&
               nameLen >= 1 && nameLen <= 15 &&
               entryLength == 0x27 &&
               entriesPerBlock > 0 &&
               bitmapPtr > 0 && bitmapPtr < 280 &&
               totalBlocks > 0 && totalBlocks <= 280;
    };

    // First try direct contiguous block mapping (PO style).
    size_t blockOffset = 2 * 512;
    if (blockOffset + 512 <= m_data.size() &&
        looksLikeProDOSBlock2(m_data.data() + blockOffset)) {
        return true;
    }

    // For DOS-order images (.do/.dsk), ProDOS block2 maps to DOS logical
    // sectors 11 and 10 on track 0.
    if (getSectorOrder() == SectorOrder::DOS) {
        constexpr size_t s1 = 11;
        constexpr size_t s2 = 10;
        size_t off1 = s1 * 256;
        size_t off2 = s2 * 256;
        if (off1 + 256 <= m_data.size() && off2 + 256 <= m_data.size()) {
            uint8_t blk2[512];
            std::copy(m_data.begin() + off1, m_data.begin() + off1 + 256, blk2);
            std::copy(m_data.begin() + off2, m_data.begin() + off2 + 256, blk2 + 256);
            if (looksLikeProDOSBlock2(blk2)) {
                return true;
            }
        }
    }

    return false;
}

size_t AppleDiskImage::logicalToPhysical(size_t logical) const {
    if (logical >= SECTORS_16) {
        return logical;  // Out of range, return as-is
    }

    if (getSectorOrder() == SectorOrder::DOS) {
        return AppleInterleave::DOS33_INTERLEAVE[logical];
    } else {
        return AppleInterleave::PRODOS_INTERLEAVE[logical];
    }
}

size_t AppleDiskImage::physicalToLogical(size_t physical) const {
    if (physical >= SECTORS_16) {
        return physical;  // Out of range, return as-is
    }

    if (getSectorOrder() == SectorOrder::DOS) {
        return AppleInterleave::DOS33_DEINTERLEAVE[physical];
    } else {
        return AppleInterleave::PRODOS_DEINTERLEAVE[physical];
    }
}

SectorBuffer AppleDiskImage::readBlock(size_t block) {
    // ProDOS block = 512 bytes = 2 sectors
    // Block mapping depends on format

    if (block >= getTotalBlocks()) {
        throw SectorNotFoundException(static_cast<int>(block / 8),
                                      static_cast<int>(block % 8));
    }

    // Calculate track and sectors for this block
    // Each track has 8 blocks (16 sectors / 2 sectors per block)
    size_t track = block / 8;
    size_t blockInTrack = block % 8;

    // Read two sectors that make up this block
    SectorBuffer result;
    result.reserve(512);

    // ProDOS logical sectors in this track.
    size_t logical1 = blockInTrack * 2;
    size_t logical2 = blockInTrack * 2 + 1;
    size_t sector1 = logical1;
    size_t sector2 = logical2;

    if (getSectorOrder() == SectorOrder::DOS) {
        // DO images store DOS logical sectors. Convert ProDOS logical -> physical
        // then physical -> DOS logical.
        size_t phys1 = AppleInterleave::PRODOS_INTERLEAVE[logical1];
        size_t phys2 = AppleInterleave::PRODOS_INTERLEAVE[logical2];
        sector1 = AppleInterleave::DOS33_DEINTERLEAVE[phys1];
        sector2 = AppleInterleave::DOS33_DEINTERLEAVE[phys2];
    }

    auto s1 = readSector(track, 0, sector1);
    auto s2 = readSector(track, 0, sector2);

    result.insert(result.end(), s1.begin(), s1.end());
    result.insert(result.end(), s2.begin(), s2.end());

    return result;
}

void AppleDiskImage::writeBlock(size_t block, const SectorBuffer& data) {
    if (block >= getTotalBlocks()) {
        throw SectorNotFoundException(static_cast<int>(block / 8),
                                      static_cast<int>(block % 8));
    }

    if (data.size() < 512) {
        throw InvalidFormatException("Block data must be 512 bytes");
    }

    size_t track = block / 8;
    size_t blockInTrack = block % 8;
    size_t logical1 = blockInTrack * 2;
    size_t logical2 = blockInTrack * 2 + 1;
    size_t sector1 = logical1;
    size_t sector2 = logical2;

    if (getSectorOrder() == SectorOrder::DOS) {
        size_t phys1 = AppleInterleave::PRODOS_INTERLEAVE[logical1];
        size_t phys2 = AppleInterleave::PRODOS_INTERLEAVE[logical2];
        sector1 = AppleInterleave::DOS33_DEINTERLEAVE[phys1];
        sector2 = AppleInterleave::DOS33_DEINTERLEAVE[phys2];
    }

    SectorBuffer s1(data.begin(), data.begin() + 256);
    SectorBuffer s2(data.begin() + 256, data.begin() + 512);

    writeSector(track, 0, sector1, s1);
    writeSector(track, 0, sector2, s2);
}

} // namespace rde
