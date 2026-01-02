#ifndef RDEDISKTOOL_MSX_XSAIMAGE_H
#define RDEDISKTOOL_MSX_XSAIMAGE_H

#include "rdedisktool/msx/MSXDiskImage.h"

namespace rde {

/**
 * MSX XSA compressed disk image (.xsa)
 *
 * XSA (eXtendable Storage Archive) is a compressed disk image format
 * developed by XelaSoft for MSX computers in 1994.
 *
 * Features:
 * - LZ77 compression with adaptive Huffman coding
 * - Supports standard MSX disk geometries (360KB, 720KB)
 * - Can be converted to/from DSK or DMK format
 * - Bi-directional conversion: DSK/DMK -> XSA and XSA -> DSK/DMK
 */
class MSXXSAImage : public MSXDiskImage {
public:
    MSXXSAImage();
    ~MSXXSAImage() override = default;

    //=========================================================================
    // DiskImage Interface
    //=========================================================================

    /**
     * Load XSA compressed disk image
     * @param path Path to the .xsa file
     * @throws FileNotFoundException if file doesn't exist
     * @throws InvalidFormatException if not a valid XSA file
     */
    void load(const std::filesystem::path& path) override;

    /**
     * Save disk image in XSA compressed format
     * @param path Path to save the .xsa file (uses original path if empty)
     */
    void save(const std::filesystem::path& path = {}) override;

    /**
     * Create is not supported for XSA format (read-only)
     * @throws NotImplementedException always
     */
    void create(const DiskGeometry& geometry) override;

    DiskFormat getFormat() const override { return DiskFormat::MSXXSA; }

    /**
     * Get write protection status
     * Note: XSA in-memory data can be modified, but only saved as compressed XSA
     */
    bool isWriteProtected() const override { return m_writeProtected; }

    /**
     * Set write protection status
     */
    void setWriteProtected(bool protect) override;

    SectorBuffer readSector(size_t track, size_t side, size_t sector) override;

    /**
     * Write sector data
     * Changes are kept in memory and saved when save() is called
     */
    void writeSector(size_t track, size_t side, size_t sector,
                    const SectorBuffer& data) override;

    TrackBuffer readTrack(size_t track, size_t side) override;

    /**
     * Write track data
     * Changes are kept in memory and saved when save() is called
     */
    void writeTrack(size_t track, size_t side, const TrackBuffer& data) override;

    /**
     * XSA can be converted to uncompressed formats
     */
    bool canConvertTo(DiskFormat format) const override;

    /**
     * Convert to DSK or DMK format
     */
    std::unique_ptr<DiskImage> convertTo(DiskFormat format) const override;

    bool validate() const override;
    std::string getDiagnostics() const override;

    //=========================================================================
    // XSA-Specific Methods
    //=========================================================================

    /**
     * Check if data appears to be valid XSA format
     * @param data Data to check
     * @return true if data starts with XSA magic number
     */
    static bool isXSAFormat(const std::vector<uint8_t>& data);

    /**
     * Create XSA image from raw (uncompressed) disk data
     * @param rawData Raw disk image data
     * @param originalFilename Filename to store in XSA header
     * @return New XSA image containing compressed data
     */
    static std::unique_ptr<MSXXSAImage> createFromRawData(
        const std::vector<uint8_t>& rawData,
        const std::string& originalFilename = "");

    /**
     * Get the compressed data for this image
     * @return XSA compressed data
     */
    std::vector<uint8_t> getCompressedData() const;

private:
    // Store original compressed data for diagnostics
    size_t m_compressedSize = 0;
    std::string m_originalFilename;
};

} // namespace rde

#endif // RDEDISKTOOL_MSX_XSAIMAGE_H
