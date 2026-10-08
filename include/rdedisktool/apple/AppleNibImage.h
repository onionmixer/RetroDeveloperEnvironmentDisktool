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
 *
 * readSector/writeSector take DOS 3.3 logical sector numbers (same as .do);
 * address fields on the track carry the physical sector number.
 */
class AppleNibImage : public AppleDiskImage {
public:
    static constexpr size_t NIB_TRACK_SIZE = NibbleEncoder::TRACK_NIBBLE_SIZE;  // 6656
    static constexpr size_t NB2_TRACK_SIZE = NibbleEncoder::TRACK_NIBBLE_SIZE_NB2;  // 6384
    static constexpr size_t NIB_DISK_SIZE = TRACKS_35 * NIB_TRACK_SIZE;  // 232960
    static constexpr size_t NB2_DISK_SIZE = TRACKS_35 * NB2_TRACK_SIZE;  // 223440

    // format selects the track size for create(): AppleNIB (6656) or AppleNIB2 (6384)
    explicit AppleNibImage(DiskFormat format = DiskFormat::AppleNIB);
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

    // DOS 3.3 logical numbers; 13-sector (DOS 3.2) images number physically
    SectorOrder getSectorOrder() const override {
        return m_sectors13 ? SectorOrder::Physical : SectorOrder::DOS;
    }

    /** True when the tracks are 13-sector (DOS 3.2, 5-and-3) */
    bool isThirteenSector() const { return m_sectors13; }

    //=========================================================================
    // NIB-Specific Methods
    //=========================================================================

    /**
     * Get the track size for this image
     */
    size_t getTrackSize() const { return m_trackSize; }



    /**
     * Get/set volume number (used in address fields)
     */
    uint8_t getVolumeNumber() const { return m_volumeNumber; }
    void setVolumeNumber(uint8_t vol) { m_volumeNumber = vol; }

protected:
    size_t calculateOffset(size_t track, size_t sector) const override;
    const std::vector<uint8_t>& detectionImage() const override;

public:
    std::vector<std::string> readWarnings() const override;

protected:

private:
    DiskFormat m_format = DiskFormat::AppleNIB;
    size_t m_trackSize = NIB_TRACK_SIZE;
    uint8_t m_volumeNumber = 254;  // Default DOS 3.3 volume
    bool m_sectors13 = false;      // DOS 3.2 tracks (D5 AA B5, 5-and-3)

    // Cached decoded sectors per track (indexed by DOS logical sector)
    std::array<NibbleEncoder::ParsedTrack, TRACKS_35> m_decodedTracks;
    std::array<bool, TRACKS_35> m_trackDecoded = {};

    // DOS-order sector image for file system detection
    mutable std::vector<uint8_t> m_detectionImage;
    mutable bool m_detectionValid = false;

    NibbleEncoder::ParsedTrack parseRawTrack(size_t track) const;
    void decodeTrackIfNeeded(size_t track);
    void invalidateTrackCache(size_t track);
    void invalidateDetection();
};

} // namespace rde

#endif // RDEDISKTOOL_APPLE_NIBIMAGE_H
