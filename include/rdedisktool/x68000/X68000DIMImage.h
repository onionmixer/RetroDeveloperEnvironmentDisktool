#ifndef RDEDISKTOOL_X68000_DIMIMAGE_H
#define RDEDISKTOOL_X68000_DIMIMAGE_H

#include "rdedisktool/x68000/X68000DiskImage.h"

namespace rde {

/**
 * X68000 DIM (Disk Image) format
 *
 * DIM is a flexible disk image format with a 256-byte header:
 * - Supports multiple disk types (2HD, 2HS, 2HC, 2HDE, 2HQ)
 * - Per-track existence flags for sparse images
 * - Metadata fields (date, time, comment)
 *
 * Header structure (256 bytes):
 *   Offset  Size  Description
 *   0x00    1     Disk type (0-9)
 *   0x01    170   Track flags (1 byte per track)
 *   0xAB    15    Header info
 *   0xBA    4     Date
 *   0xBE    4     Time
 *   0xC2    61    Comment
 *   0xFF    1     Overtrack flag
 */
class X68000DIMImage : public X68000DiskImage {
public:
    X68000DIMImage();
    ~X68000DIMImage() override = default;

    //=========================================================================
    // DiskImage Interface Implementation
    //=========================================================================

    void load(const std::filesystem::path& path) override;
    void save(const std::filesystem::path& path = {}) override;
    void create(const DiskGeometry& geometry) override;

    DiskFormat getFormat() const override { return DiskFormat::X68000DIM; }

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
    // DIM-Specific Methods
    //=========================================================================

    /**
     * Get the DIM disk type
     */
    X68000DIMType getDIMType() const { return m_dimType; }

    /**
     * Set the DIM disk type
     */
    void setDIMType(X68000DIMType type);

    /**
     * Get the comment field from header
     */
    std::string getComment() const;

    /**
     * Set the comment field
     */
    void setComment(const std::string& comment);

    /**
     * Check if a specific track is present in the image
     */
    bool isTrackPresent(size_t track) const;

    /**
     * Set track presence flag
     */
    void setTrackPresent(size_t track, bool present);

    /**
     * Get sector size for current DIM type
     */
    size_t getSectorSize() const;

    /**
     * Get sectors per track for current DIM type
     */
    size_t getSectorsPerTrack() const;

    /**
     * Get track size for current DIM type
     */
    size_t getTrackSize() const;

    /**
     * Get DIM type name
     */
    static const char* getDIMTypeName(X68000DIMType type);

private:
    /**
     * DIM header structure (256 bytes)
     */
    #pragma pack(push, 1)
    struct DIMHeader {
        uint8_t  type;              // 0x00: Disk type (0-9)
        uint8_t  trkflag[170];      // 0x01: Track existence flags
        uint8_t  headerinfo[15];    // 0xAB: Header info
        uint8_t  date[4];           // 0xBA: Date
        uint8_t  time[4];           // 0xBE: Time
        char     comment[61];       // 0xC2: Comment (null-terminated)
        uint8_t  overtrack;         // 0xFF: Overtrack flag
    };
    #pragma pack(pop)

    static_assert(sizeof(DIMHeader) == 256, "DIMHeader must be 256 bytes");

    /**
     * Track sizes for each DIM type
     */
    static constexpr size_t TRACK_SIZES[10] = {
        1024 * 8,   // DIM_2HD:  8192 bytes
        1024 * 9,   // DIM_2HS:  9216 bytes
        512 * 15,   // DIM_2HC:  7680 bytes
        1024 * 9,   // DIM_2HDE: 9216 bytes
        0, 0, 0, 0, 0,
        512 * 18    // DIM_2HQ:  9216 bytes
    };

    /**
     * Sector sizes for each DIM type
     */
    static constexpr size_t SECTOR_SIZES[10] = {
        1024, 1024, 512, 1024, 0, 0, 0, 0, 0, 512
    };

    /**
     * Sectors per track for each DIM type
     */
    static constexpr size_t SECTORS_PER_TRACK[10] = {
        8, 9, 15, 9, 0, 0, 0, 0, 0, 18
    };

    /**
     * Max valid track for each DIM type
     */
    static constexpr size_t MAX_TRACKS[10] = {
        154, 160, 160, 160, 0, 0, 0, 0, 0, 160
    };

    DIMHeader m_header;
    X68000DIMType m_dimType;

    /**
     * Parse header from loaded data
     */
    void parseHeader();

    /**
     * Build header before saving
     */
    void buildHeader();

    /**
     * Calculate byte offset for a given track and sector
     */
    size_t calculateOffset(size_t track, size_t sector) const override;

    /**
     * Validate track/sector parameters
     */
    void validateParameters(size_t track, size_t sector) const;

    /**
     * Check if DIM type is valid
     */
    static bool isValidDIMType(uint8_t type);

    /**
     * Get the data offset (after header, accounting for sparse tracks)
     */
    size_t getDataOffset(size_t track) const;
};

} // namespace rde

#endif // RDEDISKTOOL_X68000_DIMIMAGE_H
