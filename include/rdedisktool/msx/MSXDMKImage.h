#ifndef RDEDISKTOOL_MSX_DMKIMAGE_H
#define RDEDISKTOOL_MSX_DMKIMAGE_H

#include "rdedisktool/msx/MSXDiskImage.h"
#include <array>

namespace rde {

/**
 * MSX DMK disk image format (.dmk)
 *
 * DMK (David's Disk Image) format stores raw track data including:
 * - IDAM (ID Address Mark) pointers for each sector
 * - Full track data with gaps, sync bytes, and CRC
 *
 * Structure:
 * - 16-byte header
 * - Track data for each track (size varies)
 *
 * Header:
 * - Byte 0: Write protect (0x00 = writable, 0xFF = protected)
 * - Byte 1: Number of tracks
 * - Bytes 2-3: Track length (little-endian)
 * - Byte 4: Flags
 *   - Bit 4: Single-sided if set
 *   - Bit 6: Single density if set
 *   - Bit 7: Ignore density if set
 */
class MSXDMKImage : public MSXDiskImage {
public:
    // DMK constants
    static constexpr size_t DMK_HEADER_SIZE = 16;
    static constexpr size_t DMK_IDAM_COUNT = 64;  // Max IDAMs per track
    static constexpr size_t DMK_IDAM_SIZE = 128;  // IDAM table size (64 * 2 bytes)

    // Default track lengths
    static constexpr size_t DMK_TRACK_LENGTH_DD = 6250;   // Double density
    static constexpr size_t DMK_TRACK_LENGTH_SD = 3125;   // Single density

    MSXDMKImage();
    ~MSXDMKImage() override = default;

    //=========================================================================
    // DiskImage Interface
    //=========================================================================

    void load(const std::filesystem::path& path) override;
    void save(const std::filesystem::path& path = {}) override;
    void create(const DiskGeometry& geometry) override;

    DiskFormat getFormat() const override { return DiskFormat::MSXDMK; }

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
    // DMK-Specific Methods
    //=========================================================================

    /**
     * Get DMK header data
     */
    struct DMKHeader {
        uint8_t writeProtect;
        uint8_t numTracks;
        uint16_t trackLength;
        uint8_t flags;
    };

    DMKHeader getDMKHeader() const;

    /**
     * Check if single-sided (from flags)
     */
    bool isSingleSided() const;

    /**
     * Check if single-density (from flags)
     */
    bool isSingleDensity() const;

    /**
     * Get IDAM table for a track
     * @return Array of 64 IDAM offsets (0 = unused)
     */
    std::array<uint16_t, DMK_IDAM_COUNT> getIDAMTable(size_t track, size_t side) const;

    /**
     * Find sector within track data using IDAM table
     * @param trackData Track data including IDAM table
     * @param sector Sector number to find (1-based for MSX)
     * @return Offset to sector data, or -1 if not found
     */
    int findSectorInTrack(const TrackBuffer& trackData, size_t sector) const;

protected:
    size_t calculateOffset(size_t track, size_t side, size_t sector) const override;

private:
    // DMK header info
    uint8_t m_numTracks = 80;
    uint16_t m_trackLength = DMK_TRACK_LENGTH_DD;
    uint8_t m_dmkFlags = 0;

    // Get offset to track data in file
    size_t getTrackOffset(size_t track, size_t side) const;

    // Parse IDAM entry
    struct IDAMEntry {
        bool doubleDensity;
        uint16_t offset;
        uint8_t track;
        uint8_t side;
        uint8_t sector;
        uint8_t sizeCode;
    };

    IDAMEntry parseIDAM(const uint8_t* data, uint16_t idamPtr) const;

    // Build a complete track with MFM encoding
    TrackBuffer buildFormattedTrack(size_t track, size_t side,
                                    const std::array<SectorBuffer, 9>& sectors);
};

} // namespace rde

#endif // RDEDISKTOOL_MSX_DMKIMAGE_H
