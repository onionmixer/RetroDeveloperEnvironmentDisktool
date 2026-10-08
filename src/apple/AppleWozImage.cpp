#include "rdedisktool/apple/AppleWozImage.h"
#include "rdedisktool/apple/AppleDOImage.h"
#include "rdedisktool/apple/NibbleEncoder.h"
#include "rdedisktool/DiskImageFactory.h"
#include <algorithm>
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

    uint16_t readU16(const uint8_t* p) {
        return static_cast<uint16_t>(p[0] | (p[1] << 8));
    }
    uint32_t readU32(const uint8_t* p) {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }
    void putU16(std::vector<uint8_t>& v, size_t at, uint16_t x) {
        v[at] = x & 0xFF;
        v[at + 1] = (x >> 8) & 0xFF;
    }
    void putU32(std::vector<uint8_t>& v, size_t at, uint32_t x) {
        for (int i = 0; i < 4; ++i) v[at + i] = (x >> (8 * i)) & 0xFF;
    }

    constexpr size_t INFO_SIZE = 60;
    constexpr size_t TMAP_SIZE = 160;
    constexpr size_t TRK_ENTRIES = 160;
}

AppleWozImage::AppleWozImage() : AppleDiskImage() {
    std::fill(m_trackMap.begin(), m_trackMap.end(), 0xFF);
    std::fill(m_fluxMap.begin(), m_fluxMap.end(), 0xFF);
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

    // Reset state from any previous image
    m_infoRaw.clear();
    m_tracks.clear();
    m_metadata.clear();
    m_otherChunks.clear();
    std::fill(m_trackMap.begin(), m_trackMap.end(), 0xFF);
    std::fill(m_fluxMap.begin(), m_fluxMap.end(), 0xFF);
    m_hasFlux = false;
    m_tracksChanged = false;

    // Parse WOZ structure
    parseWozHeader();

    // 13-sector (DOS 3.2) or 16-sector tracks, decided by track 0's content
    m_sectors13 = false;
    {
        std::vector<uint8_t> bits;
        uint32_t bitCount = 0;
        if (readableBits(0, bits, bitCount)) {
            m_sectors13 = NibbleEncoder::looksLike13Sector(
                NibbleEncoder::wozBitsToNibbles(bits, bitCount, 2), 0);
        }
    }
    initGeometry(TRACKS_35, m_sectors13 ? SECTORS_13 : SECTORS_16);

    m_modified = false;
    std::fill(m_sectorsCached.begin(), m_sectorsCached.end(), false);
    invalidateDetection();
}

void AppleWozImage::parseWozHeader() {
    if (m_data.size() < WOZ_HEADER_SIZE) {
        throw InvalidFormatException("Invalid WOZ header");
    }

    // Check magic number
    uint32_t magic = readU32(&m_data[0]);

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
    uint32_t storedCrc = readU32(&m_data[8]);
    uint32_t calculatedCrc = CRC::crc32(m_data.data() + WOZ_HEADER_SIZE,
                                         m_data.size() - WOZ_HEADER_SIZE);

    if (storedCrc != 0 && storedCrc != calculatedCrc) {
        // Warning only - some WOZ files have incorrect CRC
    }

    // Collect chunks first: TRKS validation needs INFO and FLUX, which may
    // appear in any order.
    struct Chunk { uint32_t id; size_t pos; size_t size; };
    std::vector<Chunk> chunks;
    size_t pos = WOZ_HEADER_SIZE;
    while (pos + 8 <= m_data.size()) {
        uint32_t chunkId = readU32(&m_data[pos]);
        uint32_t chunkSize = readU32(&m_data[pos + 4]);
        pos += 8;

        if (chunkSize > m_data.size() - pos) {
            throw InvalidFormatException("Chunk extends beyond file");
        }
        chunks.push_back({chunkId, pos, chunkSize});
        pos += chunkSize;
    }

    auto find = [&chunks](uint32_t id) -> const Chunk* {
        for (const auto& c : chunks) {
            if (c.id == id) return &c;
        }
        return nullptr;
    };

    const Chunk* info = find(CHUNK_INFO);
    const Chunk* tmap = find(CHUNK_TMAP);
    const Chunk* trks = find(CHUNK_TRKS);
    if (!info || !tmap || !trks) {
        throw InvalidFormatException("WOZ file is missing INFO, TMAP or TRKS chunk");
    }

    parseInfoChunk(&m_data[info->pos], info->size);
    parseTmapChunk(&m_data[tmap->pos], tmap->size);

    // FLUX is only valid with INFO v3+ and both FLUX fields non-zero
    if (const Chunk* flux = find(CHUNK_FLUX)) {
        if (m_infoVersion >= 3 && readU16(&m_infoRaw[46]) != 0 &&
            readU16(&m_infoRaw[48]) != 0 && flux->size >= TMAP_SIZE) {
            std::copy(&m_data[flux->pos], &m_data[flux->pos] + TMAP_SIZE, m_fluxMap.begin());
            m_hasFlux = std::any_of(m_fluxMap.begin(), m_fluxMap.end(),
                                    [](uint8_t v) { return v != 0xFF; });
        }
    }

    parseTrksChunk(&m_data[trks->pos], trks->size);

    for (const auto& c : chunks) {
        if (c.id == CHUNK_META) {
            parseMetaChunk(&m_data[c.pos], c.size);
        } else if (c.id != CHUNK_INFO && c.id != CHUNK_TMAP && c.id != CHUNK_TRKS) {
            m_otherChunks.emplace_back(
                c.id, std::vector<uint8_t>(m_data.begin() + c.pos,
                                           m_data.begin() + c.pos + c.size));
        }
    }
}

void AppleWozImage::parseInfoChunk(const uint8_t* data, size_t size) {
    if (size < INFO_SIZE) {
        throw InvalidFormatException("INFO chunk too small");
    }

    m_infoRaw.assign(data, data + INFO_SIZE);
    m_infoVersion = data[0];
    m_diskType = data[1];
    m_writeProtected = (data[2] != 0);
    m_synchronized = (data[3] != 0);
    m_cleaned = (data[4] != 0);

    // Creator string (32 bytes, space-padded; older writers used NULs)
    m_creator = std::string(reinterpret_cast<const char*>(&data[5]), 32);
    const size_t last = m_creator.find_last_not_of(std::string("\0 ", 2));
    m_creator.erase(last == std::string::npos ? 0 : last + 1);

    if (m_infoVersion >= 2) {
        m_diskSides = data[37];
        m_bootSectorFormat = data[38];
        m_optimalBitTiming = data[39];
    }
}

void AppleWozImage::parseTmapChunk(const uint8_t* data, size_t size) {
    if (size < TMAP_SIZE) {
        throw InvalidFormatException("TMAP chunk too small");
    }

    std::copy(data, data + TMAP_SIZE, m_trackMap.begin());
}

void AppleWozImage::parseTrksChunk(const uint8_t* data, size_t size) {
    if (m_wozVersion == 1) {
        // WOZ1: 6656-byte records: 6646 bitstream bytes, bytes used, bit count,
        // splice point, splice nibble, splice bit count, reserved
        size_t numTracks = size / WOZ1_TRACK_SIZE;
        m_tracks.resize(numTracks);

        for (size_t i = 0; i < numTracks; ++i) {
            const uint8_t* rec = data + i * WOZ1_TRACK_SIZE;
            TrackInfo& t = m_tracks[i];
            t.bytesUsed = readU16(rec + WOZ1_BITS_SIZE);
            t.bitCount = readU16(rec + WOZ1_BITS_SIZE + 2);
            t.splicePoint = readU16(rec + WOZ1_BITS_SIZE + 4);
            t.spliceNibble = rec[WOZ1_BITS_SIZE + 6];
            t.spliceBitCount = rec[WOZ1_BITS_SIZE + 7];

            if (t.bytesUsed > WOZ1_BITS_SIZE ||
                t.bitCount > static_cast<uint32_t>(t.bytesUsed) * 8) {
                throw InvalidFormatException("WOZ1 track " + std::to_string(i) +
                                             ": bit count exceeds track data");
            }
            t.bits.assign(rec, rec + t.bytesUsed);
        }
    } else {
        // WOZ2: 160 TRK entries (8 bytes each), bit data in 512-byte blocks
        // counted from the start of the file (first possible block is 3)
        if (size < TRK_ENTRIES * 8) {
            throw InvalidFormatException("TRKS chunk too small");
        }
        m_tracks.resize(TRK_ENTRIES);

        for (size_t i = 0; i < TRK_ENTRIES; ++i) {
            const uint8_t* e = data + i * 8;
            TrackInfo& t = m_tracks[i];
            t.startingBlock = readU16(e);
            t.blockCount = readU16(e + 2);
            t.bitCount = readU32(e + 4);

            if (t.startingBlock == 0 && t.blockCount == 0) {
                continue;  // unused entry
            }

            const size_t offset = static_cast<size_t>(t.startingBlock) * WOZ2_BITS_BLOCK;
            const size_t length = static_cast<size_t>(t.blockCount) * WOZ2_BITS_BLOCK;
            const bool isFlux = std::find(m_fluxMap.begin(), m_fluxMap.end(),
                                          static_cast<uint8_t>(i)) != m_fluxMap.end();
            // FLUX tracks store a byte count in bitCount
            const uint64_t needBytes = isFlux ? t.bitCount
                                              : (static_cast<uint64_t>(t.bitCount) + 7) / 8;

            if (t.startingBlock < WOZ2_FIRST_BITS_BLOCK ||
                offset > m_data.size() || length > m_data.size() - offset ||
                needBytes > length) {
                throw InvalidFormatException("WOZ2 track entry " + std::to_string(i) +
                                             " points outside the file or is too short");
            }
            t.bits.assign(m_data.begin() + offset, m_data.begin() + offset + length);
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

    if (m_hasFlux) {
        throw UnsupportedFormatException("Saving WOZ files with FLUX tracks is not supported");
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

    // Initialize header (CRC32 at bytes 8-11 will be calculated after all data is written)
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

    // INFO (60) and TMAP (160) first, so WOZ2 bit data starts at block 3
    addChunk(CHUNK_INFO, infoChunk);
    addChunk(CHUNK_TMAP, tmapChunk);
    addChunk(CHUNK_TRKS, trksChunk);
    if (!metaChunk.empty()) {
        addChunk(CHUNK_META, metaChunk);
    }
    for (const auto& [id, data] : m_otherChunks) {
        if (id == CHUNK_WRIT && m_tracksChanged) {
            continue;  // write hints describe the old bitstreams
        }
        addChunk(id, data);
    }

    // Calculate and store CRC32
    uint32_t crc = CRC::crc32(result.data() + WOZ_HEADER_SIZE,
                              result.size() - WOZ_HEADER_SIZE);
    putU32(result, 8, crc);

    return result;
}

std::vector<uint8_t> AppleWozImage::buildInfoChunk() const {
    std::vector<uint8_t> result;

    if (m_infoRaw.size() == INFO_SIZE) {
        // Loaded image: keep every field, patch only what this tool changes
        result = m_infoRaw;
    } else {
        result.assign(INFO_SIZE, 0);
        result[0] = (m_wozVersion == 1) ? 1 : 2;
        result[1] = m_diskType;
        result[4] = m_cleaned ? 1 : 0;

        // Creator: UTF-8, padded to 32 bytes with spaces
        std::fill(result.begin() + 5, result.begin() + 37, ' ');
        size_t creatorLen = std::min(m_creator.size(), size_t(32));
        std::copy(m_creator.begin(), m_creator.begin() + creatorLen, result.begin() + 5);

        if (result[0] >= 2) {
            result[37] = m_diskSides;
            result[38] = m_bootSectorFormat;
            result[39] = m_optimalBitTiming;
            putU16(result, 40, 0);   // Compatible hardware: unknown
            putU16(result, 42, 0);   // Required RAM: unknown
        }
    }

    result[2] = m_writeProtected ? 1 : 0;
    result[3] = m_synchronized ? 1 : 0;

    if (result[0] >= 2) {
        // Largest track, in 512-byte blocks
        size_t largest = 0;
        for (const auto& track : m_tracks) {
            const size_t bytes = (static_cast<size_t>(track.bitCount) + 7) / 8;
            largest = std::max(largest, (bytes + WOZ2_BITS_BLOCK - 1) / WOZ2_BITS_BLOCK);
        }
        putU16(result, 44, static_cast<uint16_t>(largest));
    }

    return result;
}

std::vector<uint8_t> AppleWozImage::buildTmapChunk() const {
    return std::vector<uint8_t>(m_trackMap.begin(), m_trackMap.end());
}

std::vector<uint8_t> AppleWozImage::buildTrksChunk() const {
    if (m_wozVersion == 1) {
        std::vector<uint8_t> result;
        for (const auto& track : m_tracks) {
            const size_t bytes = (static_cast<size_t>(track.bitCount) + 7) / 8;
            if (bytes > WOZ1_BITS_SIZE || track.bitCount > 0xFFFF) {
                throw WriteException("Track too long for WOZ1");
            }
            std::vector<uint8_t> rec(WOZ1_TRACK_SIZE, 0);
            std::copy(track.bits.begin(),
                      track.bits.begin() + std::min(bytes, track.bits.size()), rec.begin());
            putU16(rec, WOZ1_BITS_SIZE, static_cast<uint16_t>(bytes));
            putU16(rec, WOZ1_BITS_SIZE + 2, static_cast<uint16_t>(track.bitCount));
            putU16(rec, WOZ1_BITS_SIZE + 4, track.splicePoint);
            rec[WOZ1_BITS_SIZE + 6] = track.spliceNibble;
            rec[WOZ1_BITS_SIZE + 7] = track.spliceBitCount;
            result.insert(result.end(), rec.begin(), rec.end());
        }
        return result;
    }

    // WOZ2: 160 track entries, then bit data from block 3 in entry order
    std::vector<uint8_t> result(TRK_ENTRIES * 8, 0);
    size_t currentBlock = WOZ2_FIRST_BITS_BLOCK;

    for (size_t i = 0; i < m_tracks.size() && i < TRK_ENTRIES; ++i) {
        const auto& track = m_tracks[i];
        const size_t bytes = (static_cast<size_t>(track.bitCount) + 7) / 8;
        if (bytes == 0) {
            continue;
        }
        const size_t blockCount = (bytes + WOZ2_BITS_BLOCK - 1) / WOZ2_BITS_BLOCK;

        const size_t entryOffset = i * 8;
        putU16(result, entryOffset, static_cast<uint16_t>(currentBlock));
        putU16(result, entryOffset + 2, static_cast<uint16_t>(blockCount));
        putU32(result, entryOffset + 4, track.bitCount);

        std::vector<uint8_t> padded(blockCount * WOZ2_BITS_BLOCK, 0);
        std::copy(track.bits.begin(),
                  track.bits.begin() + std::min(bytes, track.bits.size()), padded.begin());
        result.insert(result.end(), padded.begin(), padded.end());

        currentBlock += blockCount;
    }

    return result;
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

AppleWozImage::TrackInfo AppleWozImage::makeStandardTrack(
    const std::array<std::vector<uint8_t>, 16>& sectors, uint8_t volume, uint8_t track) {

    TrackInfo info;
    auto nibbles = NibbleEncoder::buildTrackNibbles(sectors, volume, track,
                                                    NibbleEncoder::WOZ_GAP1_SYNCS);
    info.bits = NibbleEncoder::nibblesToWozBits(nibbles, info.bitCount);
    info.bytesUsed = static_cast<uint16_t>(info.bits.size());
    info.splicePoint = 0xFFFF;
    return info;
}

void AppleWozImage::create(const DiskGeometry& geometry) {
    requireLoadableGeometry(geometry, true);
    size_t tracks = geometry.tracks > 0 ? geometry.tracks : TRACKS_35;
    // 13 sectors: DOS 3.2 tracks (the target of a .d13 conversion)
    m_sectors13 = geometry.sectorsPerTrack == SECTORS_13;
    initGeometry(tracks, m_sectors13 ? SECTORS_13 : SECTORS_16);

    m_wozVersion = 2;
    m_infoVersion = 2;
    m_infoRaw.clear();
    m_diskType = 1;  // 5.25"
    m_writeProtected = false;
    m_synchronized = false;
    m_cleaned = true;
    m_diskSides = 1;
    m_bootSectorFormat = m_sectors13 ? 2 : 1;  // 13- or 16-sector
    m_optimalBitTiming = 32;
    m_creator = "rdedisktool";
    m_metadata.clear();
    m_otherChunks.clear();
    m_hasFlux = false;
    m_tracksChanged = false;
    std::fill(m_fluxMap.begin(), m_fluxMap.end(), 0xFF);

    // Track map: each track is also visible from the adjacent quarter tracks
    std::fill(m_trackMap.begin(), m_trackMap.end(), 0xFF);
    for (size_t t = 0; t < tracks && t * 4 < TMAP_SIZE; ++t) {
        const uint8_t index = static_cast<uint8_t>(t);
        if (t > 0) m_trackMap[t * 4 - 1] = index;
        m_trackMap[t * 4] = index;
        if (t * 4 + 1 < TMAP_SIZE) m_trackMap[t * 4 + 1] = index;
    }

    // Create formatted tracks with empty sectors
    std::array<std::vector<uint8_t>, 16> sectorData;
    for (auto& sector : sectorData) {
        sector.assign(BYTES_PER_SECTOR, 0);
    }
    m_tracks.assign(tracks, TrackInfo{});
    for (size_t t = 0; t < tracks; ++t) {
        if (m_sectors13) {
            TrackInfo info;
            const auto nibbles = NibbleEncoder::buildTrackNibbles13(
                sectorData, 254, static_cast<uint8_t>(t), NibbleEncoder::WOZ13_GAP1_SYNCS);
            info.bits = NibbleEncoder::nibblesToWozBits(nibbles, info.bitCount, 9);
            info.bytesUsed = static_cast<uint16_t>(info.bits.size());
            info.splicePoint = 0xFFFF;
            m_tracks[t] = std::move(info);
        } else {
            m_tracks[t] = makeStandardTrack(sectorData, 254, static_cast<uint8_t>(t));
        }
    }

    // Build the file data
    m_data = buildWozFile();

    m_modified = true;
    m_filePath.clear();
    std::fill(m_sectorsCached.begin(), m_sectorsCached.end(), false);
    invalidateDetection();
}

size_t AppleWozImage::calculateOffset(size_t track, size_t /*sector*/) const {
    // WOZ format doesn't have simple linear offsets
    return track;  // Return track number as reference
}

int AppleWozImage::trackIndexFor(size_t track) const {
    if (track >= TRACKS_35 || track * 4 >= TMAP_SIZE) {
        return -1;
    }
    const uint8_t index = m_trackMap[track * 4];
    if (index == 0xFF || index >= m_tracks.size() || m_tracks[index].bitCount == 0) {
        return -1;
    }
    return index;
}

bool AppleWozImage::isFluxTrack(size_t track) const {
    // FLUX takes precedence over TMAP for the same quarter track
    return m_hasFlux && track * 4 < TMAP_SIZE && m_fluxMap[track * 4] != 0xFF;
}

int AppleWozImage::fluxIndexFor(size_t track) const {
    if (!isFluxTrack(track)) {
        return -1;
    }
    const uint8_t index = m_fluxMap[track * 4];
    if (index >= m_tracks.size() || m_tracks[index].bitCount == 0) {
        return -1;
    }
    return index;
}

bool AppleWozImage::isFluxEntry(int index) const {
    return m_hasFlux && index >= 0 &&
           std::find(m_fluxMap.begin(), m_fluxMap.end(), static_cast<uint8_t>(index)) !=
               m_fluxMap.end();
}

std::vector<uint8_t> AppleWozImage::fluxToBits(const TrackInfo& t, uint32_t& bitCount) const {
    // Each byte: ticks (125 ns) since the previous flux transition; 255 adds
    // to the next byte. A transition is a 1 bit; an interval of n bit cells
    // (n = ticks / optimal bit timing, rounded half up, at least 1) is n-1
    // zero bits and the one. The stream loops without a time warp.
    const uint32_t timing = m_optimalBitTiming != 0 ? m_optimalBitTiming : 32;
    std::vector<uint8_t> bits;
    uint32_t count = 0;
    auto push = [&](int bit) {
        if ((count & 7) == 0) bits.push_back(0);
        if (bit) bits.back() |= static_cast<uint8_t>(0x80 >> (count & 7));
        ++count;
    };
    const size_t n = std::min<size_t>(t.bitCount, t.bits.size());
    uint64_t ticks = 0;
    for (size_t i = 0; i < n; ++i) {
        ticks += t.bits[i];
        if (t.bits[i] == 255) {
            continue;
        }
        const uint64_t cells = std::max<uint64_t>(1, (2 * ticks + timing) / (2 * timing));
        for (uint64_t k = 1; k < cells; ++k) push(0);
        push(1);
        ticks = 0;
    }
    bitCount = count;
    return bits;
}

bool AppleWozImage::readableBits(size_t track, std::vector<uint8_t>& bits,
                                 uint32_t& bitCount) const {
    const int flux = fluxIndexFor(track);
    if (flux >= 0) {
        bits = fluxToBits(m_tracks[flux], bitCount);
        return bitCount > 0;
    }
    if (isFluxTrack(track)) {
        return false;   // FLUX map entry without data: the TMAP one is not used
    }
    const int index = trackIndexFor(track);
    if (index < 0) {
        return false;
    }
    bits = m_tracks[index].bits;
    bitCount = m_tracks[index].bitCount;
    return true;
}

NibbleEncoder::ParsedTrack AppleWozImage::parseTrackBits(size_t track) const {
    std::vector<uint8_t> bits;
    uint32_t bitCount = 0;
    if (!readableBits(track, bits, bitCount)) {
        return {};
    }
    auto nibbles = NibbleEncoder::wozBitsToNibbles(bits, bitCount, 2);
    return m_sectors13 ? NibbleEncoder::parseNibbleStream13(nibbles, static_cast<uint8_t>(track))
                       : NibbleEncoder::parseNibbleStream(nibbles, static_cast<uint8_t>(track));
}

void AppleWozImage::decodeSectorsForTrack(size_t track) {
    if (track >= TRACKS_35 || m_sectorsCached[track]) {
        return;
    }
    m_decodedSectors[track] = parseTrackBits(track);
    m_sectorsCached[track] = true;
}

void AppleWozImage::invalidateDetection() {
    m_detectionValid = false;
    m_fileSystemDetected = false;
}

const std::vector<uint8_t>& AppleWozImage::detectionImage() const {
    if (!m_detectionValid) {
        const size_t spt = m_geometry.sectorsPerTrack;
        m_detectionImage.assign(TRACKS_35 * spt * BYTES_PER_SECTOR, 0);
        const size_t tracks = std::min(m_geometry.tracks, TRACKS_35);
        for (size_t t = 0; t < tracks; ++t) {
            const NibbleEncoder::ParsedTrack parsed =
                m_sectorsCached[t] ? m_decodedSectors[t] : parseTrackBits(t);
            for (size_t s = 0; s < spt; ++s) {
                if (parsed.found[s]) {
                    std::copy(parsed.sectors[s].begin(), parsed.sectors[s].end(),
                              m_detectionImage.begin() + (t * spt + s) * BYTES_PER_SECTOR);
                }
            }
        }
        m_detectionValid = true;
    }
    return m_detectionImage;
}

SectorBuffer AppleWozImage::readSector(size_t track, size_t /*side*/, size_t sector) {
    if (track >= m_geometry.tracks || track >= TRACKS_35) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }
    if (sector >= m_geometry.sectorsPerTrack) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }
    decodeSectorsForTrack(track);
    if (!m_decodedSectors[track].found[sector]) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }
    return m_decodedSectors[track].sectors[sector];
}

void AppleWozImage::writeSector(size_t track, size_t /*side*/, size_t sector,
                                const SectorBuffer& data) {
    if (m_writeProtected) {
        throw WriteProtectedException();
    }
    if (track >= m_geometry.tracks || track >= TRACKS_35) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }
    if (sector >= m_geometry.sectorsPerTrack) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }
    // FLUX data is only read, never written (also when the TMAP entry is
    // FLUX data another map entry points at)
    if (isFluxTrack(track) || isFluxEntry(trackIndexFor(track))) {
        throw UnsupportedFormatException("Track " + std::to_string(track) +
                                         " is stored as FLUX data (read-only)");
    }

    const int index = trackIndexFor(track);
    if (index < 0) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }
    TrackInfo& info = m_tracks[index];

    // Like DOS 3.3 / 3.2 RWTS: find the sector, then replace only its data
    // field (D5 AA AD .. DE AA; 6-and-2, or 5-and-3 on 13-sector tracks).
    // Address fields, gaps, other sectors - readable or not - and anything a
    // copy protection put on the track stay as they are.
    std::vector<uint64_t> firstBit;
    const auto nibbles = NibbleEncoder::wozBitsToNibbles(info.bits, info.bitCount, 2, &firstBit);
    const NibbleEncoder::ParsedTrack before = m_sectors13
        ? NibbleEncoder::parseNibbleStream13(nibbles, static_cast<uint8_t>(track))
        : NibbleEncoder::parseNibbleStream(nibbles, static_cast<uint8_t>(track));
    const bool unwritten = m_sectors13 && !before.found[sector] && before.addrOnly[sector];
    if (!before.found[sector] && !unwritten) {
        throw WriteException("Cannot write track " + std::to_string(track) + " sector " +
                             std::to_string(sector) + ": the sector cannot be read");
    }

    SectorBuffer payload = data;
    payload.resize(BYTES_PER_SECTOR, 0);
    const std::vector<uint8_t> field = m_sectors13 ? NibbleEncoder::dataFieldNibbles53(payload)
                                                   : NibbleEncoder::dataFieldNibbles(payload);
    // The bits written, MSB first
    std::vector<uint8_t> written;
    auto emit = [&](int bit) { written.push_back(static_cast<uint8_t>(bit)); };
    auto putNibble = [&](uint8_t v) { for (int b = 7; b >= 0; --b) emit((v >> b) & 1); };
    uint64_t start = 0, oldLength = 0;
    if (unwritten) {
        // DOS 3.2 INIT wrote only the address field: the data field goes
        // after its DE AA EB as DOS 3.2 writes it (real capture: 14 nine-bit
        // syncs FF+0, D5 AA AD .. DE AA EB), over the same number of bits,
        // and must end before the next address field (4 syncs kept)
        const size_t addr = before.addrAt[sector];
        start = firstBit[addr + 13] + 8;
        uint64_t limit = firstBit[addr] + info.bitCount;
        for (size_t k = addr + 3; k + 2 < nibbles.size(); ++k) {
            if (nibbles[k] == NibbleEncoder::ADDR_PROLOGUE_1 &&
                nibbles[k + 1] == NibbleEncoder::ADDR_PROLOGUE_2 &&
                nibbles[k + 2] == NibbleEncoder::ADDR_PROLOGUE_3_13) {
                limit = firstBit[k];
                break;
            }
        }
        const uint64_t fieldBits = (field.size() + 1) * 8;
        size_t syncs = 14;
        while (syncs >= 5 && start + syncs * 9 + fieldBits + 4 * 9 > limit) {
            --syncs;
        }
        if (syncs < 5) {
            throw WriteException("Cannot write track " + std::to_string(track) + " sector " +
                                 std::to_string(sector) +
                                 ": no room for a data field before the next address field");
        }
        for (size_t k = 0; k < syncs; ++k) {
            putNibble(0xFF);
            emit(0);
        }
        for (uint8_t v : field) putNibble(v);
        putNibble(NibbleEncoder::EPILOGUE_3);
        oldLength = written.size();
    } else {
        const size_t firstNibble = before.dataAt[sector];
        start = firstBit[firstNibble];
        oldLength = firstBit[firstNibble + field.size() - 1] + 8 - start;
        for (uint8_t v : field) putNibble(v);
    }
    const uint64_t newLength = written.size();
    auto fieldBit = [&](uint64_t j) { return written[j]; };

    const TrackInfo saved = info;
    const bool savedSync = m_synchronized;
    if (oldLength == newLength) {
        for (uint64_t j = 0; j < newLength; ++j) {
            const uint32_t pos = static_cast<uint32_t>((start + j) % info.bitCount);
            const uint8_t mask = static_cast<uint8_t>(0x80 >> (pos & 7));
            info.bits[pos >> 3] = static_cast<uint8_t>(fieldBit(j) ? (info.bits[pos >> 3] | mask)
                                                                   : (info.bits[pos >> 3] & ~mask));
        }
    } else {
        // Timing bits inside the old field: written back as plain 8-bit
        // nibbles (as RWTS writes them), so the track gets shorter or longer
        const uint64_t from = start % info.bitCount;
        if (from + oldLength > info.bitCount) {
            throw WriteException("Cannot write track " + std::to_string(track) + " sector " +
                                 std::to_string(sector) +
                                 ": its data field has timing bits and crosses the track start");
        }
        auto oldBit = [&](uint64_t pos) { return (saved.bits[pos >> 3] >> (7 - (pos & 7))) & 1; };
        const uint64_t total = info.bitCount - oldLength + newLength;
        std::vector<uint8_t> bits((total + 7) / 8, 0);
        uint64_t out = 0;
        auto put = [&](int bit) {
            if (bit) bits[out >> 3] |= static_cast<uint8_t>(0x80 >> (out & 7));
            ++out;
        };
        for (uint64_t pos = 0; pos < from; ++pos) put(oldBit(pos));
        for (uint64_t j = 0; j < newLength; ++j) put(fieldBit(j));
        for (uint64_t pos = from + oldLength; pos < saved.bitCount; ++pos) put(oldBit(pos));
        info.bits = std::move(bits);
        info.bitCount = static_cast<uint32_t>(total);
        info.bytesUsed = static_cast<uint16_t>(info.bits.size());
        // WOZ1 splice point (a bit index): moves with the bits after the
        // field, is lost when it was inside the rewritten field
        if (info.splicePoint != 0xFFFF && info.splicePoint >= from) {
            info.splicePoint = info.splicePoint >= from + oldLength
                ? static_cast<uint16_t>(info.splicePoint + newLength - oldLength)
                : static_cast<uint16_t>(0xFFFF);
        }
        m_synchronized = false;   // track lengths no longer line up
    }

    // Every logical track stored in this TRKS entry must be decoded again
    auto invalidate = [&]() {
        for (size_t t = 0; t < TRACKS_35; ++t) {
            if (trackIndexFor(t) == index) {
                m_sectorsCached[t] = false;
            }
        }
        invalidateDetection();
    };
    invalidate();

    // The write must read back, no other sector may change, and every
    // address field (written sectors and unwritten ones) must still be there
    decodeSectorsForTrack(track);
    const NibbleEncoder::ParsedTrack& after = m_decodedSectors[track];
    bool ok = after.found[sector] && after.sectors[sector] == payload &&
              after.addrSeen == before.addrSeen;
    for (size_t s = 0; ok && s < before.sectorCount; ++s) {
        if (s != sector && before.found[s] &&
            (!after.found[s] || after.sectors[s] != before.sectors[s])) {
            ok = false;
        }
        if (s != sector && before.addrOnly[s] && !after.addrOnly[s]) {
            ok = false;
        }
    }
    if (!ok) {
        info = saved;
        m_synchronized = savedSync;
        invalidate();
        throw WriteException("Cannot write track " + std::to_string(track) + " sector " +
                             std::to_string(sector) + ": the track did not read back (left unchanged)");
    }

    m_tracksChanged = true;   // WRIT hints describe the old bitstreams
    m_modified = true;
}

TrackBuffer AppleWozImage::readTrack(size_t track, size_t /*side*/) {
    if (track >= m_geometry.tracks) {
        throw SectorNotFoundException(static_cast<int>(track), 0);
    }

    std::vector<uint8_t> bits;
    uint32_t bitCount = 0;
    if (!readableBits(track, bits, bitCount)) {
        return TrackBuffer(NibbleEncoder::TRACK_NIBBLE_SIZE, 0xFF);
    }
    return bits;
}

void AppleWozImage::writeTrack(size_t track, size_t /*side*/, const TrackBuffer& data) {
    if (m_writeProtected) {
        throw WriteProtectedException();
    }
    if (m_sectors13) {
        throw UnsupportedFormatException("13-sector (DOS 3.2) tracks cannot be written whole");
    }

    if (track >= m_geometry.tracks || track >= TRACKS_35) {
        throw SectorNotFoundException(static_cast<int>(track), 0);
    }
    if (isFluxTrack(track) || isFluxEntry(m_trackMap[track * 4])) {
        throw UnsupportedFormatException("Track " + std::to_string(track) +
                                         " is stored as FLUX data (read-only)");
    }

    uint8_t trackIndex = m_trackMap[track * 4];
    if (trackIndex == 0xFF || trackIndex >= m_tracks.size()) {
        // Need to allocate new track entry
        trackIndex = static_cast<uint8_t>(m_tracks.size());
        m_tracks.emplace_back();
        m_trackMap[track * 4] = trackIndex;
    }

    // Raw bitstream bytes, all bits valid
    m_tracks[trackIndex].bits = data;
    m_tracks[trackIndex].bitCount = static_cast<uint32_t>(data.size() * 8);
    m_tracks[trackIndex].bytesUsed = static_cast<uint16_t>(data.size());
    m_tracks[trackIndex].splicePoint = 0xFFFF;

    // Invalidate sector cache
    m_sectorsCached[track] = false;
    m_tracksChanged = true;
    m_synchronized = false;

    m_modified = true;
    invalidateDetection();
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
        case DiskFormat::Unknown:
        case DiskFormat::AppleNIB2:
        case DiskFormat::AppleWOZ1:
        case DiskFormat::AppleWOZ2:
        case DiskFormat::MSXDSK:
        case DiskFormat::MSXDMK:
        case DiskFormat::MSXXSA:
        case DiskFormat::X68000XDF:
        case DiskFormat::X68000DIM:
        case DiskFormat::MacIMG:
        case DiskFormat::MacDC42:
        case DiskFormat::MacMOOF:
        case DiskFormat::AppleD13:
        case DiskFormat::Apple800PO:
        case DiskFormat::Apple800MG:
            return false;
    }
    return false;
}

std::unique_ptr<DiskImage> AppleWozImage::convertTo(DiskFormat format) const {
    if (!canConvertTo(format)) {
        throw UnsupportedFormatException("Cannot convert to " +
                                         std::string(formatToString(format)));
    }

    if (m_sectors13) {
        throw UnsupportedFormatException("13-sector images: use the convert command (d13, nib, nb2, woz)");
    }

    if (format == DiskFormat::AppleDO) {
        auto doImage = std::make_unique<AppleDOImage>();
        doImage->create(m_geometry);

        // DO images use the same DOS 3.3 logical numbering
        for (size_t track = 0; track < m_geometry.tracks && track < TRACKS_35; ++track) {
            const NibbleEncoder::ParsedTrack parsed =
                m_sectorsCached[track] ? m_decodedSectors[track] : parseTrackBits(track);
            for (size_t sector = 0; sector < SECTORS_16; ++sector) {
                if (!parsed.found[sector]) {
                    throw SectorNotFoundException(static_cast<int>(track),
                                                  static_cast<int>(sector));
                }
                doImage->writeSector(track, 0, sector, parsed.sectors[sector]);
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
    uint32_t magic = readU32(&m_data[0]);
    if (magic != WOZ1_MAGIC && magic != WOZ2_MAGIC) {
        return false;
    }

    // Check that we have some tracks
    return std::any_of(m_tracks.begin(), m_tracks.end(),
                       [](const TrackInfo& t) { return t.bitCount > 0; });
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
        if (trackIndexFor(t) >= 0 || fluxIndexFor(t) >= 0) ++validTracks;
    }
    oss << "Valid Tracks: " << validTracks << "\n";
    oss << "Sectors/Track: " << m_geometry.sectorsPerTrack
        << (m_sectors13 ? " (DOS 3.2, 5-and-3)" : "") << "\n";
    oss << "Total Track Entries: " << m_tracks.size() << "\n";
    if (m_hasFlux) {
        int flux = 0;
        for (size_t t = 0; t < TRACKS_35; ++t) {
            if (fluxIndexFor(t) >= 0) ++flux;
        }
        oss << "FLUX Tracks: " << flux << " (read; never written - the image is not saved)\n";
    }

    if (!m_metadata.empty()) {
        oss << "\nMetadata:\n";
        for (const auto& [key, value] : m_metadata) {
            oss << "  " << key << ": " << value << "\n";
        }
    }

    return oss.str();
}

std::vector<std::string> AppleWozImage::readWarnings() const {
    std::vector<std::string> warnings;
    const size_t tracks = std::min(m_geometry.tracks, TRACKS_35);
    for (size_t t = 0; t < tracks; ++t) {
        const NibbleEncoder::ParsedTrack parsed = parseTrackBits(t);
        for (size_t s = 0; s < m_geometry.sectorsPerTrack; ++s) {
            if (parsed.found[s] && parsed.fixedBitsMissing[s]) {
                warnings.push_back("Track " + std::to_string(t) + ", sector " + std::to_string(s) +
                                   ": address field misses fixed bits (read as DOS 3.3 reads it)");
            }
        }
    }
    return warnings;
}

} // namespace rde
