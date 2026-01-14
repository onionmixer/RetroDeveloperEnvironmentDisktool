#include "rdedisktool/x68000/X68000XDFImage.h"
#include "rdedisktool/DiskImageFactory.h"
#include <fstream>
#include <sstream>
#include <cstring>

namespace rde {

// Register format with factory
namespace {
    struct X68000XDFRegistrar {
        X68000XDFRegistrar() {
            DiskImageFactory::registerFormat(DiskFormat::X68000XDF,
                []() -> std::unique_ptr<DiskImage> {
                    return std::make_unique<X68000XDFImage>();
                });
        }
    };
    static X68000XDFRegistrar registrar;
}

X68000XDFImage::X68000XDFImage() : X68000DiskImage() {
    // XDF fixed geometry
    initGeometry(XDF_TOTAL_TRACKS, XDF_HEADS, XDF_SECTORS_PER_TRACK, XDF_SECTOR_SIZE);
}

void X68000XDFImage::load(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        throw FileNotFoundException(path.string());
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw ReadException("Cannot open file: " + path.string());
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    // XDF has a fixed size of 1,261,568 bytes
    // But we'll accept slightly different sizes for compatibility
    if (fileSize > XDF_FILE_SIZE + 1024 || fileSize < XDF_FILE_SIZE - 1024) {
        throw InvalidFormatException("Invalid file size for XDF format: " +
                                     std::to_string(fileSize) + " bytes (expected " +
                                     std::to_string(XDF_FILE_SIZE) + ")");
    }

    // Allocate and fill with 0xE5 (standard empty byte)
    m_data.resize(XDF_FILE_SIZE, 0xE5);

    // Read file content
    file.read(reinterpret_cast<char*>(m_data.data()), fileSize);

    if (!file && !file.eof()) {
        throw ReadException("Failed to read file: " + path.string());
    }

    m_filePath = path;
    m_modified = false;
    m_fileSystemDetected = false;
}

void X68000XDFImage::save(const std::filesystem::path& path) {
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

void X68000XDFImage::create(const DiskGeometry& geometry) {
    (void)geometry;  // XDF has fixed geometry

    // Initialize with XDF fixed geometry
    initGeometry(XDF_TOTAL_TRACKS, XDF_HEADS, XDF_SECTORS_PER_TRACK, XDF_SECTOR_SIZE);

    // Create blank disk image filled with 0xE5
    m_data.resize(XDF_FILE_SIZE, 0xE5);

    m_modified = true;
    m_fileSystemDetected = false;
    m_filePath.clear();
}

size_t X68000XDFImage::calculateOffset(size_t track, size_t sector) const {
    // XDF uses linear track addressing
    // track: 0-153 (linear), sector: 1-8 (1-indexed)
    return ((track * XDF_SECTORS_PER_TRACK) + (sector - 1)) * XDF_SECTOR_SIZE;
}

void X68000XDFImage::validateParameters(size_t track, size_t sector) const {
    if (track >= XDF_TOTAL_TRACKS) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }
    if (sector < 1 || sector > XDF_SECTORS_PER_TRACK) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }
}

SectorBuffer X68000XDFImage::readSector(size_t track, size_t side, size_t sector) {
    // Convert track/side to linear track if needed
    size_t linearTrack = track;
    if (side == 0 && track < XDF_CYLINDERS) {
        // If side is provided separately, calculate linear track
        linearTrack = (track << 1) | side;
    } else if (side == 1 && track < XDF_CYLINDERS) {
        linearTrack = (track << 1) | 1;
    }
    // Otherwise, assume track is already linear

    validateParameters(linearTrack, sector);

    size_t offset = calculateOffset(linearTrack, sector);
    if (offset + XDF_SECTOR_SIZE > m_data.size()) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }

    return SectorBuffer(m_data.begin() + offset,
                        m_data.begin() + offset + XDF_SECTOR_SIZE);
}

void X68000XDFImage::writeSector(size_t track, size_t side, size_t sector,
                                  const SectorBuffer& data) {
    if (m_writeProtected) {
        throw WriteProtectedException();
    }

    // Convert track/side to linear track if needed
    size_t linearTrack = track;
    if (side == 0 && track < XDF_CYLINDERS) {
        linearTrack = (track << 1) | side;
    } else if (side == 1 && track < XDF_CYLINDERS) {
        linearTrack = (track << 1) | 1;
    }

    validateParameters(linearTrack, sector);

    size_t offset = calculateOffset(linearTrack, sector);
    if (offset + XDF_SECTOR_SIZE > m_data.size()) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }

    size_t copySize = std::min(data.size(), XDF_SECTOR_SIZE);
    std::copy(data.begin(), data.begin() + copySize, m_data.begin() + offset);

    // Fill remaining with 0xE5 if data is short
    if (copySize < XDF_SECTOR_SIZE) {
        std::fill(m_data.begin() + offset + copySize,
                  m_data.begin() + offset + XDF_SECTOR_SIZE, 0xE5);
    }

    m_modified = true;
}

TrackBuffer X68000XDFImage::readTrack(size_t track, size_t side) {
    // Convert to linear track
    size_t linearTrack = track;
    if (track < XDF_CYLINDERS && (side == 0 || side == 1)) {
        linearTrack = (track << 1) | side;
    }

    if (linearTrack >= XDF_TOTAL_TRACKS) {
        throw SectorNotFoundException(static_cast<int>(track), 0);
    }

    size_t trackSize = XDF_SECTORS_PER_TRACK * XDF_SECTOR_SIZE;
    size_t offset = linearTrack * trackSize;

    return TrackBuffer(m_data.begin() + offset,
                       m_data.begin() + offset + trackSize);
}

void X68000XDFImage::writeTrack(size_t track, size_t side, const TrackBuffer& data) {
    if (m_writeProtected) {
        throw WriteProtectedException();
    }

    // Convert to linear track
    size_t linearTrack = track;
    if (track < XDF_CYLINDERS && (side == 0 || side == 1)) {
        linearTrack = (track << 1) | side;
    }

    if (linearTrack >= XDF_TOTAL_TRACKS) {
        throw SectorNotFoundException(static_cast<int>(track), 0);
    }

    size_t trackSize = XDF_SECTORS_PER_TRACK * XDF_SECTOR_SIZE;
    size_t offset = linearTrack * trackSize;
    size_t copySize = std::min(data.size(), trackSize);

    std::copy(data.begin(), data.begin() + copySize, m_data.begin() + offset);

    if (copySize < trackSize) {
        std::fill(m_data.begin() + offset + copySize,
                  m_data.begin() + offset + trackSize, 0xE5);
    }

    m_modified = true;
}

bool X68000XDFImage::canConvertTo(DiskFormat format) const {
    switch (format) {
        case DiskFormat::X68000DIM:
            return true;  // Can convert to DIM (2HD type)
        default:
            return false;
    }
}

std::unique_ptr<DiskImage> X68000XDFImage::convertTo(DiskFormat format) const {
    if (!canConvertTo(format)) {
        throw UnsupportedFormatException("Cannot convert XDF to " +
                                         std::string(formatToString(format)));
    }

    // Conversion to DIM will be implemented in DIM class
    throw NotImplementedException("XDF to DIM conversion");
}

bool X68000XDFImage::validate() const {
    // Check file size
    if (m_data.size() != XDF_FILE_SIZE) {
        return false;
    }

    // XDF has no header to validate, so size check is sufficient
    return true;
}

std::string X68000XDFImage::getDiagnostics() const {
    std::ostringstream oss;

    oss << "Format: X68000 XDF (.xdf)\n";
    oss << "Size: " << m_data.size() << " bytes\n";
    oss << "Cylinders: " << XDF_CYLINDERS << "\n";
    oss << "Heads: " << XDF_HEADS << "\n";
    oss << "Total Tracks: " << XDF_TOTAL_TRACKS << "\n";
    oss << "Sectors/Track: " << XDF_SECTORS_PER_TRACK << "\n";
    oss << "Bytes/Sector: " << XDF_SECTOR_SIZE << "\n";
    oss << "Total Capacity: " << (XDF_TOTAL_TRACKS * XDF_SECTORS_PER_TRACK * XDF_SECTOR_SIZE) << " bytes\n";

    oss << "File System: ";
    switch (getFileSystemType()) {
        case FileSystemType::Human68k: oss << "Human68k"; break;
        default: oss << "Unknown"; break;
    }
    oss << "\n";

    oss << "Write Protected: " << (m_writeProtected ? "Yes" : "No") << "\n";
    oss << "Modified: " << (m_modified ? "Yes" : "No") << "\n";

    return oss.str();
}

} // namespace rde
