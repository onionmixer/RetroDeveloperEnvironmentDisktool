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

    // Sector size codes (N value in IDAM)
    enum SectorSizeCode : uint8_t {
        SIZE_128  = 0,  // 128 bytes
        SIZE_256  = 1,  // 256 bytes
        SIZE_512  = 2,  // 512 bytes (MSX standard)
        SIZE_1024 = 3   // 1024 bytes
    };

    /**
     * Get sector size in bytes from size code
     */
    static size_t sectorSizeFromCode(uint8_t code) {
        return 128 << code;  // 128, 256, 512, 1024
    }

    /**
     * Get size code from sector size in bytes
     */
    static uint8_t sectorSizeToCode(size_t size) {
        if (size <= 128) return SIZE_128;
        if (size <= 256) return SIZE_256;
        if (size <= 512) return SIZE_512;
        return SIZE_1024;
    }

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

    /**
     * Override getBootSector to read from DMK track data
     */
    const uint8_t* getBootSector() const override;

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

    /**
     * Enable or disable CRC verification on sector read
     * @param enable true to enable CRC checking
     * @param strict true to throw exception on CRC error, false to just warn
     */
    void setCRCVerification(bool enable, bool strict = false) {
        m_verifyCRC = enable;
        m_strictCRC = strict;
    }

    /**
     * Get CRC verification status
     */
    bool isCRCVerificationEnabled() const { return m_verifyCRC; }

    /**
     * Set interleave factor for formatting
     * @param interleave Interleave factor (1 = no interleave, 2 = every other sector, etc.)
     */
    void setInterleave(uint8_t interleave) { m_interleave = interleave; }

    /**
     * Get current interleave factor
     */
    uint8_t getInterleave() const { return m_interleave; }

protected:
    size_t calculateOffset(size_t track, size_t side, size_t sector) const override;

private:
    // DMK header info
    uint8_t m_numTracks = 80;
    uint16_t m_trackLength = DMK_TRACK_LENGTH_DD;
    uint8_t m_dmkFlags = 0;

    // Format options
    uint8_t m_interleave = 1;  // 1 = no interleave
    bool m_verifyCRC = false;  // Verify CRC on read
    bool m_strictCRC = false;  // Throw exception on CRC error

    // Cached boot sector for getBootSector()
    mutable SectorBuffer m_cachedBootSector;
    mutable bool m_bootSectorCached = false;

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
