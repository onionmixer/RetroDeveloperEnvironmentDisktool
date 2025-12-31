#ifndef RDEDISKTOOL_APPLE_NIBIMAGE_H
#define RDEDISKTOOL_APPLE_NIBIMAGE_H

#include "rdedisktool/apple/AppleDiskImage.h"
#include "rdedisktool/apple/NibbleEncoder.h"
#include <array>

namespace rde {

/**
 * Apple II Nibble disk image (.nib)
 *
 * NIB format stores raw nibblized track data:
 * - 35 tracks × 6656 bytes per track = 232,960 bytes
 * - Each track contains GCR-encoded sectors with address and data fields
 * - Self-sync bytes (0xFF) between sectors
 *
 * Variant: NB2 format uses 6384 bytes per track (223,440 bytes total)
 */
class AppleNibImage : public AppleDiskImage {
public:
    static constexpr size_t NIB_TRACK_SIZE = NibbleEncoder::TRACK_NIBBLE_SIZE;  // 6656
    static constexpr size_t NB2_TRACK_SIZE = NibbleEncoder::TRACK_NIBBLE_SIZE_NB2;  // 6384
    static constexpr size_t NIB_DISK_SIZE = TRACKS_35 * NIB_TRACK_SIZE;  // 232960
    static constexpr size_t NB2_DISK_SIZE = TRACKS_35 * NB2_TRACK_SIZE;  // 223440

    AppleNibImage();
    ~AppleNibImage() override = default;

    //=========================================================================
    // DiskImage Interface
    //=========================================================================

    void load(const std::filesystem::path& path) override;
    void save(const std::filesystem::path& path = {}) override;
    void create(const DiskGeometry& geometry) override;

    DiskFormat getFormat() const override { return m_format; }

    SectorBuffer readSector(size_t track, size_t side, size_t sector) override;
    void writeSector(size_t track, size_t side, size_t sector,
                    const SectorBuffer& data) override;

    TrackBuffer readTrack(size_t track, size_t side) override;
    void writeTrack(size_t track, size_t side, const TrackBuffer& data) override;

    bool canConvertTo(DiskFormat format) const override;
    std::unique_ptr<DiskImage> convertTo(DiskFormat format) const override;

    bool validate() const override;
    std::string getDiagnostics() const override;

    //=========================================================================
    // AppleDiskImage Interface
    //=========================================================================

    SectorOrder getSectorOrder() const override { return SectorOrder::Physical; }

    //=========================================================================
    // NIB-Specific Methods
    //=========================================================================

    /**
     * Get the track size for this image
     */
    size_t getTrackSize() const { return m_trackSize; }

    /**
     * Get decoded sector data for a track (cached)
     */
    const std::array<std::vector<uint8_t>, 16>& getDecodedTrack(size_t track);

    /**
     * Rebuild nibble track from decoded sectors
     */
    void rebuildTrack(size_t track);

    /**
     * Get/set volume number (used in address fields)
     */
    uint8_t getVolumeNumber() const { return m_volumeNumber; }
    void setVolumeNumber(uint8_t vol) { m_volumeNumber = vol; }

protected:
    size_t calculateOffset(size_t track, size_t sector) const override;

private:
    DiskFormat m_format = DiskFormat::AppleNIB;
    size_t m_trackSize = NIB_TRACK_SIZE;
    uint8_t m_volumeNumber = 254;  // Default DOS 3.3 volume

    // Cached decoded sectors per track
    std::array<std::array<std::vector<uint8_t>, 16>, TRACKS_35> m_decodedTracks;
    std::array<bool, TRACKS_35> m_trackDecoded = {};
    std::array<bool, TRACKS_35> m_trackDirty = {};

    void decodeTrackIfNeeded(size_t track);
    void invalidateTrackCache(size_t track);
};

} // namespace rde

#endif // RDEDISKTOOL_APPLE_NIBIMAGE_H
