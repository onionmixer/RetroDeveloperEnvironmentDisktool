#ifndef RDEDISKTOOL_XSA_HEADER_H
#define RDEDISKTOOL_XSA_HEADER_H

#include <cstdint>
#include <string>
#include <vector>

namespace rdedisktool {

/**
 * XSA (eXtended Size Archive) file header structure
 *
 * XSA format layout:
 *   Offset 0-3:   Magic bytes "PCK\x08"
 *   Offset 4-7:   Original (uncompressed) length (32-bit LE)
 *   Offset 8-11:  Compressed length (32-bit LE)
 *   Offset 12+:   Original filename (null-terminated string)
 *   After null:   Compressed data begins
 */
struct XSAHeader {
    /// Magic bytes identifying XSA format
    static constexpr uint8_t MAGIC[4] = {'P', 'C', 'K', 0x08};

    /// Minimum header size: magic(4) + lengths(8) + null(1)
    static constexpr size_t HEADER_MIN_SIZE = 13;

    /// Original (uncompressed) data length
    uint32_t originalLength = 0;

    /// Compressed data length
    uint32_t compressedLength = 0;

    /// Original filename (without path)
    std::string originalFilename;

    /**
     * Read header from data buffer
     * @param data Data buffer
     * @param size Buffer size
     * @return Bytes consumed (header size), or 0 on error
     */
    size_t read(const uint8_t* data, size_t size);

    /**
     * Read header from vector
     * @param data Data vector
     * @return Bytes consumed (header size), or 0 on error
     */
    size_t read(const std::vector<uint8_t>& data);

    /**
     * Write header to output buffer
     * Writes magic, placeholder lengths (0), and filename
     * Call updateLengths() after compression to set actual lengths
     * @param output Output vector
     * @return Bytes written
     */
    size_t write(std::vector<uint8_t>& output) const;

    /**
     * Update lengths in already-written header
     * @param output Output buffer containing header
     * @param orgLen Original (uncompressed) length
     * @param compLen Compressed length
     */
    static void updateLengths(std::vector<uint8_t>& output,
                              uint32_t orgLen, uint32_t compLen);

    /**
     * Check if data starts with XSA magic bytes
     * @param data Data buffer
     * @param size Buffer size
     * @return true if XSA format detected
     */
    static bool isXSAFormat(const uint8_t* data, size_t size);

    /**
     * Check if vector starts with XSA magic bytes
     * @param data Data vector
     * @return true if XSA format detected
     */
    static bool isXSAFormat(const std::vector<uint8_t>& data);

    /**
     * Calculate total header size including filename
     * @return Header size in bytes
     */
    size_t headerSize() const;

    /**
     * Clear all header fields
     */
    void clear();
};

} // namespace rdedisktool

#endif // RDEDISKTOOL_XSA_HEADER_H
