#include "rdedisktool/x68000/X68000DIMImage.h"
#include "rdedisktool/x68000/X68000XDFImage.h"
#include "rdedisktool/DiskImageFactory.h"
#include <fstream>
#include <sstream>
#include <cstring>

namespace rde {

// Register format with factory
namespace {
    struct X68000DIMRegistrar {
        X68000DIMRegistrar() {
            DiskImageFactory::registerFormat(DiskFormat::X68000DIM,
                []() -> std::unique_ptr<DiskImage> {
                    return std::make_unique<X68000DIMImage>();
                });
        }
    };
    static X68000DIMRegistrar registrar;
}

// Static member definitions
constexpr size_t X68000DIMImage::TRACK_SIZES[10];
constexpr size_t X68000DIMImage::SECTOR_SIZES[10];
constexpr size_t X68000DIMImage::SECTORS_PER_TRACK[10];
constexpr size_t X68000DIMImage::MAX_TRACKS[10];

X68000DIMImage::X68000DIMImage() : X68000DiskImage() {
    // Default to DIM_2HD type
    m_dimType = X68000DIMType::DIM_2HD;
    std::memset(&m_header, 0, sizeof(m_header));
    m_header.type = static_cast<uint8_t>(m_dimType);

    // Set all track flags to present by default
    std::memset(m_header.trkflag, 1, sizeof(m_header.trkflag));

    // Initialize geometry for 2HD
    initGeometry(154, 2, 8, 1024);
}

void X68000DIMImage::load(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        throw FileNotFoundException(path.string());
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw ReadException("Cannot open file: " + path.string());
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    // Minimum size check (header only)
    if (fileSize < DIM_HEADER_SIZE) {
        throw InvalidFormatException("File too small for DIM format");
    }

    // Read header first
    file.read(reinterpret_cast<char*>(&m_header), sizeof(m_header));

    if (!file) {
        throw ReadException("Failed to read DIM header");
    }

    // Validate header
    if (!isValidDIMType(m_header.type)) {
        throw InvalidFormatException("Invalid DIM type: " + std::to_string(m_header.type));
    }

    m_dimType = static_cast<X68000DIMType>(m_header.type);

    // Update geometry based on DIM type
    size_t sectorSize = SECTOR_SIZES[m_header.type];
    size_t sectorsPerTrack = SECTORS_PER_TRACK[m_header.type];
    size_t maxTracks = MAX_TRACKS[m_header.type];
    initGeometry(maxTracks, 2, sectorsPerTrack, sectorSize);

    // Calculate expected size and allocate
    size_t trackSize = TRACK_SIZES[m_header.type];
    size_t maxDataSize = DIM_HEADER_SIZE + (trackSize * DIM_MAX_TRACKS);

    m_data.resize(maxDataSize, 0xE5);

    // Copy header to data
    std::memcpy(m_data.data(), &m_header, sizeof(m_header));

    // Read track data based on track flags
    size_t dataOffset = DIM_HEADER_SIZE;

    // If overtrack is 0, all tracks are present
    if (m_header.overtrack == 0) {
        std::memset(m_header.trkflag, 1, sizeof(m_header.trkflag));
    }

    for (size_t track = 0; track < DIM_MAX_TRACKS; ++track) {
        if (m_header.trkflag[track]) {
            // Read track data from file
            size_t readSize = std::min(trackSize, fileSize - static_cast<size_t>(file.tellg()));
            if (readSize > 0) {
                file.read(reinterpret_cast<char*>(m_data.data() + dataOffset), readSize);
            }
        }
        dataOffset += trackSize;
    }

    m_filePath = path;
    m_modified = false;
    m_fileSystemDetected = false;
}

void X68000DIMImage::save(const std::filesystem::path& path) {
    std::filesystem::path savePath = path.empty() ? m_filePath : path;

    if (savePath.empty()) {
        throw WriteException("No file path specified");
    }

    if (m_writeProtected && savePath == m_filePath) {
        throw WriteProtectedException();
    }

    // Update header in data
    buildHeader();

    std::ofstream file(savePath, std::ios::binary);
    if (!file) {
        throw WriteException("Cannot create file: " + savePath.string());
    }

    // Write header
    file.write(reinterpret_cast<const char*>(&m_header), sizeof(m_header));

    // Write track data (only tracks that are present)
    size_t trackSize = TRACK_SIZES[m_header.type];
    size_t dataOffset = DIM_HEADER_SIZE;

    for (size_t track = 0; track < DIM_MAX_TRACKS; ++track) {
        if (m_header.trkflag[track]) {
            file.write(reinterpret_cast<const char*>(m_data.data() + dataOffset), trackSize);
        }
        dataOffset += trackSize;
    }

    if (!file) {
        throw WriteException("Failed to write file: " + savePath.string());
    }

    if (path.empty() || path == m_filePath) {
        m_modified = false;
    }

    m_filePath = savePath;
}

void X68000DIMImage::create(const DiskGeometry& geometry) {
    // Determine DIM type from geometry
    if (geometry.bytesPerSector == 512) {
        if (geometry.sectorsPerTrack == 18) {
            m_dimType = X68000DIMType::DIM_2HQ;
        } else {
            m_dimType = X68000DIMType::DIM_2HC;
        }
    } else {
        if (geometry.sectorsPerTrack == 9) {
            m_dimType = X68000DIMType::DIM_2HS;
        } else {
            m_dimType = X68000DIMType::DIM_2HD;
        }
    }

    // Initialize header
    std::memset(&m_header, 0, sizeof(m_header));
    m_header.type = static_cast<uint8_t>(m_dimType);
    std::memset(m_header.trkflag, 1, sizeof(m_header.trkflag));
    m_header.overtrack = 0;

    // Update geometry
    size_t sectorSize = SECTOR_SIZES[m_header.type];
    size_t sectorsPerTrack = SECTORS_PER_TRACK[m_header.type];
    size_t maxTracks = MAX_TRACKS[m_header.type];
    initGeometry(maxTracks, 2, sectorsPerTrack, sectorSize);

    // Allocate data
    size_t trackSize = TRACK_SIZES[m_header.type];
    size_t totalSize = DIM_HEADER_SIZE + (trackSize * DIM_MAX_TRACKS);
    m_data.resize(totalSize, 0xE5);

    // Copy header to data
    std::memcpy(m_data.data(), &m_header, sizeof(m_header));

    m_modified = true;
    m_fileSystemDetected = false;
    m_filePath.clear();
}

void X68000DIMImage::parseHeader() {
    if (m_data.size() >= sizeof(m_header)) {
        std::memcpy(&m_header, m_data.data(), sizeof(m_header));
        m_dimType = static_cast<X68000DIMType>(m_header.type);
    }
}

void X68000DIMImage::buildHeader() {
    m_header.type = static_cast<uint8_t>(m_dimType);
    if (m_data.size() >= sizeof(m_header)) {
        std::memcpy(m_data.data(), &m_header, sizeof(m_header));
    }
}

size_t X68000DIMImage::getDataOffset(size_t track) const {
    size_t trackSize = TRACK_SIZES[static_cast<uint8_t>(m_dimType)];
    return DIM_HEADER_SIZE + (track * trackSize);
}

size_t X68000DIMImage::calculateOffset(size_t track, size_t sector) const {
    size_t sectorSize = SECTOR_SIZES[static_cast<uint8_t>(m_dimType)];
    return getDataOffset(track) + ((sector - 1) * sectorSize);
}

void X68000DIMImage::validateParameters(size_t track, size_t sector) const {
    size_t maxTrack = MAX_TRACKS[static_cast<uint8_t>(m_dimType)];
    size_t sectorsPerTrack = SECTORS_PER_TRACK[static_cast<uint8_t>(m_dimType)];

    if (track >= maxTrack) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }
    if (sector < 1 || sector > sectorsPerTrack) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }
}

bool X68000DIMImage::isValidDIMType(uint8_t type) {
    return type <= 3 || type == 9;
}

SectorBuffer X68000DIMImage::readSector(size_t track, size_t side, size_t sector) {
    // Convert track/side to linear track
    size_t linearTrack = (track << 1) | (side & 1);

    validateParameters(linearTrack, sector);

    if (!isTrackPresent(linearTrack)) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }

    size_t offset = calculateOffset(linearTrack, sector);
    size_t sectorSize = getSectorSize();

    if (offset + sectorSize > m_data.size()) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }

    return SectorBuffer(m_data.begin() + offset,
                        m_data.begin() + offset + sectorSize);
}

void X68000DIMImage::writeSector(size_t track, size_t side, size_t sector,
                                  const SectorBuffer& data) {
    if (m_writeProtected) {
        throw WriteProtectedException();
    }

    // Convert track/side to linear track
    size_t linearTrack = (track << 1) | (side & 1);

    validateParameters(linearTrack, sector);

    // Mark track as present
    setTrackPresent(linearTrack, true);

    size_t offset = calculateOffset(linearTrack, sector);
    size_t sectorSize = getSectorSize();

    if (offset + sectorSize > m_data.size()) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }

    size_t copySize = std::min(data.size(), sectorSize);
    std::copy(data.begin(), data.begin() + copySize, m_data.begin() + offset);

    if (copySize < sectorSize) {
        std::fill(m_data.begin() + offset + copySize,
                  m_data.begin() + offset + sectorSize, 0xE5);
    }

    m_modified = true;
}

TrackBuffer X68000DIMImage::readTrack(size_t track, size_t side) {
    size_t linearTrack = (track << 1) | (side & 1);
    size_t maxTrack = MAX_TRACKS[static_cast<uint8_t>(m_dimType)];

    if (linearTrack >= maxTrack) {
        throw SectorNotFoundException(static_cast<int>(track), 0);
    }

    size_t trackSize = getTrackSize();
    size_t offset = getDataOffset(linearTrack);

    return TrackBuffer(m_data.begin() + offset,
                       m_data.begin() + offset + trackSize);
}

void X68000DIMImage::writeTrack(size_t track, size_t side, const TrackBuffer& data) {
    if (m_writeProtected) {
        throw WriteProtectedException();
    }

    size_t linearTrack = (track << 1) | (side & 1);
    size_t maxTrack = MAX_TRACKS[static_cast<uint8_t>(m_dimType)];

    if (linearTrack >= maxTrack) {
        throw SectorNotFoundException(static_cast<int>(track), 0);
    }

    setTrackPresent(linearTrack, true);

    size_t trackSize = getTrackSize();
    size_t offset = getDataOffset(linearTrack);
    size_t copySize = std::min(data.size(), trackSize);

    std::copy(data.begin(), data.begin() + copySize, m_data.begin() + offset);

    if (copySize < trackSize) {
        std::fill(m_data.begin() + offset + copySize,
                  m_data.begin() + offset + trackSize, 0xE5);
    }

    m_modified = true;
}

void X68000DIMImage::setDIMType(X68000DIMType type) {
    m_dimType = type;
    m_header.type = static_cast<uint8_t>(type);

    // Update geometry
    size_t sectorSize = SECTOR_SIZES[m_header.type];
    size_t sectorsPerTrack = SECTORS_PER_TRACK[m_header.type];
    size_t maxTracks = MAX_TRACKS[m_header.type];
    initGeometry(maxTracks, 2, sectorsPerTrack, sectorSize);

    m_modified = true;
}

std::string X68000DIMImage::getComment() const {
    // Comment is null-terminated, max 61 chars
    return std::string(m_header.comment, strnlen(m_header.comment, sizeof(m_header.comment)));
}

void X68000DIMImage::setComment(const std::string& comment) {
    std::memset(m_header.comment, 0, sizeof(m_header.comment));
    size_t copyLen = std::min(comment.size(), sizeof(m_header.comment) - 1);
    std::memcpy(m_header.comment, comment.c_str(), copyLen);
    m_modified = true;
}

bool X68000DIMImage::isTrackPresent(size_t track) const {
    if (track >= DIM_MAX_TRACKS) {
        return false;
    }
    return m_header.trkflag[track] != 0;
}

void X68000DIMImage::setTrackPresent(size_t track, bool present) {
    if (track < DIM_MAX_TRACKS) {
        m_header.trkflag[track] = present ? 1 : 0;
        m_modified = true;
    }
}

size_t X68000DIMImage::getSectorSize() const {
    return SECTOR_SIZES[static_cast<uint8_t>(m_dimType)];
}

size_t X68000DIMImage::getSectorsPerTrack() const {
    return SECTORS_PER_TRACK[static_cast<uint8_t>(m_dimType)];
}

size_t X68000DIMImage::getTrackSize() const {
    return TRACK_SIZES[static_cast<uint8_t>(m_dimType)];
}

const char* X68000DIMImage::getDIMTypeName(X68000DIMType type) {
    switch (type) {
        case X68000DIMType::DIM_2HD:  return "2HD (1024x8)";
        case X68000DIMType::DIM_2HS:  return "2HS (1024x9)";
        case X68000DIMType::DIM_2HC:  return "2HC (512x15)";
        case X68000DIMType::DIM_2HDE: return "2HDE (1024x9)";
        case X68000DIMType::DIM_2HQ:  return "2HQ (512x18)";
        default: return "Unknown";
    }
}

bool X68000DIMImage::canConvertTo(DiskFormat format) const {
    switch (format) {
        case DiskFormat::X68000XDF:
            // Can convert to XDF only if 2HD type (same layout)
            return m_dimType == X68000DIMType::DIM_2HD;
        default:
            return false;
    }
}

std::unique_ptr<DiskImage> X68000DIMImage::convertTo(DiskFormat format) const {
    if (!canConvertTo(format)) {
        throw UnsupportedFormatException("Cannot convert DIM to " +
                                         std::string(formatToString(format)));
    }

    if (format == DiskFormat::X68000XDF) {
        auto xdfImage = std::make_unique<X68000XDFImage>();
        xdfImage->create({});

        // Copy sectors from DIM to XDF
        // Only first 154 tracks (XDF limit)
        for (size_t track = 0; track < X68000DiskImage::XDF_TOTAL_TRACKS; ++track) {
            if (isTrackPresent(track)) {
                for (size_t sector = 1; sector <= 8; ++sector) {
                    size_t offset = calculateOffset(track, sector);
                    SectorBuffer sectorData(m_data.begin() + offset,
                                           m_data.begin() + offset + 1024);
                    xdfImage->writeSector(track, 0, sector, sectorData);
                }
            }
        }

        return xdfImage;
    }

    throw NotImplementedException("DIM to " + std::string(formatToString(format)) + " conversion");
}

bool X68000DIMImage::validate() const {
    // Check minimum size
    if (m_data.size() < DIM_HEADER_SIZE) {
        return false;
    }

    // Check valid DIM type
    if (!isValidDIMType(m_header.type)) {
        return false;
    }

    return true;
}

std::string X68000DIMImage::getDiagnostics() const {
    std::ostringstream oss;

    oss << "Format: X68000 DIM (.dim)\n";
    oss << "DIM Type: " << getDIMTypeName(m_dimType) << "\n";
    oss << "Size: " << m_data.size() << " bytes\n";
    oss << "Max Tracks: " << MAX_TRACKS[static_cast<uint8_t>(m_dimType)] << "\n";
    oss << "Sectors/Track: " << getSectorsPerTrack() << "\n";
    oss << "Bytes/Sector: " << getSectorSize() << "\n";
    oss << "Track Size: " << getTrackSize() << " bytes\n";

    // Count present tracks
    size_t presentTracks = 0;
    for (size_t i = 0; i < DIM_MAX_TRACKS; ++i) {
        if (m_header.trkflag[i]) ++presentTracks;
    }
    oss << "Present Tracks: " << presentTracks << "/" << DIM_MAX_TRACKS << "\n";

    std::string comment = getComment();
    if (!comment.empty()) {
        oss << "Comment: " << comment << "\n";
    }

    oss << "Overtrack: " << (m_header.overtrack ? "Yes" : "No") << "\n";

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
