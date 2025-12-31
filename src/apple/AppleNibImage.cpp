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
                    return std::make_unique<AppleNibImage>();
                });
        }
    };
    static AppleNibRegistrar registrar;
}

AppleNibImage::AppleNibImage() : AppleDiskImage() {
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

    // Reset track cache
    std::fill(m_trackDecoded.begin(), m_trackDecoded.end(), false);
    std::fill(m_trackDirty.begin(), m_trackDirty.end(), false);
}

void AppleNibImage::save(const std::filesystem::path& path) {
    std::filesystem::path savePath = path.empty() ? m_filePath : path;

    if (savePath.empty()) {
        throw WriteException("No file path specified");
    }

    if (m_writeProtected && savePath == m_filePath) {
        throw WriteProtectedException();
    }

    // Rebuild any dirty tracks
    for (size_t t = 0; t < TRACKS_35; ++t) {
        if (m_trackDirty[t]) {
            rebuildTrack(t);
        }
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
    size_t tracks = geometry.tracks > 0 ? geometry.tracks : TRACKS_35;
    initGeometry(tracks, SECTORS_16);

    m_format = DiskFormat::AppleNIB;
    m_trackSize = NIB_TRACK_SIZE;
    m_data.resize(tracks * m_trackSize);

    // Initialize all tracks with blank formatted data
    for (size_t t = 0; t < tracks; ++t) {
        // Create empty sector data
        std::array<std::vector<uint8_t>, 16> sectorData;
        for (auto& sector : sectorData) {
            sector.resize(BYTES_PER_SECTOR, 0);
        }

        // Build nibblized track
        auto track = NibbleEncoder::buildTrack(sectorData, m_volumeNumber,
                                               static_cast<uint8_t>(t));

        // Copy to raw data
        size_t offset = t * m_trackSize;
        std::copy(track.begin(), track.end(), m_data.begin() + offset);
    }

    m_modified = true;
    m_fileSystemDetected = false;
    m_filePath.clear();

    // Reset cache
    std::fill(m_trackDecoded.begin(), m_trackDecoded.end(), false);
    std::fill(m_trackDirty.begin(), m_trackDirty.end(), false);
}

size_t AppleNibImage::calculateOffset(size_t track, size_t /*sector*/) const {
    return track * m_trackSize;
}

void AppleNibImage::decodeTrackIfNeeded(size_t track) {
    if (track >= TRACKS_35) {
        throw SectorNotFoundException(static_cast<int>(track), 0);
    }

    if (!m_trackDecoded[track]) {
        size_t offset = track * m_trackSize;
        std::vector<uint8_t> rawTrack(m_data.begin() + offset,
                                       m_data.begin() + offset + m_trackSize);

        m_decodedTracks[track] = NibbleEncoder::parseTrack(rawTrack,
                                                           static_cast<uint8_t>(track));
        m_trackDecoded[track] = true;
    }
}

void AppleNibImage::invalidateTrackCache(size_t track) {
    if (track < TRACKS_35) {
        m_trackDecoded[track] = false;
    }
}

const std::array<std::vector<uint8_t>, 16>& AppleNibImage::getDecodedTrack(size_t track) {
    decodeTrackIfNeeded(track);
    return m_decodedTracks[track];
}

void AppleNibImage::rebuildTrack(size_t track) {
    if (track >= TRACKS_35) return;

    if (m_trackDecoded[track]) {
        auto nibbleTrack = NibbleEncoder::buildTrack(m_decodedTracks[track],
                                                      m_volumeNumber,
                                                      static_cast<uint8_t>(track));

        size_t offset = track * m_trackSize;
        size_t copySize = std::min(nibbleTrack.size(), m_trackSize);
        std::copy(nibbleTrack.begin(), nibbleTrack.begin() + copySize,
                  m_data.begin() + offset);

        m_trackDirty[track] = false;
    }
}

SectorBuffer AppleNibImage::readSector(size_t track, size_t /*side*/, size_t sector) {
    if (track >= m_geometry.tracks) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }
    if (sector >= m_geometry.sectorsPerTrack) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }

    decodeTrackIfNeeded(track);
    return m_decodedTracks[track][sector];
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

    decodeTrackIfNeeded(track);

    // Update sector data
    m_decodedTracks[track][sector] = data;
    if (m_decodedTracks[track][sector].size() < BYTES_PER_SECTOR) {
        m_decodedTracks[track][sector].resize(BYTES_PER_SECTOR, 0);
    } else if (m_decodedTracks[track][sector].size() > BYTES_PER_SECTOR) {
        m_decodedTracks[track][sector].resize(BYTES_PER_SECTOR);
    }

    m_trackDirty[track] = true;
    m_modified = true;
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
}

bool AppleNibImage::canConvertTo(DiskFormat format) const {
    switch (format) {
        case DiskFormat::AppleDO:
        case DiskFormat::ApplePO:
        case DiskFormat::AppleWOZ2:
            return true;
        default:
            return false;
    }
}

std::unique_ptr<DiskImage> AppleNibImage::convertTo(DiskFormat format) const {
    if (!canConvertTo(format)) {
        throw UnsupportedFormatException("Cannot convert to " +
                                         std::string(formatToString(format)));
    }

    if (format == DiskFormat::AppleDO) {
        auto doImage = std::make_unique<AppleDOImage>();
        doImage->create(m_geometry);

        // Decode each track and copy sectors
        for (size_t track = 0; track < m_geometry.tracks; ++track) {
            size_t offset = track * m_trackSize;
            std::vector<uint8_t> rawTrack(m_data.begin() + offset,
                                           m_data.begin() + offset + m_trackSize);

            auto sectors = NibbleEncoder::parseTrack(rawTrack,
                                                      static_cast<uint8_t>(track));

            for (size_t sector = 0; sector < 16; ++sector) {
                doImage->writeSector(track, 0, sector, sectors[sector]);
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
        try {
            size_t offset = t * m_trackSize;
            std::vector<uint8_t> rawTrack(m_data.begin() + offset,
                                           m_data.begin() + offset + m_trackSize);

            auto sectors = NibbleEncoder::parseTrack(rawTrack, static_cast<uint8_t>(t));

            // Check that we got at least some valid sectors
            int validSectors = 0;
            for (const auto& sector : sectors) {
                if (!sector.empty()) ++validSectors;
            }
            if (validSectors < 10) return false;
        } catch (...) {
            return false;
        }
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
    oss << "Volume Number: " << static_cast<int>(m_volumeNumber) << "\n";
    oss << "Write Protected: " << (m_writeProtected ? "Yes" : "No") << "\n";
    oss << "Modified: " << (m_modified ? "Yes" : "No") << "\n";

    // Count cached/dirty tracks
    int cached = 0, dirty = 0;
    for (size_t t = 0; t < TRACKS_35; ++t) {
        if (m_trackDecoded[t]) ++cached;
        if (m_trackDirty[t]) ++dirty;
    }
    oss << "Cached Tracks: " << cached << "\n";
    oss << "Dirty Tracks: " << dirty << "\n";

    return oss.str();
}

} // namespace rde
