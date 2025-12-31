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
 * This implementation is READ-ONLY. XSA files can be loaded and their
 * contents accessed, but modifications cannot be saved back to XSA format.
 * Use convertTo(DiskFormat::MSXDSK) to save changes in an uncompressed format.
 *
 * Features:
 * - LZ77 compression with adaptive Huffman coding
 * - Supports standard MSX disk geometries (360KB, 720KB)
 * - Can be converted to DSK or DMK format for editing
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
     * Save is not supported for XSA format (read-only)
     * @throws WriteProtectedException always
     */
    void save(const std::filesystem::path& path = {}) override;

    /**
     * Create is not supported for XSA format (read-only)
     * @throws NotImplementedException always
     */
    void create(const DiskGeometry& geometry) override;

    DiskFormat getFormat() const override { return DiskFormat::MSXXSA; }

    /**
     * XSA format is always write-protected
     */
    bool isWriteProtected() const override { return true; }

    /**
     * Cannot change write protection on XSA format
     */
    void setWriteProtected(bool protect) override;

    SectorBuffer readSector(size_t track, size_t side, size_t sector) override;

    /**
     * Writing sectors is not supported for XSA format
     * @throws WriteProtectedException always
     */
    void writeSector(size_t track, size_t side, size_t sector,
                    const SectorBuffer& data) override;

    TrackBuffer readTrack(size_t track, size_t side) override;

    /**
     * Writing tracks is not supported for XSA format
     * @throws WriteProtectedException always
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

private:
    // Store original compressed data for diagnostics
    size_t m_compressedSize = 0;
    std::string m_originalFilename;
};

} // namespace rde

#endif // RDEDISKTOOL_MSX_XSAIMAGE_H
