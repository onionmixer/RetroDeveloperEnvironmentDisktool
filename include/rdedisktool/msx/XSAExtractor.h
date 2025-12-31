#ifndef RDEDISKTOOL_MSX_XSAEXTRACTOR_H
#define RDEDISKTOOL_MSX_XSAEXTRACTOR_H

#include <array>
#include <cstdint>
#include <vector>
#include <stdexcept>

namespace rde {

/**
 * XSA (eXtendable Storage Archive) Extractor
 *
 * Decompresses XSA compressed disk images used by MSX.
 * XSA uses LZ77 compression with adaptive Huffman coding.
 *
 * File format:
 * - Bytes 0-3: Magic number "PCK\x08"
 * - Bytes 4-7: Original data length (little-endian)
 * - Bytes 8-11: Compressed data length (skipped)
 * - Bytes 12+: Original filename (null-terminated)
 * - Rest: Compressed data
 *
 * Algorithm based on openMSX implementation.
 */
class XSAExtractor {
public:
    /**
     * XSA-specific exception
     */
    class XSAException : public std::runtime_error {
    public:
        explicit XSAException(const std::string& message)
            : std::runtime_error(message) {}
    };

    /**
     * Construct extractor with compressed data
     * @param data The compressed XSA file data
     * @throws XSAException if data is invalid
     */
    explicit XSAExtractor(const std::vector<uint8_t>& data);

    /**
     * Extract and return decompressed data
     * @return Decompressed disk image data
     */
    std::vector<uint8_t> extract();

    /**
     * Check if data appears to be valid XSA format
     * @param data Data to check
     * @return true if data starts with XSA magic number
     */
    static bool isXSAFormat(const std::vector<uint8_t>& data);

    /**
     * XSA magic number: "PCK\x08"
     */
    static constexpr std::array<uint8_t, 4> MAGIC = {'P', 'C', 'K', 0x08};

private:
    // Constants
    static constexpr int MAX_STR_LEN = 254;   // Maximum string length for LZ77 match
    static constexpr int TBL_SIZE = 16;       // Huffman table size
    static constexpr int MAX_HUF_CNT = 127;   // Huffman update counter max

    // Extra bits for distance codes
    static constexpr std::array<uint8_t, TBL_SIZE> cpdExt = {
        0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12
    };

    /**
     * Huffman tree node
     */
    struct HufNode {
        HufNode* child1 = nullptr;
        HufNode* child2 = nullptr;
        int weight = 0;
    };

    // Input handling
    uint8_t charIn();
    bool bitIn();

    // Header processing
    void checkHeader();

    // Decompression
    void unLz77();
    unsigned rdStrLen();
    int rdStrPos();

    // Huffman table management
    void initHufInfo();
    void mkHufTbl();

    // State
    const uint8_t* m_inputPtr;
    const uint8_t* m_inputEnd;
    std::vector<uint8_t> m_output;

    // Huffman state
    int m_updHufCnt;
    std::array<int, TBL_SIZE + 1> m_cpDist;
    std::array<int, TBL_SIZE> m_tblSizes;
    std::array<HufNode, 2 * TBL_SIZE - 1> m_hufTbl;

    // Bit reading state
    uint8_t m_bitFlg;
    uint8_t m_bitCnt;
};

} // namespace rde

#endif // RDEDISKTOOL_MSX_XSAEXTRACTOR_H
