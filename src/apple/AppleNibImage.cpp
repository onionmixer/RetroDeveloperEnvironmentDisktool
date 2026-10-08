#include "rdedisktool/apple/AppleNibImage.h"
#include "rdedisktool/apple/AppleDOImage.h"
#include "rdedisktool/DiskImageFactory.h"
#include <fstream>
#include <sstream>

namespace rde {

// Register format with factory
namespace {
    struct AppleNibRegistrar {
        AppleNibRegistrar() {
            DiskImageFactory::registerFormat(DiskFormat::AppleNIB,
                []() -> std::unique_ptr<DiskImage> {
                    return std::make_unique<AppleNibImage>(DiskFormat::AppleNIB);
                });
            DiskImageFactory::registerFormat(DiskFormat::AppleNIB2,
                []() -> std::unique_ptr<DiskImage> {
                    return std::make_unique<AppleNibImage>(DiskFormat::AppleNIB2);
                });
        }
    };
    static AppleNibRegistrar registrar;
}

AppleNibImage::AppleNibImage(DiskFormat format)
    : AppleDiskImage(),
      m_format(format == DiskFormat::AppleNIB2 ? DiskFormat::AppleNIB2 : DiskFormat::AppleNIB),
      m_trackSize(format == DiskFormat::AppleNIB2 ? NB2_TRACK_SIZE : NIB_TRACK_SIZE) {
}

void AppleNibImage::load(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        throw FileNotFoundException(path.string());
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw ReadException("Cannot open file: " + path.string());
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    // Determine format based on file size
    if (fileSize == NIB_DISK_SIZE) {
        m_format = DiskFormat::AppleNIB;
        m_trackSize = NIB_TRACK_SIZE;
    } else if (fileSize == NB2_DISK_SIZE) {
        m_format = DiskFormat::AppleNIB2;
        m_trackSize = NB2_TRACK_SIZE;
    } else {
        throw InvalidFormatException("Invalid NIB file size: expected " +
                                     std::to_string(NIB_DISK_SIZE) + " or " +
                                     std::to_string(NB2_DISK_SIZE) + " bytes");
    }

    m_data.resize(fileSize);
    file.read(reinterpret_cast<char*>(m_data.data()), fileSize);

    if (!file) {
        throw ReadException("Failed to read file: " + path.string());
    }

    m_filePath = path;
    m_modified = false;
    m_fileSystemDetected = false;

    // 13-sector (DOS 3.2) or 16-sector tracks, decided by track 0's content
    m_sectors13 = false;
    {
        std::vector<uint8_t> stream(m_data.begin(), m_data.begin() + m_trackSize);
        stream.insert(stream.end(), m_data.begin(), m_data.begin() + m_trackSize);
        m_sectors13 = NibbleEncoder::looksLike13Sector(stream, 0);
    }
    initGeometry(TRACKS_35, m_sectors13 ? SECTORS_13 : SECTORS_16);

    // Reset track cache
    std::fill(m_trackDecoded.begin(), m_trackDecoded.end(), false);
    invalidateDetection();
}

void AppleNibImage::save(const std::filesystem::path& path) {
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

void AppleNibImage::create(const DiskGeometry& geometry) {
    requireLoadableGeometry(geometry, true);
    size_t tracks = geometry.tracks > 0 ? geometry.tracks : TRACKS_35;
    // 13 sectors: DOS 3.2 tracks (the target of a .d13 conversion)
    m_sectors13 = geometry.sectorsPerTrack == SECTORS_13;
    initGeometry(tracks, m_sectors13 ? SECTORS_13 : SECTORS_16);

    // Track size follows the format this image was made for (NIB or NB2)
    m_trackSize = (m_format == DiskFormat::AppleNIB2) ? NB2_TRACK_SIZE : NIB_TRACK_SIZE;
    m_data.resize(tracks * m_trackSize);

    // Initialize all tracks with blank formatted data
    for (size_t t = 0; t < tracks; ++t) {
        // Create empty sector data
        std::array<std::vector<uint8_t>, 16> sectorData;
        for (auto& sector : sectorData) {
            sector.resize(BYTES_PER_SECTOR, 0);
        }

        // Build nibblized track (13 sectors: gap 1 takes what is left)
        auto track = m_sectors13
            ? NibbleEncoder::buildTrackNibbles13(sectorData, m_volumeNumber, static_cast<uint8_t>(t),
                                                 m_trackSize - 13 * NibbleEncoder::SECTOR13_NIBBLES).nibbles
            : NibbleEncoder::buildNibTrack(sectorData, m_volumeNumber,
                                           static_cast<uint8_t>(t), m_trackSize);

        // Copy to raw data
        size_t offset = t * m_trackSize;
        std::copy(track.begin(), track.end(), m_data.begin() + offset);
    }

    m_modified = true;
    m_fileSystemDetected = false;
    m_filePath.clear();

    // Reset cache
    std::fill(m_trackDecoded.begin(), m_trackDecoded.end(), false);
    invalidateDetection();
}

size_t AppleNibImage::calculateOffset(size_t track, size_t /*sector*/) const {
    return track * m_trackSize;
}

NibbleEncoder::ParsedTrack AppleNibImage::parseRawTrack(size_t track) const {
    // Two passes over the circular track so a sector crossing the end is seen whole
    const size_t offset = track * m_trackSize;
    std::vector<uint8_t> stream;
    stream.reserve(m_trackSize * 2);
    stream.insert(stream.end(), m_data.begin() + offset, m_data.begin() + offset + m_trackSize);
    stream.insert(stream.end(), m_data.begin() + offset, m_data.begin() + offset + m_trackSize);
    return m_sectors13 ? NibbleEncoder::parseNibbleStream13(stream, static_cast<uint8_t>(track))
                       : NibbleEncoder::parseNibbleStream(stream, static_cast<uint8_t>(track));
}

void AppleNibImage::decodeTrackIfNeeded(size_t track) {
    if (track >= TRACKS_35) {
        throw SectorNotFoundException(static_cast<int>(track), 0);
    }

    if (!m_trackDecoded[track]) {
        m_decodedTracks[track] = parseRawTrack(track);
        m_trackDecoded[track] = true;
    }
}

void AppleNibImage::invalidateTrackCache(size_t track) {
    if (track < TRACKS_35) {
        m_trackDecoded[track] = false;
    }
}

void AppleNibImage::invalidateDetection() {
    m_detectionValid = false;
    m_fileSystemDetected = false;
}

const std::vector<uint8_t>& AppleNibImage::detectionImage() const {
    if (!m_detectionValid) {
        const size_t spt = m_geometry.sectorsPerTrack;
        m_detectionImage.assign(TRACKS_35 * spt * BYTES_PER_SECTOR, 0);
        const size_t tracks = std::min(m_geometry.tracks, TRACKS_35);
        for (size_t t = 0; t < tracks && (t + 1) * m_trackSize <= m_data.size(); ++t) {
            const NibbleEncoder::ParsedTrack parsed =
                m_trackDecoded[t] ? m_decodedTracks[t] : parseRawTrack(t);
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

SectorBuffer AppleNibImage::readSector(size_t track, size_t /*side*/, size_t sector) {
    if (track >= m_geometry.tracks) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }
    if (sector >= m_geometry.sectorsPerTrack) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }

    decodeTrackIfNeeded(track);
    if (!m_decodedTracks[track].found[sector]) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }
    return m_decodedTracks[track].sectors[sector];
}

void AppleNibImage::writeSector(size_t track, size_t /*side*/, size_t sector,
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

    // Like DOS 3.3 / 3.2 RWTS: find the sector, then replace only its data
    // field (D5 AA AD .. DE AA; 6-and-2, or 5-and-3 on 13-sector tracks).
    // Everything else on the track - other sectors, readable or not, and any
    // copy-protection nibbles - stays as it is.
    const size_t offset = track * m_trackSize;
    if (offset + m_trackSize > m_data.size()) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }
    const NibbleEncoder::ParsedTrack before = parseRawTrack(track);
    const bool unwritten = m_sectors13 && !before.found[sector] && before.addrOnly[sector];
    if (!before.found[sector] && !unwritten) {
        throw WriteException("Cannot write track " + std::to_string(track) + " sector " +
                             std::to_string(sector) + ": the sector cannot be read");
    }

    SectorBuffer payload = data;
    payload.resize(BYTES_PER_SECTOR, 0);
    std::vector<uint8_t> field = m_sectors13 ? NibbleEncoder::dataFieldNibbles53(payload)
                                             : NibbleEncoder::dataFieldNibbles(payload);
    size_t at = before.dataAt[sector];
    if (unwritten) {
        // DOS 3.2 INIT wrote only the address field: the data field goes
        // after its DE AA EB like DOS 3.2 writes it (real disks: 14 syncs,
        // D5 AA AD .. DE AA EB), and must end before the next address field.
        // NIB nibbles are byte-aligned (no sync needed to find the next D5):
        // fewer syncs (down to 5) when the gap is short, 1 nibble kept free
        const size_t addr = before.addrAt[sector];
        auto byteAt = [&](size_t k) { return m_data[offset + k % m_trackSize]; };
        size_t next = m_trackSize;
        for (size_t d = 3; d < m_trackSize; ++d) {
            if (byteAt(addr + d) == NibbleEncoder::ADDR_PROLOGUE_1 &&
                byteAt(addr + d + 1) == NibbleEncoder::ADDR_PROLOGUE_2 &&
                byteAt(addr + d + 2) == NibbleEncoder::ADDR_PROLOGUE_3_13) {
                next = d;
                break;
            }
        }
        field.push_back(NibbleEncoder::EPILOGUE_3);
        size_t syncs = 14;
        while (syncs >= 5 && 14 + syncs + field.size() + 1 > next) {
            --syncs;
        }
        if (syncs < 5) {
            throw WriteException("Cannot write track " + std::to_string(track) + " sector " +
                                 std::to_string(sector) +
                                 ": no room for a data field before the next address field");
        }
        field.insert(field.begin(), syncs, 0xFF);
        at = addr + 14;
    }
    const std::vector<uint8_t> saved(m_data.begin() + offset, m_data.begin() + offset + m_trackSize);
    for (size_t j = 0; j < field.size(); ++j) {
        // the parser saw the track twice in a row: positions wrap around
        m_data[offset + (at + j) % m_trackSize] = field[j];
    }
    invalidateTrackCache(track);

    // The write must read back, no other sector may change, and every
    // address field (written sectors and unwritten ones) must still be there
    decodeTrackIfNeeded(track);
    const NibbleEncoder::ParsedTrack& after = m_decodedTracks[track];
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
        std::copy(saved.begin(), saved.end(), m_data.begin() + offset);
        invalidateTrackCache(track);
        throw WriteException("Cannot write track " + std::to_string(track) + " sector " +
                             std::to_string(sector) + ": the track did not read back (left unchanged)");
    }

    m_modified = true;
    invalidateDetection();
}

TrackBuffer AppleNibImage::readTrack(size_t track, size_t /*side*/) {
    if (track >= m_geometry.tracks) {
        throw SectorNotFoundException(static_cast<int>(track), 0);
    }

    size_t offset = track * m_trackSize;
    return TrackBuffer(m_data.begin() + offset,
                       m_data.begin() + offset + m_trackSize);
}

void AppleNibImage::writeTrack(size_t track, size_t /*side*/, const TrackBuffer& data) {
    if (m_writeProtected) {
        throw WriteProtectedException();
    }
    if (m_sectors13) {
        throw UnsupportedFormatException("13-sector (DOS 3.2) tracks cannot be written whole");
    }

    if (track >= m_geometry.tracks) {
        throw SectorNotFoundException(static_cast<int>(track), 0);
    }

    size_t offset = track * m_trackSize;
    size_t copySize = std::min(data.size(), m_trackSize);

    std::copy(data.begin(), data.begin() + copySize, m_data.begin() + offset);

    if (copySize < m_trackSize) {
        std::fill(m_data.begin() + offset + copySize,
                  m_data.begin() + offset + m_trackSize, 0xFF);
    }

    // Invalidate decoded cache for this track
    invalidateTrackCache(track);
    m_modified = true;
    invalidateDetection();
}

bool AppleNibImage::canConvertTo(DiskFormat format) const {
    switch (format) {
        case DiskFormat::AppleDO:
        case DiskFormat::ApplePO:
        case DiskFormat::AppleWOZ2:
            return true;
        case DiskFormat::Unknown:
        case DiskFormat::AppleNIB:
        case DiskFormat::AppleNIB2:
        case DiskFormat::AppleWOZ1:
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

std::unique_ptr<DiskImage> AppleNibImage::convertTo(DiskFormat format) const {
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
        for (size_t track = 0; track < m_geometry.tracks; ++track) {
            const NibbleEncoder::ParsedTrack parsed =
                m_trackDecoded[track] ? m_decodedTracks[track] : parseRawTrack(track);
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

bool AppleNibImage::validate() const {
    // Check file size
    if (m_data.size() != TRACKS_35 * m_trackSize) {
        return false;
    }

    // Try to decode a few tracks to verify format
    for (size_t t = 0; t < 3; ++t) {
        auto parsed = parseRawTrack(t);
        int validSectors = 0;
        for (bool f : parsed.found) {
            if (f) ++validSectors;
        }
        if (validSectors < 10) return false;
    }

    return true;
}

std::string AppleNibImage::getDiagnostics() const {
    std::ostringstream oss;

    oss << "Format: Apple II Nibble (";
    oss << (m_format == DiskFormat::AppleNIB ? ".nib" : ".nb2") << ")\n";
    oss << "Size: " << m_data.size() << " bytes\n";
    oss << "Track Size: " << m_trackSize << " bytes\n";
    oss << "Tracks: " << m_geometry.tracks << "\n";
    oss << "Sectors/Track: " << m_geometry.sectorsPerTrack
        << (m_sectors13 ? " (DOS 3.2, 5-and-3)" : "") << "\n";
    oss << "Volume Number: " << static_cast<int>(m_volumeNumber) << "\n";
    oss << "Write Protected: " << (m_writeProtected ? "Yes" : "No") << "\n";
    oss << "Modified: " << (m_modified ? "Yes" : "No") << "\n";

    // Count cached tracks
    int cached = 0;
    for (size_t t = 0; t < TRACKS_35; ++t) {
        if (m_trackDecoded[t]) ++cached;
    }
    oss << "Cached Tracks: " << cached << "\n";

    return oss.str();
}

std::vector<std::string> AppleNibImage::readWarnings() const {
    std::vector<std::string> warnings;
    const size_t tracks = std::min(m_geometry.tracks, TRACKS_35);
    for (size_t t = 0; t < tracks; ++t) {
        const NibbleEncoder::ParsedTrack parsed = parseRawTrack(t);
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
