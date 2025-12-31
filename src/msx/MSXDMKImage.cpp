#include "rdedisktool/msx/MSXDMKImage.h"
#include "rdedisktool/msx/MSXDSKImage.h"
#include "rdedisktool/DiskImageFactory.h"
#include "rdedisktool/CRC.h"
#include <fstream>
#include <sstream>
#include <cstring>

namespace rde {

// Register format with factory
namespace {
    struct MSXDMKRegistrar {
        MSXDMKRegistrar() {
            DiskImageFactory::registerFormat(DiskFormat::MSXDMK,
                []() -> std::unique_ptr<DiskImage> {
                    return std::make_unique<MSXDMKImage>();
                });
        }
    };
    static MSXDMKRegistrar registrar;
}

MSXDMKImage::MSXDMKImage() : MSXDiskImage() {
}

void MSXDMKImage::load(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        throw FileNotFoundException(path.string());
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw ReadException("Cannot open file: " + path.string());
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    if (fileSize < DMK_HEADER_SIZE) {
        throw InvalidFormatException("File too small for DMK format");
    }

    m_data.resize(fileSize);
    file.read(reinterpret_cast<char*>(m_data.data()), fileSize);

    if (!file) {
        throw ReadException("Failed to read file: " + path.string());
    }

    // Parse DMK header
    m_writeProtected = (m_data[0] == 0xFF);
    m_numTracks = m_data[1];
    m_trackLength = m_data[2] | (m_data[3] << 8);
    m_dmkFlags = m_data[4];

    // Validate header
    if (m_numTracks == 0 || m_numTracks > 86 ||
        m_trackLength < 128 || m_trackLength > 16384) {
        throw InvalidFormatException("Invalid DMK header");
    }

    // Set geometry from header
    size_t sides = isSingleSided() ? 1 : 2;
    initGeometry(m_numTracks, sides, SECTORS_9);

    m_filePath = path;
    m_modified = false;
    m_fileSystemDetected = false;
}

void MSXDMKImage::save(const std::filesystem::path& path) {
    std::filesystem::path savePath = path.empty() ? m_filePath : path;

    if (savePath.empty()) {
        throw WriteException("No file path specified");
    }

    if (m_writeProtected && savePath == m_filePath) {
        throw WriteProtectedException();
    }

    // Update header
    m_data[0] = m_writeProtected ? 0xFF : 0x00;
    m_data[1] = m_numTracks;
    m_data[2] = m_trackLength & 0xFF;
    m_data[3] = (m_trackLength >> 8) & 0xFF;
    m_data[4] = m_dmkFlags;

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

void MSXDMKImage::create(const DiskGeometry& geometry) {
    size_t tracks = geometry.tracks > 0 ? geometry.tracks : TRACKS_80;
    size_t sides = geometry.sides > 0 ? geometry.sides : SIDES_2;
    size_t sectors = geometry.sectorsPerTrack > 0 ? geometry.sectorsPerTrack : SECTORS_9;

    initGeometry(tracks, sides, sectors);

    m_numTracks = static_cast<uint8_t>(tracks);
    m_trackLength = DMK_TRACK_LENGTH_DD;
    m_dmkFlags = (sides == 1) ? 0x10 : 0x00;  // Set single-sided flag if needed

    // Calculate file size: header + (tracks * sides * track_length)
    size_t fileSize = DMK_HEADER_SIZE + (tracks * sides * m_trackLength);
    m_data.resize(fileSize, 0);

    // Initialize header
    m_data[0] = 0x00;  // Not write protected
    m_data[1] = m_numTracks;
    m_data[2] = m_trackLength & 0xFF;
    m_data[3] = (m_trackLength >> 8) & 0xFF;
    m_data[4] = m_dmkFlags;
    // Bytes 5-11 reserved (zero)
    // Bytes 12-15 for virtual disk flag (zero for real disk)

    // Initialize each track with formatted data
    for (size_t track = 0; track < tracks; ++track) {
        for (size_t side = 0; side < sides; ++side) {
            // Create empty sectors
            std::array<SectorBuffer, 9> sectorData;
            for (auto& sector : sectorData) {
                sector.resize(BYTES_PER_SECTOR, 0xE5);  // Fill with 0xE5 (formatted)
            }

            // Build formatted track
            auto trackData = buildFormattedTrack(track, side, sectorData);

            // Copy to disk image
            size_t offset = getTrackOffset(track, side);
            std::copy(trackData.begin(), trackData.end(), m_data.begin() + offset);
        }
    }

    m_modified = true;
    m_fileSystemDetected = false;
    m_filePath.clear();
}

size_t MSXDMKImage::getTrackOffset(size_t track, size_t side) const {
    // Calculate track index
    size_t trackIndex;
    if (isSingleSided()) {
        trackIndex = track;
    } else {
        // Interleaved: track 0 side 0, track 0 side 1, track 1 side 0, ...
        trackIndex = track * 2 + side;
    }

    return DMK_HEADER_SIZE + (trackIndex * m_trackLength);
}

size_t MSXDMKImage::calculateOffset(size_t track, size_t side, size_t sector) const {
    // DMK stores raw track data; this just returns track offset
    return getTrackOffset(track, side);
}

MSXDMKImage::DMKHeader MSXDMKImage::getDMKHeader() const {
    return {
        m_data[0],
        m_data[1],
        static_cast<uint16_t>(m_data[2] | (m_data[3] << 8)),
        m_data[4]
    };
}

bool MSXDMKImage::isSingleSided() const {
    return (m_dmkFlags & 0x10) != 0;
}

bool MSXDMKImage::isSingleDensity() const {
    return (m_dmkFlags & 0x40) != 0;
}

std::array<uint16_t, MSXDMKImage::DMK_IDAM_COUNT> MSXDMKImage::getIDAMTable(
    size_t track, size_t side) const {

    std::array<uint16_t, DMK_IDAM_COUNT> result = {};

    size_t offset = getTrackOffset(track, side);
    if (offset + DMK_IDAM_SIZE > m_data.size()) {
        return result;
    }

    for (size_t i = 0; i < DMK_IDAM_COUNT; ++i) {
        result[i] = m_data[offset + i * 2] | (m_data[offset + i * 2 + 1] << 8);
    }

    return result;
}

MSXDMKImage::IDAMEntry MSXDMKImage::parseIDAM(const uint8_t* data, uint16_t idamPtr) const {
    IDAMEntry entry = {};

    // IDAM pointer format:
    // Bit 15: Double density flag (1 = DD, 0 = SD)
    // Bit 14: Unused
    // Bits 13-0: Offset to ID Address Mark within track

    entry.doubleDensity = (idamPtr & 0x8000) != 0;
    entry.offset = idamPtr & 0x3FFF;

    if (entry.offset < DMK_IDAM_SIZE || entry.offset + 6 > m_trackLength) {
        return entry;
    }

    // Read IDAM fields (after 0xFE mark)
    // Structure: FE track head sector size CRC1 CRC2
    entry.track = data[entry.offset + 1];
    entry.side = data[entry.offset + 2];
    entry.sector = data[entry.offset + 3];
    entry.sizeCode = data[entry.offset + 4];

    return entry;
}

int MSXDMKImage::findSectorInTrack(const TrackBuffer& trackData, size_t sector) const {
    // Search IDAM table for matching sector
    for (size_t i = 0; i < DMK_IDAM_COUNT; ++i) {
        uint16_t idamPtr = trackData[i * 2] | (trackData[i * 2 + 1] << 8);

        if (idamPtr == 0) {
            break;  // End of IDAM table
        }

        auto entry = parseIDAM(trackData.data(), idamPtr);

        if (entry.sector == sector) {
            // Found sector; data follows IDAM + CRC + gap + DAM
            // Look for Data Address Mark (0xFB) after IDAM
            size_t searchStart = entry.offset + 7;  // After IDAM + CRC
            size_t searchEnd = std::min(searchStart + 50, trackData.size());

            for (size_t j = searchStart; j < searchEnd; ++j) {
                if (trackData[j] == 0xFB || trackData[j] == 0xF8) {
                    // Found DAM, data starts at next byte
                    return static_cast<int>(j + 1);
                }
            }
        }
    }

    return -1;  // Sector not found
}

SectorBuffer MSXDMKImage::readSector(size_t track, size_t side, size_t sector) {
    if (track >= m_geometry.tracks) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }
    if (side >= m_geometry.sides) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }

    // Read track data
    size_t offset = getTrackOffset(track, side);
    if (offset + m_trackLength > m_data.size()) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }

    TrackBuffer trackData(m_data.begin() + offset,
                          m_data.begin() + offset + m_trackLength);

    // Find sector within track (1-based sector number for MSX)
    int dataOffset = findSectorInTrack(trackData, sector + 1);
    if (dataOffset < 0 || dataOffset + BYTES_PER_SECTOR > static_cast<int>(m_trackLength)) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }

    return SectorBuffer(trackData.begin() + dataOffset,
                        trackData.begin() + dataOffset + BYTES_PER_SECTOR);
}

void MSXDMKImage::writeSector(size_t track, size_t side, size_t sector,
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

    // Read track data
    size_t trackOffset = getTrackOffset(track, side);
    if (trackOffset + m_trackLength > m_data.size()) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }

    TrackBuffer trackData(m_data.begin() + trackOffset,
                          m_data.begin() + trackOffset + m_trackLength);

    // Find sector within track (1-based sector number for MSX)
    int dataOffset = findSectorInTrack(trackData, sector + 1);
    if (dataOffset < 0 || dataOffset + BYTES_PER_SECTOR > static_cast<int>(m_trackLength)) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }

    // Copy sector data
    size_t copySize = std::min(data.size(), BYTES_PER_SECTOR);
    std::copy(data.begin(), data.begin() + copySize,
              m_data.begin() + trackOffset + dataOffset);

    // TODO: Update CRC

    m_modified = true;
}

TrackBuffer MSXDMKImage::readTrack(size_t track, size_t side) {
    if (track >= m_geometry.tracks || side >= m_geometry.sides) {
        throw SectorNotFoundException(static_cast<int>(track), 0);
    }

    size_t offset = getTrackOffset(track, side);
    if (offset + m_trackLength > m_data.size()) {
        throw SectorNotFoundException(static_cast<int>(track), 0);
    }

    return TrackBuffer(m_data.begin() + offset,
                       m_data.begin() + offset + m_trackLength);
}

void MSXDMKImage::writeTrack(size_t track, size_t side, const TrackBuffer& data) {
    if (m_writeProtected) {
        throw WriteProtectedException();
    }

    if (track >= m_geometry.tracks || side >= m_geometry.sides) {
        throw SectorNotFoundException(static_cast<int>(track), 0);
    }

    size_t offset = getTrackOffset(track, side);
    if (offset + m_trackLength > m_data.size()) {
        throw SectorNotFoundException(static_cast<int>(track), 0);
    }

    size_t copySize = std::min(data.size(), size_t(m_trackLength));
    std::copy(data.begin(), data.begin() + copySize, m_data.begin() + offset);

    if (copySize < m_trackLength) {
        std::fill(m_data.begin() + offset + copySize,
                  m_data.begin() + offset + m_trackLength, 0);
    }

    m_modified = true;
}

TrackBuffer MSXDMKImage::buildFormattedTrack(size_t track, size_t side,
                                              const std::array<SectorBuffer, 9>& sectors) {
    TrackBuffer result(m_trackLength, 0);

    // IDAM table (128 bytes)
    size_t idamIdx = 0;
    size_t dataPos = DMK_IDAM_SIZE;

    // Gap 4a (80 bytes of 0x4E)
    std::fill(result.begin() + dataPos, result.begin() + dataPos + 80, 0x4E);
    dataPos += 80;

    // Sync (12 bytes of 0x00)
    std::fill(result.begin() + dataPos, result.begin() + dataPos + 12, 0x00);
    dataPos += 12;

    // Index mark (4 bytes)
    result[dataPos++] = 0xC2;
    result[dataPos++] = 0xC2;
    result[dataPos++] = 0xC2;
    result[dataPos++] = 0xFC;

    // Gap 1 (50 bytes of 0x4E)
    std::fill(result.begin() + dataPos, result.begin() + dataPos + 50, 0x4E);
    dataPos += 50;

    // Write sectors
    for (size_t sect = 0; sect < 9 && idamIdx < DMK_IDAM_COUNT; ++sect) {
        // Record IDAM position (with DD flag)
        uint16_t idamPtr = 0x8000 | static_cast<uint16_t>(dataPos);  // DD flag set
        result[idamIdx * 2] = idamPtr & 0xFF;
        result[idamIdx * 2 + 1] = (idamPtr >> 8) & 0xFF;
        ++idamIdx;

        // Sync (12 bytes of 0x00)
        std::fill(result.begin() + dataPos, result.begin() + dataPos + 12, 0x00);
        dataPos += 12;

        // ID Address Mark
        result[dataPos++] = 0xA1;  // Sync mark
        result[dataPos++] = 0xA1;
        result[dataPos++] = 0xA1;
        result[dataPos++] = 0xFE;  // IDAM

        // ID field: track, side, sector (1-based), size code
        result[dataPos++] = static_cast<uint8_t>(track);
        result[dataPos++] = static_cast<uint8_t>(side);
        result[dataPos++] = static_cast<uint8_t>(sect + 1);  // 1-based
        result[dataPos++] = 0x02;  // 512 bytes

        // ID CRC (placeholder - would calculate real CRC)
        uint16_t idCrc = CRC::crc16_ccitt(&result[dataPos - 8], 8);
        result[dataPos++] = (idCrc >> 8) & 0xFF;
        result[dataPos++] = idCrc & 0xFF;

        // Gap 2 (22 bytes of 0x4E)
        std::fill(result.begin() + dataPos, result.begin() + dataPos + 22, 0x4E);
        dataPos += 22;

        // Sync (12 bytes of 0x00)
        std::fill(result.begin() + dataPos, result.begin() + dataPos + 12, 0x00);
        dataPos += 12;

        // Data Address Mark
        result[dataPos++] = 0xA1;
        result[dataPos++] = 0xA1;
        result[dataPos++] = 0xA1;
        result[dataPos++] = 0xFB;  // DAM

        // Sector data
        const auto& sectorData = sectors[sect];
        size_t dataSize = std::min(sectorData.size(), BYTES_PER_SECTOR);
        std::copy(sectorData.begin(), sectorData.begin() + dataSize,
                  result.begin() + dataPos);
        if (dataSize < BYTES_PER_SECTOR) {
            std::fill(result.begin() + dataPos + dataSize,
                      result.begin() + dataPos + BYTES_PER_SECTOR, 0);
        }
        dataPos += BYTES_PER_SECTOR;

        // Data CRC
        uint16_t dataCrc = CRC::crc16_ccitt(&result[dataPos - BYTES_PER_SECTOR - 4],
                                            BYTES_PER_SECTOR + 4);
        result[dataPos++] = (dataCrc >> 8) & 0xFF;
        result[dataPos++] = dataCrc & 0xFF;

        // Gap 3 (54 bytes of 0x4E)
        std::fill(result.begin() + dataPos, result.begin() + dataPos + 54, 0x4E);
        dataPos += 54;
    }

    // Gap 4b (fill rest with 0x4E)
    std::fill(result.begin() + dataPos, result.end(), 0x4E);

    return result;
}

bool MSXDMKImage::canConvertTo(DiskFormat format) const {
    switch (format) {
        case DiskFormat::MSXDSK:
            return true;
        default:
            return false;
    }
}

std::unique_ptr<DiskImage> MSXDMKImage::convertTo(DiskFormat format) const {
    if (!canConvertTo(format)) {
        throw UnsupportedFormatException("Cannot convert to " +
                                         std::string(formatToString(format)));
    }

    if (format == DiskFormat::MSXDSK) {
        auto dskImage = std::make_unique<MSXDSKImage>();
        dskImage->create(m_geometry);

        // Copy all sectors
        for (size_t track = 0; track < m_geometry.tracks; ++track) {
            for (size_t side = 0; side < m_geometry.sides; ++side) {
                for (size_t sector = 0; sector < m_geometry.sectorsPerTrack; ++sector) {
                    try {
                        auto sectorData = const_cast<MSXDMKImage*>(this)->readSector(
                            track, side, sector);
                        dskImage->writeSector(track, side, sector, sectorData);
                    } catch (const SectorNotFoundException&) {
                        // Write zeros for missing sectors
                        SectorBuffer zeros(BYTES_PER_SECTOR, 0);
                        dskImage->writeSector(track, side, sector, zeros);
                    }
                }
            }
        }

        return dskImage;
    }

    throw NotImplementedException("Conversion to " + std::string(formatToString(format)));
}

bool MSXDMKImage::validate() const {
    // Check header
    if (m_data.size() < DMK_HEADER_SIZE) {
        return false;
    }

    // Validate header values
    if (m_numTracks == 0 || m_numTracks > 86) {
        return false;
    }

    if (m_trackLength < 128 || m_trackLength > 16384) {
        return false;
    }

    // Check file size
    size_t expectedSize = DMK_HEADER_SIZE +
        (m_numTracks * (isSingleSided() ? 1 : 2) * m_trackLength);

    if (m_data.size() < expectedSize) {
        return false;
    }

    return true;
}

std::string MSXDMKImage::getDiagnostics() const {
    std::ostringstream oss;

    oss << "Format: MSX DMK (.dmk)\n";
    oss << "Size: " << m_data.size() << " bytes\n";
    oss << "Tracks: " << static_cast<int>(m_numTracks) << "\n";
    oss << "Track Length: " << m_trackLength << " bytes\n";
    oss << "Sides: " << (isSingleSided() ? 1 : 2) << "\n";
    oss << "Density: " << (isSingleDensity() ? "Single" : "Double") << "\n";
    oss << "Flags: 0x" << std::hex << static_cast<int>(m_dmkFlags) << std::dec << "\n";
    oss << "Write Protected: " << (m_writeProtected ? "Yes" : "No") << "\n";
    oss << "Modified: " << (m_modified ? "Yes" : "No") << "\n";

    return oss.str();
}

} // namespace rde
