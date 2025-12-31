#include "rdedisktool/apple/AppleDOImage.h"
#include "rdedisktool/apple/ApplePOImage.h"
#include "rdedisktool/DiskImageFactory.h"
#include <fstream>
#include <sstream>

namespace rde {

// Register format with factory
namespace {
    struct AppleDORegistrar {
        AppleDORegistrar() {
            DiskImageFactory::registerFormat(DiskFormat::AppleDO,
                []() -> std::unique_ptr<DiskImage> {
                    return std::make_unique<AppleDOImage>();
                });
        }
    };
    static AppleDORegistrar registrar;
}

AppleDOImage::AppleDOImage() : AppleDiskImage() {
}

void AppleDOImage::load(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        throw FileNotFoundException(path.string());
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw ReadException("Cannot open file: " + path.string());
    }

    // Get file size
    size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    // Validate size
    if (fileSize != DISK_SIZE_140K) {
        // Also accept DOS 3.2 size (13 sectors/track)
        size_t dos32Size = TRACKS_35 * SECTORS_13 * BYTES_PER_SECTOR;
        if (fileSize == dos32Size) {
            initGeometry(TRACKS_35, SECTORS_13);
        } else {
            throw InvalidFormatException("Invalid file size for Apple II disk image");
        }
    }

    // Read data
    m_data.resize(fileSize);
    file.read(reinterpret_cast<char*>(m_data.data()), fileSize);

    if (!file) {
        throw ReadException("Failed to read file: " + path.string());
    }

    m_filePath = path;
    m_modified = false;
    m_fileSystemDetected = false;
}

void AppleDOImage::save(const std::filesystem::path& path) {
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

void AppleDOImage::create(const DiskGeometry& geometry) {
    size_t tracks = geometry.tracks > 0 ? geometry.tracks : TRACKS_35;
    size_t sectors = geometry.sectorsPerTrack > 0 ? geometry.sectorsPerTrack : SECTORS_16;

    initGeometry(tracks, sectors);

    // Create blank disk image filled with zeros
    m_data.resize(tracks * sectors * BYTES_PER_SECTOR, 0);
    m_modified = true;
    m_fileSystemDetected = false;
    m_filePath.clear();
}

size_t AppleDOImage::calculateOffset(size_t track, size_t sector) const {
    // In DO format, sectors are stored in DOS logical order
    // No additional mapping needed for sector number
    return (track * m_geometry.sectorsPerTrack + sector) * BYTES_PER_SECTOR;
}

SectorBuffer AppleDOImage::readSector(size_t track, size_t /*side*/, size_t sector) {
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

void AppleDOImage::writeSector(size_t track, size_t /*side*/, size_t sector,
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

    // Copy data, padding or truncating to sector size
    size_t copySize = std::min(data.size(), BYTES_PER_SECTOR);
    std::copy(data.begin(), data.begin() + copySize, m_data.begin() + offset);

    // Zero-fill if data is short
    if (copySize < BYTES_PER_SECTOR) {
        std::fill(m_data.begin() + offset + copySize,
                  m_data.begin() + offset + BYTES_PER_SECTOR, 0);
    }

    m_modified = true;
}

TrackBuffer AppleDOImage::readTrack(size_t track, size_t /*side*/) {
    if (track >= m_geometry.tracks) {
        throw SectorNotFoundException(static_cast<int>(track), 0);
    }

    size_t offset = track * TRACK_SIZE;
    size_t trackSize = m_geometry.sectorsPerTrack * BYTES_PER_SECTOR;

    return TrackBuffer(m_data.begin() + offset,
                       m_data.begin() + offset + trackSize);
}

void AppleDOImage::writeTrack(size_t track, size_t /*side*/, const TrackBuffer& data) {
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

    // Zero-fill remainder
    if (copySize < trackSize) {
        std::fill(m_data.begin() + offset + copySize,
                  m_data.begin() + offset + trackSize, 0);
    }

    m_modified = true;
}

bool AppleDOImage::canConvertTo(DiskFormat format) const {
    switch (format) {
        case DiskFormat::ApplePO:
        case DiskFormat::AppleNIB:
        case DiskFormat::AppleWOZ2:
            return true;
        default:
            return false;
    }
}

std::unique_ptr<DiskImage> AppleDOImage::convertTo(DiskFormat format) const {
    if (!canConvertTo(format)) {
        throw UnsupportedFormatException("Cannot convert to " +
                                         std::string(formatToString(format)));
    }

    if (format == DiskFormat::ApplePO) {
        // Convert DO to PO by reordering sectors
        auto poImage = std::make_unique<ApplePOImage>();
        poImage->create(m_geometry);

        // Copy sectors with sector order conversion
        for (size_t track = 0; track < m_geometry.tracks; ++track) {
            for (size_t doSector = 0; doSector < m_geometry.sectorsPerTrack; ++doSector) {
                // Read from DO format
                size_t doOffset = (track * m_geometry.sectorsPerTrack + doSector) *
                                  BYTES_PER_SECTOR;
                SectorBuffer sector(m_data.begin() + doOffset,
                                   m_data.begin() + doOffset + BYTES_PER_SECTOR);

                // Convert DOS logical sector to ProDOS logical sector
                // DO stores in DOS order, PO stores in ProDOS order
                // We need to map: DOS logical -> Physical -> ProDOS logical
                size_t physical = AppleInterleave::DOS33_INTERLEAVE[doSector];
                size_t poSector = AppleInterleave::PRODOS_DEINTERLEAVE[physical];

                poImage->writeSector(track, 0, poSector, sector);
            }
        }

        return poImage;
    }

    // Other conversions not yet implemented
    throw NotImplementedException("Conversion to " + std::string(formatToString(format)));
}

bool AppleDOImage::validate() const {
    // Check file size
    if (m_data.size() != m_geometry.totalSize()) {
        return false;
    }

    // Check for known file system
    if (getFileSystemType() != FileSystemType::Unknown) {
        return true;
    }

    // No file system detected, but size is valid
    return true;
}

std::string AppleDOImage::getDiagnostics() const {
    std::ostringstream oss;

    oss << "Format: Apple II DOS Order (.do/.dsk)\n";
    oss << "Size: " << m_data.size() << " bytes\n";
    oss << "Tracks: " << m_geometry.tracks << "\n";
    oss << "Sectors/Track: " << m_geometry.sectorsPerTrack << "\n";
    oss << "Bytes/Sector: " << m_geometry.bytesPerSector << "\n";
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
