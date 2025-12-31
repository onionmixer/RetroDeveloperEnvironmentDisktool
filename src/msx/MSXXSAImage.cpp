#include "rdedisktool/msx/MSXXSAImage.h"
#include "rdedisktool/msx/MSXDSKImage.h"
#include "rdedisktool/msx/MSXDMKImage.h"
#include "rdedisktool/msx/XSAExtractor.h"
#include "rdedisktool/DiskImageFactory.h"
#include <fstream>
#include <sstream>

namespace rde {

// Register format with factory
namespace {
    struct MSXXSARegistrar {
        MSXXSARegistrar() {
            DiskImageFactory::registerFormat(DiskFormat::MSXXSA,
                []() -> std::unique_ptr<DiskImage> {
                    return std::make_unique<MSXXSAImage>();
                });
        }
    };
    static MSXXSARegistrar registrar;
}

MSXXSAImage::MSXXSAImage() : MSXDiskImage() {
    m_writeProtected = true;  // XSA is always read-only
}

void MSXXSAImage::load(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        throw FileNotFoundException(path.string());
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw ReadException("Cannot open file: " + path.string());
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    // Read compressed data
    std::vector<uint8_t> compressedData(fileSize);
    file.read(reinterpret_cast<char*>(compressedData.data()), fileSize);

    if (!file) {
        throw ReadException("Failed to read file: " + path.string());
    }

    // Verify XSA format
    if (!isXSAFormat(compressedData)) {
        throw InvalidFormatException("Not a valid XSA image: " + path.string());
    }

    // Store compressed size for diagnostics
    m_compressedSize = fileSize;

    // Extract original filename from header
    if (fileSize > 12) {
        size_t filenameStart = 12;
        size_t filenameEnd = filenameStart;
        while (filenameEnd < fileSize && compressedData[filenameEnd] != 0) {
            ++filenameEnd;
        }
        m_originalFilename = std::string(
            reinterpret_cast<char*>(&compressedData[filenameStart]),
            filenameEnd - filenameStart);
    }

    // Decompress XSA data
    try {
        XSAExtractor extractor(compressedData);
        m_data = extractor.extract();
    } catch (const XSAExtractor::XSAException& e) {
        throw InvalidFormatException(std::string("XSA decompression failed: ") + e.what());
    }

    m_filePath = path;

    // Detect geometry from decompressed data
    detectGeometry();

    m_modified = false;
    m_fileSystemDetected = false;
    m_writeProtected = true;  // Always read-only
}

void MSXXSAImage::save(const std::filesystem::path& /*path*/) {
    // XSA format is read-only. Use convertTo(DiskFormat::MSXDSK) to save.
    throw WriteProtectedException();
}

void MSXXSAImage::create(const DiskGeometry& /*geometry*/) {
    throw NotImplementedException("Cannot create XSA images. "
        "Create a DSK image and convert if compression is needed.");
}

void MSXXSAImage::setWriteProtected(bool /*protect*/) {
    // XSA is always write-protected, ignore the setting
    m_writeProtected = true;
}

SectorBuffer MSXXSAImage::readSector(size_t track, size_t side, size_t sector) {
    if (track >= m_geometry.tracks) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }
    if (side >= m_geometry.sides) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }
    if (sector >= m_geometry.sectorsPerTrack) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }

    size_t offset = calculateOffset(track, side, sector);
    if (offset + BYTES_PER_SECTOR > m_data.size()) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }

    return SectorBuffer(m_data.begin() + offset,
                        m_data.begin() + offset + BYTES_PER_SECTOR);
}

void MSXXSAImage::writeSector(size_t /*track*/, size_t /*side*/, size_t /*sector*/,
                              const SectorBuffer& /*data*/) {
    throw WriteProtectedException();
}

TrackBuffer MSXXSAImage::readTrack(size_t track, size_t side) {
    if (track >= m_geometry.tracks || side >= m_geometry.sides) {
        throw SectorNotFoundException(static_cast<int>(track), 0);
    }

    size_t trackSize = m_geometry.sectorsPerTrack * BYTES_PER_SECTOR;
    size_t offset = calculateOffset(track, side, 0);

    if (offset + trackSize > m_data.size()) {
        throw SectorNotFoundException(static_cast<int>(track), 0);
    }

    return TrackBuffer(m_data.begin() + offset,
                       m_data.begin() + offset + trackSize);
}

void MSXXSAImage::writeTrack(size_t /*track*/, size_t /*side*/,
                             const TrackBuffer& /*data*/) {
    throw WriteProtectedException();
}

bool MSXXSAImage::canConvertTo(DiskFormat format) const {
    switch (format) {
        case DiskFormat::MSXDSK:
        case DiskFormat::MSXDMK:
            return true;
        default:
            return false;
    }
}

std::unique_ptr<DiskImage> MSXXSAImage::convertTo(DiskFormat format) const {
    if (!canConvertTo(format)) {
        throw UnsupportedFormatException("Cannot convert XSA to " +
                                         std::string(formatToString(format)));
    }

    if (format == DiskFormat::MSXDSK) {
        auto dskImage = std::make_unique<MSXDSKImage>();
        dskImage->create(m_geometry);

        // Copy all sectors
        for (size_t track = 0; track < m_geometry.tracks; ++track) {
            for (size_t side = 0; side < m_geometry.sides; ++side) {
                for (size_t sector = 0; sector < m_geometry.sectorsPerTrack; ++sector) {
                    size_t offset = calculateOffset(track, side, sector);
                    if (offset + BYTES_PER_SECTOR <= m_data.size()) {
                        SectorBuffer sectorData(m_data.begin() + offset,
                                               m_data.begin() + offset + BYTES_PER_SECTOR);
                        dskImage->writeSector(track, side, sector, sectorData);
                    }
                }
            }
        }

        return dskImage;
    }

    if (format == DiskFormat::MSXDMK) {
        auto dmkImage = std::make_unique<MSXDMKImage>();
        dmkImage->create(m_geometry);

        // Copy all sectors
        for (size_t track = 0; track < m_geometry.tracks; ++track) {
            for (size_t side = 0; side < m_geometry.sides; ++side) {
                for (size_t sector = 0; sector < m_geometry.sectorsPerTrack; ++sector) {
                    size_t offset = calculateOffset(track, side, sector);
                    if (offset + BYTES_PER_SECTOR <= m_data.size()) {
                        SectorBuffer sectorData(m_data.begin() + offset,
                                               m_data.begin() + offset + BYTES_PER_SECTOR);
                        dmkImage->writeSector(track, side, sector, sectorData);
                    }
                }
            }
        }

        return dmkImage;
    }

    throw NotImplementedException("Conversion to " + std::string(formatToString(format)));
}

bool MSXXSAImage::validate() const {
    // Check minimum decompressed size
    if (m_data.size() < 163840) {
        return false;
    }

    // Check for valid boot sector
    if (m_data.size() >= BYTES_PER_SECTOR) {
        // Check for JMP instruction
        if (m_data[0] != 0xEB && m_data[0] != 0xE9) {
            // May still be valid but unformatted
            return true;
        }

        // Check bytes per sector
        uint16_t bps = m_data[11] | (m_data[12] << 8);
        if (bps != 512) {
            return false;
        }
    }

    return true;
}

std::string MSXXSAImage::getDiagnostics() const {
    std::ostringstream oss;

    oss << "Format: MSX XSA (compressed, read-only)\n";
    oss << "Compressed Size: " << m_compressedSize << " bytes\n";
    oss << "Decompressed Size: " << m_data.size() << " bytes\n";

    if (m_compressedSize > 0) {
        double ratio = static_cast<double>(m_compressedSize) / m_data.size() * 100.0;
        oss << "Compression Ratio: " << std::fixed << std::setprecision(1)
            << ratio << "%\n";
    }

    if (!m_originalFilename.empty()) {
        oss << "Original Filename: " << m_originalFilename << "\n";
    }

    oss << "Tracks: " << m_geometry.tracks << "\n";
    oss << "Sides: " << m_geometry.sides << "\n";
    oss << "Sectors/Track: " << m_geometry.sectorsPerTrack << "\n";
    oss << "Bytes/Sector: " << m_geometry.bytesPerSector << "\n";

    oss << "File System: ";
    switch (getFileSystemType()) {
        case FileSystemType::MSXDOS1: oss << "MSX-DOS 1"; break;
        case FileSystemType::MSXDOS2: oss << "MSX-DOS 2"; break;
        case FileSystemType::FAT12: oss << "FAT12"; break;
        case FileSystemType::FAT16: oss << "FAT16"; break;
        default: oss << "Unknown"; break;
    }
    oss << "\n";

    oss << "OEM Name: " << getOEMName() << "\n";
    oss << "Media Descriptor: 0x" << std::hex << static_cast<int>(getMediaDescriptor())
        << std::dec << "\n";
    oss << "Clusters: " << getTotalClusters() << "\n";
    oss << "Bytes/Cluster: " << getBytesPerCluster() << "\n";
    oss << "Write Protected: Yes (read-only format)\n";

    return oss.str();
}

bool MSXXSAImage::isXSAFormat(const std::vector<uint8_t>& data) {
    return XSAExtractor::isXSAFormat(data);
}

} // namespace rde
