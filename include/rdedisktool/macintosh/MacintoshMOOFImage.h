#ifndef RDEDISKTOOL_MACINTOSH_MACINTOSHMOOFIMAGE_H
#define RDEDISKTOOL_MACINTOSH_MACINTOSHMOOFIMAGE_H

#include "rdedisktool/macintosh/MacintoshDiskImage.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace rde {

/**
 * Applesauce MOOF disk image.
 *
 * MOOF is a flux/bitstream Macintosh floppy image format. Spec:
 *   https://applesaucefdc.com/moof-reference/
 *
 * Layout (little-endian):
 *   0x00..0x0b   header: "MOOF\xff\x0a\x0d\x0a" + CRC32-ISO-HDLC
 *   0x0c..       chunk stream (INFO, TMAP, TRKS, FLUX, META, ...)
 *
 * E1 scope: read-only, GCR 400K / 800K only. MFM 1.4M (E2) and write
 * (E3/E4) are explicit follow-ups. Until GCR decode lands, load() parses
 * the chunks, validates CRC, then throws NotImplementedException so
 * downstream handlers don't see uninitialized data.
 */
class MacintoshMOOFImage : public MacintoshDiskImage {
public:
    MacintoshMOOFImage();
    ~MacintoshMOOFImage() override = default;

    DiskFormat getFormat() const override { return DiskFormat::MacMOOF; }

    void load(const std::filesystem::path& path) override;
    void save(const std::filesystem::path& path) override;
    void create(const DiskGeometry& geometry) override;

    bool canConvertTo(DiskFormat format) const override;
    std::unique_ptr<DiskImage> convertTo(DiskFormat format) const override;

    bool validate() const override;
    std::string getDiagnostics() const override;

    // Disk type from the INFO chunk.
    enum class DiskType : uint8_t {
        Unknown        = 0,
        SsDdGcr400K    = 1,  // Single-sided 400K GCR
        DsDdGcr800K    = 2,  // Double-sided 800K GCR
        DsHdMfm144M    = 3,  // 1.44M MFM (E2 scope)
        Twiggy         = 4,
    };

    struct InfoChunk {
        uint8_t  version = 0;
        DiskType diskType = DiskType::Unknown;
        bool     writeProtected = false;
        bool     synchronized = false;
        uint8_t  optimalBitTiming = 0;          // 125ns increments (GCR=16, MFM=8)
        std::string creator;                     // 32-byte UTF-8, trimmed
        uint16_t largestTrackBlocks = 0;
        uint16_t fluxBlock = 0;                  // 0 = no FLUX chunk
        uint16_t largestFluxTrackBlocks = 0;
    };

    // Per-track entry in the TRKS chunk (8 bytes).
    struct TrkEntry {
        uint16_t startBlock = 0;
        uint16_t blockCount = 0;
        uint32_t bitOrByteCount = 0;             // bits for bitstream, bytes for flux
    };

    // Read accessors for diagnostics & later phases.
    const InfoChunk& info() const { return m_info; }
    const std::array<uint8_t, 160>& tmap() const { return m_tmap; }
    const std::array<uint8_t, 160>& fluxMap() const { return m_fluxMap; }
    bool hasFluxChunk() const { return m_hasFluxChunk; }
    const std::array<TrkEntry, 160>& trks() const { return m_trks; }
    const std::unordered_map<std::string, std::string>& meta() const { return m_meta; }

private:
    InfoChunk m_info{};
    std::array<uint8_t, 160> m_tmap{};
    std::array<uint8_t, 160> m_fluxMap{};
    bool m_hasFluxChunk = false;
    std::array<TrkEntry, 160> m_trks{};
    std::unordered_map<std::string, std::string> m_meta;
    std::vector<uint8_t> m_fileBytes;            // full original file (for chunk back-references)

    // Helpers — parse the file once and populate the m_* fields. Throws
    // InvalidFormatException on malformed structure or CRC failure.
    void parseChunks(const std::vector<uint8_t>& bytes);

    // CRC32-ISO-HDLC (Gary S. Brown 1986) over bytes [12..end). Returns
    // 0 when caller asked to skip (header CRC == 0).
    static uint32_t computeCrc32(const uint8_t* data, size_t len);
};

} // namespace rde

#endif // RDEDISKTOOL_MACINTOSH_MACINTOSHMOOFIMAGE_H
