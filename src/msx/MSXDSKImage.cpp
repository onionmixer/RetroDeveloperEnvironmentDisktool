#include "rdedisktool/msx/MSXDSKImage.h"
#include "rdedisktool/msx/MSXDMKImage.h"
#include "rdedisktool/DiskImageFactory.h"
#include <fstream>
#include <sstream>
#include <cstring>
#include <ctime>

namespace rde {

// Register format with factory
namespace {
    struct MSXDSKRegistrar {
        MSXDSKRegistrar() {
            DiskImageFactory::registerFormat(DiskFormat::MSXDSK,
                []() -> std::unique_ptr<DiskImage> {
                    return std::make_unique<MSXDSKImage>();
                });
        }
    };
    static MSXDSKRegistrar registrar;
}

MSXDSKImage::MSXDSKImage() : MSXDiskImage() {
}

void MSXDSKImage::load(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        throw FileNotFoundException(path.string());
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw ReadException("Cannot open file: " + path.string());
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    // Validate size is reasonable for MSX disk
    if (fileSize < 163840 || fileSize > 1474560) {
        throw InvalidFormatException("Invalid file size for MSX disk image");
    }

    m_data.resize(fileSize);
    file.read(reinterpret_cast<char*>(m_data.data()), fileSize);

    if (!file) {
        throw ReadException("Failed to read file: " + path.string());
    }

    m_filePath = path;

    // Detect geometry from boot sector or file size
    detectGeometry();

    m_modified = false;
    m_fileSystemDetected = false;
}

void MSXDSKImage::save(const std::filesystem::path& path) {
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

void MSXDSKImage::create(const DiskGeometry& geometry) {
    size_t tracks = geometry.tracks > 0 ? geometry.tracks : TRACKS_80;
    size_t sides = geometry.sides > 0 ? geometry.sides : SIDES_2;
    size_t sectors = geometry.sectorsPerTrack > 0 ? geometry.sectorsPerTrack : SECTORS_9;

    initGeometry(tracks, sides, sectors);

    // Create blank disk image
    m_data.resize(m_geometry.totalSize(), 0);
    m_modified = true;
    m_fileSystemDetected = false;
    m_filePath.clear();
}

SectorBuffer MSXDSKImage::readSector(size_t track, size_t side, size_t sector) {
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

void MSXDSKImage::writeSector(size_t track, size_t side, size_t sector,
                              const SectorBuffer& data) {
    if (m_writeProtected) {
        throw WriteProtectedException();
    }

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

    size_t copySize = std::min(data.size(), BYTES_PER_SECTOR);
    std::copy(data.begin(), data.begin() + copySize, m_data.begin() + offset);

    // Zero-fill if data is short
    if (copySize < BYTES_PER_SECTOR) {
        std::fill(m_data.begin() + offset + copySize,
                  m_data.begin() + offset + BYTES_PER_SECTOR, 0);
    }

    m_modified = true;
}

TrackBuffer MSXDSKImage::readTrack(size_t track, size_t side) {
    if (track >= m_geometry.tracks || side >= m_geometry.sides) {
        throw SectorNotFoundException(static_cast<int>(track), 0);
    }

    size_t trackSize = m_geometry.sectorsPerTrack * BYTES_PER_SECTOR;
    size_t offset = calculateOffset(track, side, 0);

    return TrackBuffer(m_data.begin() + offset,
                       m_data.begin() + offset + trackSize);
}

void MSXDSKImage::writeTrack(size_t track, size_t side, const TrackBuffer& data) {
    if (m_writeProtected) {
        throw WriteProtectedException();
    }

    if (track >= m_geometry.tracks || side >= m_geometry.sides) {
        throw SectorNotFoundException(static_cast<int>(track), 0);
    }

    size_t trackSize = m_geometry.sectorsPerTrack * BYTES_PER_SECTOR;
    size_t offset = calculateOffset(track, side, 0);
    size_t copySize = std::min(data.size(), trackSize);

    std::copy(data.begin(), data.begin() + copySize, m_data.begin() + offset);

    if (copySize < trackSize) {
        std::fill(m_data.begin() + offset + copySize,
                  m_data.begin() + offset + trackSize, 0);
    }

    m_modified = true;
}

void MSXDSKImage::formatMSXDOS(const std::string& volumeLabel, uint8_t mediaDescriptor) {
    if (m_writeProtected) {
        throw WriteProtectedException();
    }

    // Clear entire disk
    std::fill(m_data.begin(), m_data.end(), 0);

    // Build boot sector
    MSXBootSector boot = {};

    // Jump instruction
    boot.jumpInstruction[0] = 0xEB;
    boot.jumpInstruction[1] = 0xFE;  // Jump to 0x100
    boot.jumpInstruction[2] = 0x90;

    // OEM name
    std::memcpy(boot.oemName, "MSX-DOS ", 8);

    // BPB (BIOS Parameter Block)
    boot.bytesPerSector = 512;
    boot.sectorsPerCluster = 2;
    boot.reservedSectors = 1;
    boot.numberOfFATs = 2;
    boot.rootEntryCount = 112;

    uint32_t totalSectors = static_cast<uint32_t>(m_geometry.totalSectors());
    boot.totalSectors16 = totalSectors < 65536 ? static_cast<uint16_t>(totalSectors) : 0;
    boot.totalSectors32 = totalSectors >= 65536 ? totalSectors : 0;

    boot.mediaDescriptor = mediaDescriptor;

    // Calculate sectors per FAT
    // Formula: sectorsPerFAT = (totalClusters * 1.5 + 511) / 512 for FAT12
    uint16_t sectorsPerFAT = 3;  // Default for 720KB
    if (m_geometry.totalSize() <= 360 * 1024) {
        sectorsPerFAT = 2;
    }
    boot.sectorsPerFAT = sectorsPerFAT;

    boot.sectorsPerTrack = static_cast<uint16_t>(m_geometry.sectorsPerTrack);
    boot.numberOfHeads = static_cast<uint16_t>(m_geometry.sides);
    boot.hiddenSectors = 0;

    // Copy boot sector to disk
    std::memcpy(m_data.data(), &boot, sizeof(boot));

    // Initialize FATs
    // FAT starts at sector 1
    size_t fatOffset = boot.reservedSectors * BYTES_PER_SECTOR;

    for (int fat = 0; fat < boot.numberOfFATs; ++fat) {
        size_t offset = fatOffset + fat * boot.sectorsPerFAT * BYTES_PER_SECTOR;

        // First two FAT entries are reserved
        // FAT12: F9 FF FF (for 720KB)
        m_data[offset + 0] = mediaDescriptor;
        m_data[offset + 1] = 0xFF;
        m_data[offset + 2] = 0xFF;
    }

    // Root directory starts after FATs
    size_t rootOffset = (boot.reservedSectors + boot.numberOfFATs * boot.sectorsPerFAT) *
                        BYTES_PER_SECTOR;

    // Create volume label entry if provided
    if (!volumeLabel.empty()) {
        char label[11];
        std::memset(label, ' ', 11);

        size_t copyLen = std::min(volumeLabel.size(), size_t(11));
        std::memcpy(label, volumeLabel.c_str(), copyLen);

        // Copy label to root directory
        std::memcpy(&m_data[rootOffset], label, 11);
        m_data[rootOffset + 11] = 0x08;  // Volume label attribute

        // Set timestamp
        std::time_t now = std::time(nullptr);
        std::tm* tm = std::localtime(&now);

        uint16_t time = (tm->tm_hour << 11) | (tm->tm_min << 5) | (tm->tm_sec / 2);
        uint16_t date = ((tm->tm_year - 80) << 9) | ((tm->tm_mon + 1) << 5) | tm->tm_mday;

        m_data[rootOffset + 22] = time & 0xFF;
        m_data[rootOffset + 23] = (time >> 8) & 0xFF;
        m_data[rootOffset + 24] = date & 0xFF;
        m_data[rootOffset + 25] = (date >> 8) & 0xFF;
    }

    m_modified = true;
    m_fileSystemDetected = false;
}

bool MSXDSKImage::canConvertTo(DiskFormat format) const {
    switch (format) {
        case DiskFormat::MSXDMK:
            return true;
        default:
            return false;
    }
}

std::unique_ptr<DiskImage> MSXDSKImage::convertTo(DiskFormat format) const {
    if (!canConvertTo(format)) {
        throw UnsupportedFormatException("Cannot convert to " +
                                         std::string(formatToString(format)));
    }

    if (format == DiskFormat::MSXDMK) {
        auto dmkImage = std::make_unique<MSXDMKImage>();
        dmkImage->create(m_geometry);

        // Copy all sectors
        for (size_t track = 0; track < m_geometry.tracks; ++track) {
            for (size_t side = 0; side < m_geometry.sides; ++side) {
                for (size_t sector = 0; sector < m_geometry.sectorsPerTrack; ++sector) {
                    size_t offset = calculateOffset(track, side, sector);
                    SectorBuffer sectorData(m_data.begin() + offset,
                                           m_data.begin() + offset + BYTES_PER_SECTOR);
                    dmkImage->writeSector(track, side, sector, sectorData);
                }
            }
        }

        return dmkImage;
    }

    throw NotImplementedException("Conversion to " + std::string(formatToString(format)));
}

bool MSXDSKImage::validate() const {
    // Check file size is valid
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

std::string MSXDSKImage::getDiagnostics() const {
    std::ostringstream oss;

    oss << "Format: MSX DSK (.dsk)\n";
    oss << "Size: " << m_data.size() << " bytes\n";
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
    oss << "Write Protected: " << (m_writeProtected ? "Yes" : "No") << "\n";
    oss << "Modified: " << (m_modified ? "Yes" : "No") << "\n";

    return oss.str();
}

} // namespace rde
