#ifndef RDEDISKTOOL_MSX_XSACOMPRESSOR_H
#define RDEDISKTOOL_MSX_XSACOMPRESSOR_H

#include <array>
#include <cstdint>
#include <vector>
#include <string>

namespace rde {

/**
 * XSA (eXtendable Storage Archive) Compressor
 *
 * Compresses data to XSA format used by MSX.
 * XSA uses LZ77 compression with adaptive Huffman coding for distance encoding.
 *
 * File format:
 * - Bytes 0-3: Magic number "PCK\x08"
 * - Bytes 4-7: Original data length (little-endian)
 * - Bytes 8-11: Compressed data length (little-endian)
 * - Bytes 12+: Original filename (null-terminated)
 * - Rest: Compressed data bitstream
 *
 * Algorithm based on XelaSoft XSA implementation.
 */
class XSACompressor {
public:
    /**
     * Construct compressor with optional original filename
     * @param originalFilename Filename to store in XSA header (optional)
     */
    explicit XSACompressor(const std::string& originalFilename = "");

    /**
     * Compress data to XSA format
     * @param data Raw data to compress
     * @return Compressed XSA data
     */
    std::vector<uint8_t> compress(const std::vector<uint8_t>& data);

    /**
     * XSA magic number: "PCK\x08"
     */
    static constexpr std::array<uint8_t, 4> MAGIC = {'P', 'C', 'K', 0x08};

private:
    // Constants
    static constexpr int MAX_STR_LEN = 254;         // Maximum LZ77 match length
    static constexpr int TBL_SIZE = 16;             // Huffman table size
    static constexpr int SLIDING_WINDOW_BITS = 13;  // 2^13 = 8192
    static constexpr int SLIDING_WINDOW_SIZE = 1 << SLIDING_WINDOW_BITS;
    static constexpr int SLIDING_WINDOW_MASK = SLIDING_WINDOW_SIZE - 1;
    static constexpr int MAX_HUF_CNT = 127;         // Huffman update interval
    static constexpr int LOG_NR_INDEX_PTRS = 8;     // 2^8 = 256 pointers per bucket
    static constexpr int NR_INDEX_PTRS = 1 << LOG_NR_INDEX_PTRS;
    static constexpr int INDEX_PTR_MASK = NR_INDEX_PTRS - 1;
    static constexpr int OUTPUT_BUF_SIZE = 32768;   // Output buffer size

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

    /**
     * Huffman code for encoding
     */
    struct HufCode {
        int nrBits = 0;
        uint16_t bitCode = 0;
    };

    /**
     * String index info for pattern matching
     */
    struct IndexInfo {
        uint16_t firstPtr = 0;
        uint16_t nrPtrs = 0;
    };

    // Output methods
    void bitOut(bool bit);
    void byteOut(uint8_t byte);
    void charOut(uint8_t ch);
    void strOut(const uint8_t* str, uint8_t strLen, uint16_t strPos);
    void flushOut();

    // Header methods
    void writeHeader(const std::string& filename);
    void updateLengths(uint32_t orgLen, uint32_t compLen);

    // String matching (LZ77)
    void initIndex();
    void fastAddString(uint16_t winPos);
    uint16_t addString(uint16_t winPos, uint16_t& matchPos);
    void deleteString(uint16_t strPos);

    // Huffman table
    void initHufInfo();
    void mkHufTbl();
    void convertToHufCodes(HufNode* node, uint16_t bitCode, int nrBits);

    // Helper
    uint8_t findMsb(uint8_t n);

    // State
    std::string m_originalFilename;
    std::vector<uint8_t> m_output;

    // Sliding window
    std::vector<uint8_t> m_window;
    uint16_t m_winPos;
    uint16_t m_freePos;
    uint16_t m_lookaheadCnt;
    uint16_t m_strLen;
    uint16_t m_strPos;
    uint16_t m_replaceCnt;

    // String index (256 buckets)
    std::vector<std::vector<const uint8_t*>> m_stringPtrs;
    std::vector<IndexInfo> m_indexInfo;

    // Huffman state
    int m_updHufCnt;
    std::array<int, TBL_SIZE + 1> m_cpDist;
    std::array<int, TBL_SIZE> m_cpDbMask;
    std::array<int, TBL_SIZE> m_tblSizes;
    std::array<HufNode, 2 * TBL_SIZE - 1> m_hufTbl;
    std::array<HufCode, TBL_SIZE> m_hufCodeTbl;

    // Bit output state
    std::vector<uint8_t> m_byteBuf;
    size_t m_byteCnt;
    size_t m_bitFlgCnt;
    uint8_t m_bitFlg;
    uint8_t m_setFlg;
    size_t m_nrWritten;
};

} // namespace rde

#endif // RDEDISKTOOL_MSX_XSACOMPRESSOR_H
