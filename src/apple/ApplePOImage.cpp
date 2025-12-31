#include "rdedisktool/apple/ApplePOImage.h"
#include "rdedisktool/apple/AppleDOImage.h"
#include "rdedisktool/DiskImageFactory.h"
#include <fstream>
#include <sstream>

namespace rde {

// Register format with factory
namespace {
    struct ApplePORegistrar {
        ApplePORegistrar() {
            DiskImageFactory::registerFormat(DiskFormat::ApplePO,
                []() -> std::unique_ptr<DiskImage> {
                    return std::make_unique<ApplePOImage>();
                });
        }
    };
    static ApplePORegistrar registrar;
}

ApplePOImage::ApplePOImage() : AppleDiskImage() {
}

void ApplePOImage::load(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        throw FileNotFoundException(path.string());
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw ReadException("Cannot open file: " + path.string());
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    if (fileSize != DISK_SIZE_140K) {
        throw InvalidFormatException("Invalid file size for Apple II disk image");
    }

    m_data.resize(fileSize);
    file.read(reinterpret_cast<char*>(m_data.data()), fileSize);

    if (!file) {
        throw ReadException("Failed to read file: " + path.string());
    }

    m_filePath = path;
    m_modified = false;
    m_fileSystemDetected = false;
}

void ApplePOImage::save(const std::filesystem::path& path) {
    std::filesystem::path savePath = path.empty() ? m_filePath : path;

    if (savePath.empty()) {
        throw WriteException("No file path specified");
    }

    if (m_writeProtected && savePath == m_filePath) {
        throw WriteProtectedException();
    }

    std::ofstream file(savePath, std::ios::binary);
    if (!file) {
        throw WriteException("Cannot create file: " + savePath.string());
    }

    file.write(reinterpret_cast<const char*>(m_data.data()), m_data.size());

    if (!file) {
        throw WriteException("Failed to write file: " + savePath.string());
    }

    if (path.empty() || path == m_filePath) {
        m_modified = false;
    }

    m_filePath = savePath;
}

void ApplePOImage::create(const DiskGeometry& geometry) {
    size_t tracks = geometry.tracks > 0 ? geometry.tracks : TRACKS_35;
    size_t sectors = geometry.sectorsPerTrack > 0 ? geometry.sectorsPerTrack : SECTORS_16;

    initGeometry(tracks, sectors);

    m_data.resize(tracks * sectors * BYTES_PER_SECTOR, 0);
    m_modified = true;
    m_fileSystemDetected = false;
    m_filePath.clear();
}

size_t ApplePOImage::calculateOffset(size_t track, size_t sector) const {
    // In PO format, sectors are stored in ProDOS logical order
    return (track * m_geometry.sectorsPerTrack + sector) * BYTES_PER_SECTOR;
}

SectorBuffer ApplePOImage::readSector(size_t track, size_t /*side*/, size_t sector) {
    if (track >= m_geometry.tracks) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }
    if (sector >= m_geometry.sectorsPerTrack) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }

    size_t offset = calculateOffset(track, sector);
    if (offset + BYTES_PER_SECTOR > m_data.size()) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }

    return SectorBuffer(m_data.begin() + offset,
                        m_data.begin() + offset + BYTES_PER_SECTOR);
}

void ApplePOImage::writeSector(size_t track, size_t /*side*/, size_t sector,
                               const SectorBuffer& data) {
    if (m_writeProtected) {
        throw WriteProtectedException();
    }

    if (track >= m_geometry.tracks) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }
    if (sector >= m_geometry.sectorsPerTrack) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }

    size_t offset = calculateOffset(track, sector);
    if (offset + BYTES_PER_SECTOR > m_data.size()) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }

    size_t copySize = std::min(data.size(), BYTES_PER_SECTOR);
    std::copy(data.begin(), data.begin() + copySize, m_data.begin() + offset);

    if (copySize < BYTES_PER_SECTOR) {
        std::fill(m_data.begin() + offset + copySize,
                  m_data.begin() + offset + BYTES_PER_SECTOR, 0);
    }

    m_modified = true;
}

TrackBuffer ApplePOImage::readTrack(size_t track, size_t /*side*/) {
    if (track >= m_geometry.tracks) {
        throw SectorNotFoundException(static_cast<int>(track), 0);
    }

    size_t offset = track * TRACK_SIZE;
    size_t trackSize = m_geometry.sectorsPerTrack * BYTES_PER_SECTOR;

    return TrackBuffer(m_data.begin() + offset,
                       m_data.begin() + offset + trackSize);
}

void ApplePOImage::writeTrack(size_t track, size_t /*side*/, const TrackBuffer& data) {
    if (m_writeProtected) {
        throw WriteProtectedException();
    }

    if (track >= m_geometry.tracks) {
        throw SectorNotFoundException(static_cast<int>(track), 0);
    }

    size_t offset = track * TRACK_SIZE;
    size_t trackSize = m_geometry.sectorsPerTrack * BYTES_PER_SECTOR;
    size_t copySize = std::min(data.size(), trackSize);

    std::copy(data.begin(), data.begin() + copySize, m_data.begin() + offset);

    if (copySize < trackSize) {
        std::fill(m_data.begin() + offset + copySize,
                  m_data.begin() + offset + trackSize, 0);
    }

    m_modified = true;
}

SectorBuffer ApplePOImage::readBlock(size_t block) {
    // In PO format, blocks are stored contiguously
    // Block N = sectors 2N and 2N+1 within the same track

    if (block >= getTotalBlocks()) {
        throw SectorNotFoundException(static_cast<int>(block / 8),
                                      static_cast<int>((block % 8) * 2));
    }

    size_t offset = block * 512;
    if (offset + 512 > m_data.size()) {
        throw SectorNotFoundException(static_cast<int>(block / 8),
                                      static_cast<int>((block % 8) * 2));
    }

    return SectorBuffer(m_data.begin() + offset, m_data.begin() + offset + 512);
}

void ApplePOImage::writeBlock(size_t block, const SectorBuffer& data) {
    if (m_writeProtected) {
        throw WriteProtectedException();
    }

    if (block >= getTotalBlocks()) {
        throw SectorNotFoundException(static_cast<int>(block / 8),
                                      static_cast<int>((block % 8) * 2));
    }

    if (data.size() < 512) {
        throw InvalidFormatException("Block data must be 512 bytes");
    }

    size_t offset = block * 512;
    if (offset + 512 > m_data.size()) {
        throw SectorNotFoundException(static_cast<int>(block / 8),
                                      static_cast<int>((block % 8) * 2));
    }

    std::copy(data.begin(), data.begin() + 512, m_data.begin() + offset);
    m_modified = true;
}

bool ApplePOImage::canConvertTo(DiskFormat format) const {
    switch (format) {
        case DiskFormat::AppleDO:
        case DiskFormat::AppleNIB:
        case DiskFormat::AppleWOZ2:
            return true;
        default:
            return false;
    }
}

std::unique_ptr<DiskImage> ApplePOImage::convertTo(DiskFormat format) const {
    if (!canConvertTo(format)) {
        throw UnsupportedFormatException("Cannot convert to " +
                                         std::string(formatToString(format)));
    }

    if (format == DiskFormat::AppleDO) {
        // Convert PO to DO by reordering sectors
        auto doImage = std::make_unique<AppleDOImage>();
        doImage->create(m_geometry);

        // Copy sectors with sector order conversion
        for (size_t track = 0; track < m_geometry.tracks; ++track) {
            for (size_t poSector = 0; poSector < m_geometry.sectorsPerTrack; ++poSector) {
                // Read from PO format
                size_t poOffset = (track * m_geometry.sectorsPerTrack + poSector) *
                                  BYTES_PER_SECTOR;
                SectorBuffer sector(m_data.begin() + poOffset,
                                   m_data.begin() + poOffset + BYTES_PER_SECTOR);

                // Convert ProDOS logical sector to DOS logical sector
                size_t physical = AppleInterleave::PRODOS_INTERLEAVE[poSector];
                size_t doSector = AppleInterleave::DOS33_DEINTERLEAVE[physical];

                doImage->writeSector(track, 0, doSector, sector);
            }
        }

        return doImage;
    }

    throw NotImplementedException("Conversion to " + std::string(formatToString(format)));
}

bool ApplePOImage::validate() const {
    if (m_data.size() != m_geometry.totalSize()) {
        return false;
    }

    if (getFileSystemType() != FileSystemType::Unknown) {
        return true;
    }

    return true;
}

std::string ApplePOImage::getDiagnostics() const {
    std::ostringstream oss;

    oss << "Format: Apple II ProDOS Order (.po)\n";
    oss << "Size: " << m_data.size() << " bytes\n";
    oss << "Tracks: " << m_geometry.tracks << "\n";
    oss << "Sectors/Track: " << m_geometry.sectorsPerTrack << "\n";
    oss << "Bytes/Sector: " << m_geometry.bytesPerSector << "\n";
    oss << "Blocks: " << getTotalBlocks() << "\n";
    oss << "File System: ";

    switch (getFileSystemType()) {
        case FileSystemType::DOS33:
            oss << "DOS 3.3\n";
            break;
        case FileSystemType::ProDOS:
            oss << "ProDOS\n";
            break;
        default:
            oss << "Unknown\n";
            break;
    }

    oss << "Write Protected: " << (m_writeProtected ? "Yes" : "No") << "\n";
    oss << "Modified: " << (m_modified ? "Yes" : "No") << "\n";

    return oss.str();
}

} // namespace rde
