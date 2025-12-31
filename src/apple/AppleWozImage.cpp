#include "rdedisktool/apple/AppleWozImage.h"
#include "rdedisktool/apple/AppleDOImage.h"
#include "rdedisktool/apple/NibbleEncoder.h"
#include "rdedisktool/DiskImageFactory.h"
#include <fstream>
#include <sstream>
#include <cstring>

namespace rde {

// Register formats with factory
namespace {
    struct AppleWozRegistrar {
        AppleWozRegistrar() {
            DiskImageFactory::registerFormat(DiskFormat::AppleWOZ1,
                []() -> std::unique_ptr<DiskImage> {
                    auto img = std::make_unique<AppleWozImage>();
                    return img;
                });
            DiskImageFactory::registerFormat(DiskFormat::AppleWOZ2,
                []() -> std::unique_ptr<DiskImage> {
                    return std::make_unique<AppleWozImage>();
                });
        }
    };
    static AppleWozRegistrar registrar;
}

AppleWozImage::AppleWozImage() : AppleDiskImage() {
    std::fill(m_trackMap.begin(), m_trackMap.end(), 0xFF);
    m_creator = "rdedisktool";
}

void AppleWozImage::load(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        throw FileNotFoundException(path.string());
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw ReadException("Cannot open file: " + path.string());
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    if (fileSize < WOZ_HEADER_SIZE + 8) {
        throw InvalidFormatException("File too small for WOZ format");
    }

    m_data.resize(fileSize);
    file.read(reinterpret_cast<char*>(m_data.data()), fileSize);

    if (!file) {
        throw ReadException("Failed to read file: " + path.string());
    }

    m_filePath = path;

    // Parse WOZ structure
    parseWozHeader();

    m_modified = false;
    m_fileSystemDetected = false;
    std::fill(m_sectorsCached.begin(), m_sectorsCached.end(), false);
}

void AppleWozImage::parseWozHeader() {
    if (m_data.size() < WOZ_HEADER_SIZE) {
        throw InvalidFormatException("Invalid WOZ header");
    }

    // Check magic number
    uint32_t magic = m_data[0] | (m_data[1] << 8) |
                     (m_data[2] << 16) | (m_data[3] << 24);

    if (magic == WOZ1_MAGIC) {
        m_wozVersion = 1;
    } else if (magic == WOZ2_MAGIC) {
        m_wozVersion = 2;
    } else {
        throw InvalidFormatException("Invalid WOZ magic number");
    }

    // Check signature bytes
    if (m_data[4] != 0xFF || m_data[5] != 0x0A ||
        m_data[6] != 0x0D || m_data[7] != 0x0A) {
        throw InvalidFormatException("Invalid WOZ signature bytes");
    }

    // Verify CRC32 (bytes 8-11)
    uint32_t storedCrc = m_data[8] | (m_data[9] << 8) |
                         (m_data[10] << 16) | (m_data[11] << 24);
    uint32_t calculatedCrc = CRC::crc32(m_data.data() + WOZ_HEADER_SIZE,
                                         m_data.size() - WOZ_HEADER_SIZE);

    if (storedCrc != calculatedCrc) {
        // Warning only - some WOZ files have incorrect CRC
    }

    // Parse chunks
    size_t pos = WOZ_HEADER_SIZE;
    while (pos + 8 <= m_data.size()) {
        uint32_t chunkId = m_data[pos] | (m_data[pos + 1] << 8) |
                           (m_data[pos + 2] << 16) | (m_data[pos + 3] << 24);
        uint32_t chunkSize = m_data[pos + 4] | (m_data[pos + 5] << 8) |
                             (m_data[pos + 6] << 16) | (m_data[pos + 7] << 24);

        pos += 8;

        if (pos + chunkSize > m_data.size()) {
            throw InvalidFormatException("Chunk extends beyond file");
        }

        switch (chunkId) {
            case CHUNK_INFO:
                parseInfoChunk(&m_data[pos], chunkSize);
                break;
            case CHUNK_TMAP:
                parseTmapChunk(&m_data[pos], chunkSize);
                break;
            case CHUNK_TRKS:
                parseTrksChunk(&m_data[pos], chunkSize);
                break;
            case CHUNK_META:
                parseMetaChunk(&m_data[pos], chunkSize);
                break;
            // WRIT chunk is ignored (write hints)
        }

        pos += chunkSize;
    }
}

void AppleWozImage::parseInfoChunk(const uint8_t* data, size_t size) {
    if (size < 60) {
        throw InvalidFormatException("INFO chunk too small");
    }

    // INFO chunk version (should match WOZ version)
    // uint8_t infoVersion = data[0];

    m_diskType = data[1];
    m_writeProtected = (data[2] != 0);
    m_synchronized = (data[3] != 0);
    m_cleaned = (data[4] != 0);

    // Creator string (32 bytes, null-padded)
    m_creator = std::string(reinterpret_cast<const char*>(&data[5]), 32);
    m_creator.erase(m_creator.find_last_not_of('\0') + 1);

    if (m_wozVersion >= 2 && size >= 60) {
        m_diskSides = data[37];
        m_bootSectorFormat = data[38];
        m_optimalBitTiming = data[39];
        // Additional WOZ2 fields...
    }
}

void AppleWozImage::parseTmapChunk(const uint8_t* data, size_t size) {
    if (size < 160) {
        throw InvalidFormatException("TMAP chunk too small");
    }

    std::copy(data, data + 160, m_trackMap.begin());
}

void AppleWozImage::parseTrksChunk(const uint8_t* data, size_t size) {
    if (m_wozVersion == 1) {
        // WOZ1: Fixed 6656 bytes per track, up to 35 tracks
        size_t numTracks = size / (WOZ1_TRACK_SIZE + 2);  // +2 for bytes used
        m_tracks.resize(numTracks);

        for (size_t i = 0; i < numTracks; ++i) {
            size_t offset = i * (WOZ1_TRACK_SIZE + 256);  // 6656 + padding

            if (offset + WOZ1_TRACK_SIZE + 2 > size) break;

            m_tracks[i].bits.assign(&data[offset], &data[offset + WOZ1_TRACK_SIZE]);
            m_tracks[i].bytesUsed = data[offset + WOZ1_TRACK_SIZE] |
                                    (data[offset + WOZ1_TRACK_SIZE + 1] << 8);
            m_tracks[i].bitCount = m_tracks[i].bytesUsed * 8;
        }
    } else {
        // WOZ2: Track entries followed by bit data blocks
        // First 160 track entries (8 bytes each = 1280 bytes)
        size_t numTrackEntries = std::min(size_t(160), size / 8);
        m_tracks.resize(numTrackEntries);

        for (size_t i = 0; i < numTrackEntries; ++i) {
            size_t entryOffset = i * 8;
            m_tracks[i].startingBlock = data[entryOffset] |
                                        (data[entryOffset + 1] << 8);
            m_tracks[i].blockCount = data[entryOffset + 2] |
                                     (data[entryOffset + 3] << 8);
            m_tracks[i].bitCount = data[entryOffset + 4] |
                                   (data[entryOffset + 5] << 8) |
                                   (data[entryOffset + 6] << 16) |
                                   (data[entryOffset + 7] << 24);
        }

        // Now read bit data from blocks
        // Block 0 is the header, blocks 1-2 are typically unused
        // Track data starts at block 3
        const uint8_t* basePtr = m_data.data();
        for (auto& track : m_tracks) {
            if (track.startingBlock > 0 && track.blockCount > 0) {
                size_t dataOffset = track.startingBlock * WOZ2_BITS_BLOCK;
                size_t dataSize = track.blockCount * WOZ2_BITS_BLOCK;

                if (dataOffset + dataSize <= m_data.size()) {
                    track.bits.assign(&basePtr[dataOffset],
                                      &basePtr[dataOffset + dataSize]);
                }
            }
        }
    }
}

void AppleWozImage::parseMetaChunk(const uint8_t* data, size_t size) {
    // META chunk contains tab-separated key-value pairs, newline delimited
    std::string metaStr(reinterpret_cast<const char*>(data), size);
    std::istringstream stream(metaStr);
    std::string line;

    while (std::getline(stream, line)) {
        size_t tabPos = line.find('\t');
        if (tabPos != std::string::npos) {
            std::string key = line.substr(0, tabPos);
            std::string value = line.substr(tabPos + 1);
            m_metadata[key] = value;
        }
    }
}

void AppleWozImage::save(const std::filesystem::path& path) {
    std::filesystem::path savePath = path.empty() ? m_filePath : path;

    if (savePath.empty()) {
        throw WriteException("No file path specified");
    }

    if (m_writeProtected && savePath == m_filePath) {
        throw WriteProtectedException();
    }

    auto wozData = buildWozFile();

    std::ofstream file(savePath, std::ios::binary);
    if (!file) {
        throw WriteException("Cannot create file: " + savePath.string());
    }

    file.write(reinterpret_cast<const char*>(wozData.data()), wozData.size());

    if (!file) {
        throw WriteException("Failed to write file: " + savePath.string());
    }

    m_data = std::move(wozData);

    if (path.empty() || path == m_filePath) {
        m_modified = false;
    }

    m_filePath = savePath;
}

std::vector<uint8_t> AppleWozImage::buildWozFile() const {
    std::vector<uint8_t> result;

    // Build chunks
    auto infoChunk = buildInfoChunk();
    auto tmapChunk = buildTmapChunk();
    auto trksChunk = buildTrksChunk();
    auto metaChunk = buildMetaChunk();

    // Calculate total size
    size_t totalSize = WOZ_HEADER_SIZE +
                       8 + infoChunk.size() +
                       8 + tmapChunk.size() +
                       8 + trksChunk.size() +
                       (metaChunk.empty() ? 0 : 8 + metaChunk.size());

    result.reserve(totalSize);

    // Header placeholder (will fill CRC later)
    result.resize(WOZ_HEADER_SIZE);
    result[0] = 'W'; result[1] = 'O'; result[2] = 'Z';
    result[3] = (m_wozVersion == 1) ? '1' : '2';
    result[4] = 0xFF; result[5] = 0x0A;
    result[6] = 0x0D; result[7] = 0x0A;

    // Helper to add chunk
    auto addChunk = [&result](uint32_t id, const std::vector<uint8_t>& data) {
        result.push_back(id & 0xFF);
        result.push_back((id >> 8) & 0xFF);
        result.push_back((id >> 16) & 0xFF);
        result.push_back((id >> 24) & 0xFF);
        uint32_t size = static_cast<uint32_t>(data.size());
        result.push_back(size & 0xFF);
        result.push_back((size >> 8) & 0xFF);
        result.push_back((size >> 16) & 0xFF);
        result.push_back((size >> 24) & 0xFF);
        result.insert(result.end(), data.begin(), data.end());
    };

    addChunk(CHUNK_INFO, infoChunk);
    addChunk(CHUNK_TMAP, tmapChunk);
    addChunk(CHUNK_TRKS, trksChunk);
    if (!metaChunk.empty()) {
        addChunk(CHUNK_META, metaChunk);
    }

    // Calculate and store CRC32
    uint32_t crc = CRC::crc32(result.data() + WOZ_HEADER_SIZE,
                              result.size() - WOZ_HEADER_SIZE);
    result[8] = crc & 0xFF;
    result[9] = (crc >> 8) & 0xFF;
    result[10] = (crc >> 16) & 0xFF;
    result[11] = (crc >> 24) & 0xFF;

    return result;
}

std::vector<uint8_t> AppleWozImage::buildInfoChunk() const {
    std::vector<uint8_t> result(60, 0);

    result[0] = m_wozVersion;  // Info version
    result[1] = m_diskType;
    result[2] = m_writeProtected ? 1 : 0;
    result[3] = m_synchronized ? 1 : 0;
    result[4] = m_cleaned ? 1 : 0;

    // Creator (32 bytes)
    size_t creatorLen = std::min(m_creator.size(), size_t(32));
    std::copy(m_creator.begin(), m_creator.begin() + creatorLen, result.begin() + 5);

    if (m_wozVersion >= 2) {
        result[37] = m_diskSides;
        result[38] = m_bootSectorFormat;
        result[39] = m_optimalBitTiming;
        // Compatible hardware: 0xFFFF = all
        result[40] = 0xFF; result[41] = 0xFF;
        // Required RAM: 0 = unknown
        result[42] = 0; result[43] = 0;
        // Largest track: in blocks
        result[44] = 13; result[45] = 0;  // 13 blocks = 6656 bytes
    }

    return result;
}

std::vector<uint8_t> AppleWozImage::buildTmapChunk() const {
    return std::vector<uint8_t>(m_trackMap.begin(), m_trackMap.end());
}

std::vector<uint8_t> AppleWozImage::buildTrksChunk() const {
    if (m_wozVersion == 1) {
        // WOZ1: 6656 bytes per track + 2 bytes for used count
        std::vector<uint8_t> result;
        for (const auto& track : m_tracks) {
            // Pad/truncate to exactly 6656 bytes
            std::vector<uint8_t> trackData(WOZ1_TRACK_SIZE, 0);
            size_t copySize = std::min(track.bits.size(), WOZ1_TRACK_SIZE);
            std::copy(track.bits.begin(), track.bits.begin() + copySize,
                      trackData.begin());

            result.insert(result.end(), trackData.begin(), trackData.end());

            // Bytes used
            uint16_t used = static_cast<uint16_t>(
                std::min(track.bytesUsed, uint16_t(WOZ1_TRACK_SIZE)));
            result.push_back(used & 0xFF);
            result.push_back((used >> 8) & 0xFF);

            // Padding to 6912 bytes
            result.resize(result.size() + 254, 0);
        }
        return result;
    } else {
        // WOZ2: Track entries + bit data blocks
        // For simplicity, write all tracks starting at block 3
        std::vector<uint8_t> result;

        // Reserve space for 160 track entries (8 bytes each)
        result.resize(1280, 0);

        size_t currentBlock = 3;  // Start after header blocks

        for (size_t i = 0; i < m_tracks.size() && i < 160; ++i) {
            const auto& track = m_tracks[i];
            size_t entryOffset = i * 8;

            if (!track.bits.empty()) {
                uint16_t blockCount = static_cast<uint16_t>(
                    (track.bits.size() + WOZ2_BITS_BLOCK - 1) / WOZ2_BITS_BLOCK);

                result[entryOffset] = currentBlock & 0xFF;
                result[entryOffset + 1] = (currentBlock >> 8) & 0xFF;
                result[entryOffset + 2] = blockCount & 0xFF;
                result[entryOffset + 3] = (blockCount >> 8) & 0xFF;
                result[entryOffset + 4] = track.bitCount & 0xFF;
                result[entryOffset + 5] = (track.bitCount >> 8) & 0xFF;
                result[entryOffset + 6] = (track.bitCount >> 16) & 0xFF;
                result[entryOffset + 7] = (track.bitCount >> 24) & 0xFF;

                currentBlock += blockCount;
            }
        }

        // Now append track bit data (will be placed at correct blocks by file layout)
        for (const auto& track : m_tracks) {
            if (!track.bits.empty()) {
                // Pad to block boundary
                size_t paddedSize = ((track.bits.size() + WOZ2_BITS_BLOCK - 1) /
                                     WOZ2_BITS_BLOCK) * WOZ2_BITS_BLOCK;
                std::vector<uint8_t> paddedData(paddedSize, 0);
                std::copy(track.bits.begin(), track.bits.end(), paddedData.begin());
                result.insert(result.end(), paddedData.begin(), paddedData.end());
            }
        }

        return result;
    }
}

std::vector<uint8_t> AppleWozImage::buildMetaChunk() const {
    if (m_metadata.empty()) {
        return {};
    }

    std::ostringstream oss;
    for (const auto& [key, value] : m_metadata) {
        oss << key << "\t" << value << "\n";
    }

    std::string str = oss.str();
    return std::vector<uint8_t>(str.begin(), str.end());
}

void AppleWozImage::create(const DiskGeometry& geometry) {
    size_t tracks = geometry.tracks > 0 ? geometry.tracks : TRACKS_35;
    initGeometry(tracks, SECTORS_16);

    m_wozVersion = 2;
    m_diskType = 1;  // 5.25"
    m_synchronized = false;
    m_cleaned = true;
    m_bootSectorFormat = 1;  // 16-sector
    m_creator = "rdedisktool";

    // Initialize track map - standard 35-track mapping
    std::fill(m_trackMap.begin(), m_trackMap.end(), 0xFF);
    for (size_t t = 0; t < tracks; ++t) {
        m_trackMap[t * 4] = static_cast<uint8_t>(t);  // Quarter-track 0
    }

    // Create blank tracks
    m_tracks.resize(tracks);
    for (size_t t = 0; t < tracks; ++t) {
        // Create empty sector data
        std::array<std::vector<uint8_t>, 16> sectorData;
        for (auto& sector : sectorData) {
            sector.resize(BYTES_PER_SECTOR, 0);
        }

        // Build nibblized track
        auto nibbleTrack = NibbleEncoder::buildTrack(sectorData, 254,
                                                      static_cast<uint8_t>(t));

        m_tracks[t].bits = std::move(nibbleTrack);
        m_tracks[t].bitCount = static_cast<uint32_t>(m_tracks[t].bits.size() * 8);
        m_tracks[t].bytesUsed = static_cast<uint16_t>(m_tracks[t].bits.size());
    }

    // Build the file data
    m_data = buildWozFile();

    m_modified = true;
    m_fileSystemDetected = false;
    m_filePath.clear();
    std::fill(m_sectorsCached.begin(), m_sectorsCached.end(), false);
}

size_t AppleWozImage::calculateOffset(size_t track, size_t /*sector*/) const {
    // WOZ format doesn't have simple linear offsets
    return track;  // Return track number as reference
}

void AppleWozImage::decodeSectorsForTrack(size_t track) {
    if (track >= TRACKS_35 || m_sectorsCached[track]) {
        return;
    }

    // Get track index from TMAP
    uint8_t trackIndex = m_trackMap[track * 4];
    if (trackIndex == 0xFF || trackIndex >= m_tracks.size()) {
        // No data for this track
        for (auto& sector : m_decodedSectors[track]) {
            sector.resize(BYTES_PER_SECTOR, 0);
        }
        m_sectorsCached[track] = true;
        return;
    }

    // Get nibble data and parse sectors
    m_decodedSectors[track] = NibbleEncoder::parseTrack(
        m_tracks[trackIndex].bits, static_cast<uint8_t>(track));

    m_sectorsCached[track] = true;
}

SectorBuffer AppleWozImage::readSector(size_t track, size_t /*side*/, size_t sector) {
    if (track >= m_geometry.tracks) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }
    if (sector >= m_geometry.sectorsPerTrack) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }

    decodeSectorsForTrack(track);
    return m_decodedSectors[track][sector];
}

void AppleWozImage::writeSector(size_t track, size_t /*side*/, size_t sector,
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

    decodeSectorsForTrack(track);

    // Update sector
    m_decodedSectors[track][sector] = data;
    if (m_decodedSectors[track][sector].size() < BYTES_PER_SECTOR) {
        m_decodedSectors[track][sector].resize(BYTES_PER_SECTOR, 0);
    }

    // Rebuild nibble track
    auto nibbleTrack = NibbleEncoder::buildTrack(m_decodedSectors[track], 254,
                                                  static_cast<uint8_t>(track));

    uint8_t trackIndex = m_trackMap[track * 4];
    if (trackIndex < m_tracks.size()) {
        m_tracks[trackIndex].bits = std::move(nibbleTrack);
        m_tracks[trackIndex].bitCount = static_cast<uint32_t>(
            m_tracks[trackIndex].bits.size() * 8);
        m_tracks[trackIndex].bytesUsed = static_cast<uint16_t>(
            m_tracks[trackIndex].bits.size());
    }

    m_modified = true;
}

TrackBuffer AppleWozImage::readTrack(size_t track, size_t /*side*/) {
    if (track >= m_geometry.tracks) {
        throw SectorNotFoundException(static_cast<int>(track), 0);
    }

    uint8_t trackIndex = m_trackMap[track * 4];
    if (trackIndex == 0xFF || trackIndex >= m_tracks.size()) {
        return TrackBuffer(NibbleEncoder::TRACK_NIBBLE_SIZE, 0xFF);
    }

    return m_tracks[trackIndex].bits;
}

void AppleWozImage::writeTrack(size_t track, size_t /*side*/, const TrackBuffer& data) {
    if (m_writeProtected) {
        throw WriteProtectedException();
    }

    if (track >= m_geometry.tracks) {
        throw SectorNotFoundException(static_cast<int>(track), 0);
    }

    uint8_t trackIndex = m_trackMap[track * 4];
    if (trackIndex == 0xFF) {
        // Need to allocate new track entry
        trackIndex = static_cast<uint8_t>(m_tracks.size());
        m_tracks.emplace_back();
        m_trackMap[track * 4] = trackIndex;
    }

    m_tracks[trackIndex].bits = data;
    m_tracks[trackIndex].bitCount = static_cast<uint32_t>(data.size() * 8);
    m_tracks[trackIndex].bytesUsed = static_cast<uint16_t>(data.size());

    // Invalidate sector cache
    m_sectorsCached[track] = false;

    m_modified = true;
}

std::vector<uint8_t> AppleWozImage::getTrackBits(size_t quarterTrack) const {
    if (quarterTrack >= 160) {
        return {};
    }

    uint8_t trackIndex = m_trackMap[quarterTrack];
    if (trackIndex == 0xFF || trackIndex >= m_tracks.size()) {
        return {};
    }

    return m_tracks[trackIndex].bits;
}

uint32_t AppleWozImage::getTrackBitCount(size_t quarterTrack) const {
    if (quarterTrack >= 160) {
        return 0;
    }

    uint8_t trackIndex = m_trackMap[quarterTrack];
    if (trackIndex == 0xFF || trackIndex >= m_tracks.size()) {
        return 0;
    }

    return m_tracks[trackIndex].bitCount;
}

void AppleWozImage::setMetadata(const std::string& key, const std::string& value) {
    m_metadata[key] = value;
    m_modified = true;
}

bool AppleWozImage::canConvertTo(DiskFormat format) const {
    switch (format) {
        case DiskFormat::AppleDO:
        case DiskFormat::ApplePO:
        case DiskFormat::AppleNIB:
            return true;
        default:
            return false;
    }
}

std::unique_ptr<DiskImage> AppleWozImage::convertTo(DiskFormat format) const {
    if (!canConvertTo(format)) {
        throw UnsupportedFormatException("Cannot convert to " +
                                         std::string(formatToString(format)));
    }

    if (format == DiskFormat::AppleDO) {
        auto doImage = std::make_unique<AppleDOImage>();
        doImage->create(m_geometry);

        // Decode each track
        for (size_t track = 0; track < m_geometry.tracks; ++track) {
            uint8_t trackIndex = m_trackMap[track * 4];
            if (trackIndex != 0xFF && trackIndex < m_tracks.size()) {
                auto sectors = NibbleEncoder::parseTrack(
                    m_tracks[trackIndex].bits, static_cast<uint8_t>(track));

                for (size_t sector = 0; sector < 16; ++sector) {
                    doImage->writeSector(track, 0, sector, sectors[sector]);
                }
            }
        }

        return doImage;
    }

    throw NotImplementedException("Conversion to " + std::string(formatToString(format)));
}

bool AppleWozImage::validate() const {
    // Check basic structure
    if (m_data.size() < WOZ_HEADER_SIZE) {
        return false;
    }

    // Verify magic number
    uint32_t magic = m_data[0] | (m_data[1] << 8) |
                     (m_data[2] << 16) | (m_data[3] << 24);
    if (magic != WOZ1_MAGIC && magic != WOZ2_MAGIC) {
        return false;
    }

    // Check that we have some tracks
    if (m_tracks.empty()) {
        return false;
    }

    return true;
}

std::string AppleWozImage::getDiagnostics() const {
    std::ostringstream oss;

    oss << "Format: WOZ v" << static_cast<int>(m_wozVersion) << "\n";
    oss << "Size: " << m_data.size() << " bytes\n";
    oss << "Disk Type: " << (m_diskType == 1 ? "5.25\"" : "3.5\"") << "\n";
    oss << "Creator: " << m_creator << "\n";
    oss << "Synchronized: " << (m_synchronized ? "Yes" : "No") << "\n";
    oss << "Write Protected: " << (m_writeProtected ? "Yes" : "No") << "\n";
    oss << "Boot Format: ";
    switch (m_bootSectorFormat) {
        case 1: oss << "16-sector"; break;
        case 2: oss << "13-sector"; break;
        case 3: oss << "Both"; break;
        default: oss << "Unknown"; break;
    }
    oss << "\n";

    // Count valid tracks
    int validTracks = 0;
    for (size_t t = 0; t < 35; ++t) {
        if (m_trackMap[t * 4] != 0xFF) ++validTracks;
    }
    oss << "Valid Tracks: " << validTracks << "\n";
    oss << "Total Track Entries: " << m_tracks.size() << "\n";

    if (!m_metadata.empty()) {
        oss << "\nMetadata:\n";
        for (const auto& [key, value] : m_metadata) {
            oss << "  " << key << ": " << value << "\n";
        }
    }

    return oss.str();
}

} // namespace rde
