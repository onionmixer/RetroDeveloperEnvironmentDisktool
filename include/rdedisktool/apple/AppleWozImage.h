#ifndef RDEDISKTOOL_APPLE_WOZIMAGE_H
#define RDEDISKTOOL_APPLE_WOZIMAGE_H

#include "rdedisktool/apple/AppleDiskImage.h"
#include "rdedisktool/CRC.h"
#include <array>
#include <map>

namespace rde {

/**
 * WOZ disk image format (.woz)
 *
 * WOZ is a modern flux-level disk image format that can accurately
 * represent copy-protected disks. This implementation supports both
 * WOZ v1 and WOZ v2 formats.
 *
 * Structure:
 * - 12-byte header: "WOZ1" or "WOZ2" + 0xFF 0x0A 0x0D 0x0A + CRC32
 * - Chunks: INFO, TMAP, TRKS (required), META, WRIT (optional)
 */
class AppleWozImage : public AppleDiskImage {
public:
    // WOZ constants
    static constexpr uint32_t WOZ1_MAGIC = 0x315A4F57;  // "WOZ1"
    static constexpr uint32_t WOZ2_MAGIC = 0x325A4F57;  // "WOZ2"
    static constexpr size_t WOZ_HEADER_SIZE = 12;
    static constexpr size_t WOZ1_TRACK_SIZE = 6656;
    static constexpr size_t WOZ2_BITS_BLOCK = 512;

    // Chunk IDs
    static constexpr uint32_t CHUNK_INFO = 0x4F464E49;  // "INFO"
    static constexpr uint32_t CHUNK_TMAP = 0x50414D54;  // "TMAP"
    static constexpr uint32_t CHUNK_TRKS = 0x534B5254;  // "TRKS"
    static constexpr uint32_t CHUNK_META = 0x4154454D;  // "META"
    static constexpr uint32_t CHUNK_WRIT = 0x54495257;  // "WRIT"

    AppleWozImage();
    ~AppleWozImage() override = default;

    //=========================================================================
    // DiskImage Interface
    //=========================================================================

    void load(const std::filesystem::path& path) override;
    void save(const std::filesystem::path& path = {}) override;
    void create(const DiskGeometry& geometry) override;

    DiskFormat getFormat() const override {
        return m_wozVersion == 1 ? DiskFormat::AppleWOZ1 : DiskFormat::AppleWOZ2;
    }

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
    // WOZ-Specific Methods
    //=========================================================================

    /**
     * Get the WOZ format version (1 or 2)
     */
    uint8_t getWozVersion() const { return m_wozVersion; }

    /**
     * Get metadata key-value pairs
     */
    const std::map<std::string, std::string>& getMetadata() const { return m_metadata; }

    /**
     * Set a metadata value
     */
    void setMetadata(const std::string& key, const std::string& value);

    /**
     * Get disk type from INFO chunk
     * 1 = 5.25" 140KB, 2 = 3.5" 800KB
     */
    uint8_t getDiskType() const { return m_diskType; }

    /**
     * Get boot sector format from INFO chunk
     * 0 = Unknown, 1 = 16-sector, 2 = 13-sector, 3 = Both
     */
    uint8_t getBootSectorFormat() const { return m_bootSectorFormat; }

    /**
     * Check if this is a synchronized disk (has timing info)
     */
    bool isSynchronized() const { return m_synchronized; }

    /**
     * Get raw track bit data for a quarter-track
     * @param quarterTrack Quarter-track number (0-159)
     */
    std::vector<uint8_t> getTrackBits(size_t quarterTrack) const;

    /**
     * Get bit count for a track (WOZ2 only)
     */
    uint32_t getTrackBitCount(size_t quarterTrack) const;

protected:
    size_t calculateOffset(size_t track, size_t sector) const override;

private:
    // WOZ header and version
    uint8_t m_wozVersion = 2;

    // INFO chunk data
    uint8_t m_diskType = 1;           // 1 = 5.25", 2 = 3.5"
    bool m_writeProtected = false;
    bool m_synchronized = false;
    bool m_cleaned = false;
    std::string m_creator;
    uint8_t m_diskSides = 1;
    uint8_t m_bootSectorFormat = 0;
    uint8_t m_optimalBitTiming = 32;  // 4us per bit

    // TMAP chunk - maps quarter-tracks to TRKS index
    std::array<uint8_t, 160> m_trackMap = {};

    // Track data storage
    struct TrackInfo {
        std::vector<uint8_t> bits;      // Bit stream data
        uint32_t bitCount = 0;           // Number of valid bits
        uint16_t bytesUsed = 0;          // Bytes of data
        uint16_t startingBlock = 0;      // WOZ2: starting block
        uint16_t blockCount = 0;         // WOZ2: number of blocks
    };
    std::vector<TrackInfo> m_tracks;

    // Metadata
    std::map<std::string, std::string> m_metadata;

    // Decoded sector cache
    std::array<std::array<std::vector<uint8_t>, 16>, TRACKS_35> m_decodedSectors;
    std::array<bool, TRACKS_35> m_sectorsCached = {};

    // Parsing helpers
    void parseWozHeader();
    void parseInfoChunk(const uint8_t* data, size_t size);
    void parseTmapChunk(const uint8_t* data, size_t size);
    void parseTrksChunk(const uint8_t* data, size_t size);
    void parseMetaChunk(const uint8_t* data, size_t size);

    // Building helpers
    std::vector<uint8_t> buildWozFile() const;
    std::vector<uint8_t> buildInfoChunk() const;
    std::vector<uint8_t> buildTmapChunk() const;
    std::vector<uint8_t> buildTrksChunk() const;
    std::vector<uint8_t> buildMetaChunk() const;

    // Decoding helpers
    void decodeSectorsForTrack(size_t track);
    std::vector<uint8_t> nibblizeTrack(size_t track) const;
};

} // namespace rde

#endif // RDEDISKTOOL_APPLE_WOZIMAGE_H
