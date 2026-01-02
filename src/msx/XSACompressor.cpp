#include "rdedisktool/msx/XSACompressor.h"
#include "rdedisktool/msx/XSAHeader.h"
#include "rdedisktool/utils/BinaryReader.h"
#include <algorithm>
#include <cstring>

namespace rde {

XSACompressor::XSACompressor(const std::string& originalFilename)
    : m_originalFilename(originalFilename)
    , m_winPos(0)
    , m_freePos(0)
    , m_lookaheadCnt(0)
    , m_strLen(0)
    , m_strPos(0)
    , m_replaceCnt(0)
    , m_updHufCnt(0)
    , m_byteCnt(0)
    , m_bitFlgCnt(0)
    , m_bitFlg(0)
    , m_setFlg(1)
    , m_nrWritten(0)
{
}

std::vector<uint8_t> XSACompressor::compress(const std::vector<uint8_t>& data) {
    if (data.empty()) {
        // Return minimal valid XSA with just header for empty data
        m_output.clear();
        m_nrWritten = 0;

        // Write header using XSAHeader (lengths remain 0 for empty data)
        rdedisktool::XSAHeader header;
        header.originalFilename = "";  // Empty filename for empty data
        header.write(m_output);

        return m_output;
    }

    // Initialize buffers
    m_output.clear();
    m_output.reserve(data.size());  // Estimate

    m_byteBuf.resize(OUTPUT_BUF_SIZE);
    m_byteCnt = 1;      // First byte after bitflag position
    m_bitFlgCnt = 0;    // Bitflag at position 0
    m_bitFlg = 0;
    m_setFlg = 1;
    m_nrWritten = 0;

    // Initialize sliding window (window + lookahead buffer)
    m_window.resize(SLIDING_WINDOW_SIZE + MAX_STR_LEN);

    // Initialize string index
    m_stringPtrs.resize(256);
    for (auto& bucket : m_stringPtrs) {
        bucket.resize(NR_INDEX_PTRS, nullptr);
    }
    m_indexInfo.resize(256);

    // Initialize Huffman info
    initHufInfo();
    convertToHufCodes(&m_hufTbl[2 * TBL_SIZE - 2], 0, 0);

    // Write header
    writeHeader(m_originalFilename);

    // Fill initial lookahead buffer (up to MAX_STR_LEN bytes)
    m_lookaheadCnt = 0;
    size_t inputPos = 0;
    while (m_lookaheadCnt < MAX_STR_LEN && inputPos < data.size()) {
        m_window[m_lookaheadCnt] = data[inputPos];
        m_window[SLIDING_WINDOW_SIZE + m_lookaheadCnt] = data[inputPos];
        ++m_lookaheadCnt;
        ++inputPos;
    }

    if (m_lookaheadCnt == 0) {
        // No data to compress, just write EOF marker
        strOut(&m_window[0], MAX_STR_LEN + 1, 0);
        flushOut();
        updateLengths(0, static_cast<uint32_t>(m_nrWritten));
        return std::move(m_output);
    }

    // Initialize index (empty at start)
    for (auto& info : m_indexInfo) {
        info.firstPtr = 0;
        info.nrPtrs = 0;
    }

    m_freePos = 0;
    m_winPos = 0;

    // Main compression loop - process all input
    while (m_lookaheadCnt > 0) {
        // Find best match at current position (searches existing index, then adds current)
        m_strLen = addString(m_winPos, m_strPos);

        // Ensure valid distance (not matching self)
        if (m_strLen >= 2) {
            uint16_t distance = (m_winPos - m_strPos) & SLIDING_WINDOW_MASK;
            if (distance == 0 || distance > m_freePos) {
                m_strLen = 0;  // Invalid match, treat as literal
            }
        }

        // Limit match length to available lookahead
        if (m_strLen > m_lookaheadCnt) {
            m_strLen = m_lookaheadCnt;
        }

        // Output literal or match
        if (m_strLen <= 1) {
            // Output single character
            charOut(m_window[m_winPos]);
            m_replaceCnt = 1;
        } else {
            // Output back-reference
            uint16_t distance = (m_winPos - m_strPos) & SLIDING_WINDOW_MASK;
            strOut(&m_window[m_strPos], static_cast<uint8_t>(m_strLen), distance);
            m_replaceCnt = m_strLen;
        }

        // Advance window by m_replaceCnt positions
        while (m_replaceCnt > 0) {
            // Delete string that's leaving the window
            if (m_freePos >= SLIDING_WINDOW_SIZE) {
                deleteString((m_winPos + MAX_STR_LEN) & SLIDING_WINDOW_MASK);
            }

            // Move window position forward
            m_winPos = (m_winPos + 1) & SLIDING_WINDOW_MASK;
            --m_lookaheadCnt;

            // Read new character if available
            if (inputPos < data.size()) {
                uint16_t insertPos = (m_winPos + MAX_STR_LEN - 1) & SLIDING_WINDOW_MASK;
                m_window[insertPos] = data[inputPos];
                if (insertPos < MAX_STR_LEN) {
                    m_window[SLIDING_WINDOW_SIZE + insertPos] = data[inputPos];
                }
                ++inputPos;
                ++m_lookaheadCnt;
            }

            // Add string at new position for future matches
            if (m_replaceCnt > 1 && m_lookaheadCnt > 0) {
                fastAddString(m_winPos);
            }

            if (m_freePos < SLIDING_WINDOW_SIZE) {
                ++m_freePos;
            }

            --m_replaceCnt;
        }
    }

    // Write EOF marker (string of length MAX_STR_LEN + 1)
    strOut(&m_window[0], MAX_STR_LEN + 1, 0);

    // Flush output buffer
    flushOut();

    // Update header with actual lengths
    updateLengths(static_cast<uint32_t>(data.size()),
                  static_cast<uint32_t>(m_nrWritten));

    // Clean up
    m_byteBuf.clear();
    m_window.clear();
    m_stringPtrs.clear();
    m_indexInfo.clear();

    return std::move(m_output);
}

//=============================================================================
// Output Methods
//=============================================================================

void XSACompressor::bitOut(bool bit) {
    if (m_setFlg == 0) {
        // Bitflag byte is full, need to start a new one
        if (m_byteCnt >= OUTPUT_BUF_SIZE - 9) {
            flushOut();
        } else {
            m_byteBuf[m_bitFlgCnt] = m_bitFlg;
        }
        m_bitFlgCnt = m_byteCnt;
        ++m_byteCnt;
        m_bitFlg = 0;
        m_setFlg = 1;
    }

    if (bit) {
        m_bitFlg |= m_setFlg;
    }
    m_setFlg <<= 1;
}

void XSACompressor::byteOut(uint8_t byte) {
    m_byteBuf[m_byteCnt] = byte;
    ++m_byteCnt;
}

void XSACompressor::charOut(uint8_t ch) {
    // 0-bit indicates literal character
    bitOut(false);
    byteOut(ch);
}

void XSACompressor::strOut(const uint8_t* str, uint8_t strLen, uint16_t strPos) {
    if (strLen == 1) {
        charOut(str[0]);
        return;
    }

    // 1-bit indicates string reference
    bitOut(true);

    // Encode string length
    --strLen;  // Length is stored as (actual - 1)

    if (strLen <= 3) {
        // Short lengths: 2=0, 3=10, 4=110
        --strLen;
        while (strLen > 0) {
            bitOut(true);
            --strLen;
        }
        bitOut(false);
    } else {
        // Longer lengths: 111... + variable bits
        bitOut(true);
        bitOut(true);
        bitOut(true);

        uint8_t mskFlg = findMsb(strLen);
        uint8_t temp = mskFlg >> 3;  // Count of leading 1s needed
        while (temp != 0) {
            bitOut(true);
            temp >>= 1;
        }
        if (strLen < 128) {
            bitOut(false);  // Terminating 0 for lengths < 128
        }

        // Output the lower bits of strlen
        mskFlg >>= 1;
        while (mskFlg != 0) {
            bitOut((strLen & mskFlg) != 0);
            mskFlg >>= 1;
        }
    }

    // Don't output position for EOF marker (strlen == MAX_STR_LEN)
    if (strLen != MAX_STR_LEN) {
        // Find distance code bucket
        int count = 0;
        while (strPos >= m_cpDist[count + 1]) {
            ++count;
        }
        ++m_tblSizes[count];

        // Output Huffman code for distance bucket
        uint16_t maskLong = 1 << (m_hufCodeTbl[count].nrBits - 1);
        while (maskLong != 0) {
            bitOut((m_hufCodeTbl[count].bitCode & maskLong) != 0);
            maskLong >>= 1;
        }

        // Output extra bits for precise distance
        strPos -= m_cpDist[count];
        if (m_cpDbMask[count] >= 256) {
            // For large distances, output LSB as a byte
            byteOut(static_cast<uint8_t>(strPos & 0xFF));
            strPos >>= 8;
            int temp = m_cpDbMask[count] >> 8;
            temp >>= 1;
            while (temp != 0) {
                bitOut((strPos & temp) != 0);
                temp >>= 1;
            }
        } else {
            int temp = m_cpDbMask[count] >> 1;
            while (temp != 0) {
                bitOut((strPos & temp) != 0);
                temp >>= 1;
            }
        }
    }

    // Update Huffman table periodically
    if (m_updHufCnt == 0) {
        mkHufTbl();
        convertToHufCodes(&m_hufTbl[2 * TBL_SIZE - 2], 0, 0);
    } else {
        --m_updHufCnt;
    }
}

void XSACompressor::flushOut() {
    m_byteBuf[m_bitFlgCnt] = m_bitFlg;
    m_output.insert(m_output.end(), m_byteBuf.begin(), m_byteBuf.begin() + m_byteCnt);
    m_nrWritten += m_byteCnt;
    m_byteCnt = 0;
}

//=============================================================================
// Header Methods
//=============================================================================

void XSACompressor::writeHeader(const std::string& filename) {
    // Use XSAHeader to write header
    rdedisktool::XSAHeader header;
    header.originalFilename = filename;
    m_nrWritten += header.write(m_output);
}

void XSACompressor::updateLengths(uint32_t orgLen, uint32_t compLen) {
    // Use XSAHeader static method to update lengths
    rdedisktool::XSAHeader::updateLengths(m_output, orgLen, compLen);
}

//=============================================================================
// String Matching (LZ77)
//=============================================================================

void XSACompressor::initIndex() {
    // Reset all index info
    for (auto& info : m_indexInfo) {
        info.firstPtr = 0;
        info.nrPtrs = 0;
    }

    // Initialize with first character
    uint8_t firstChar = m_window[0];
    ++m_indexInfo[firstChar].nrPtrs;
    m_stringPtrs[firstChar][0] = &m_window[0];
}

void XSACompressor::fastAddString(uint16_t winPos) {
    // Add string without searching for match
    const uint8_t* winPosPtr = &m_window[winPos];
    uint8_t ch = *winPosPtr;
    auto& info = m_indexInfo[ch];

    uint16_t idx = (info.firstPtr + info.nrPtrs) & INDEX_PTR_MASK;
    m_stringPtrs[ch][idx] = winPosPtr;
    ++info.nrPtrs;
}

uint16_t XSACompressor::addString(uint16_t winPos, uint16_t& matchPos) {
    const uint8_t* winPosPtr = &m_window[winPos];
    uint8_t ch = *winPosPtr;
    auto& info = m_indexInfo[ch];
    uint16_t firstPtr = info.firstPtr;
    auto& bucket = m_stringPtrs[ch];

    uint16_t matchLength = 0;
    matchPos = 0;

    uint16_t indexCnt = std::min(static_cast<uint16_t>(info.nrPtrs),
                                  static_cast<uint16_t>(NR_INDEX_PTRS));

    // Search through all pointers in this bucket
    while (indexCnt > 0) {
        --indexCnt;

        const uint8_t* candidate = bucket[(firstPtr + indexCnt) & INDEX_PTR_MASK];
        if (candidate == nullptr) continue;

        // Compare strings starting from the first character
        uint16_t thisMatchLen = 0;
        while (thisMatchLen < MAX_STR_LEN &&
               winPosPtr[thisMatchLen] == candidate[thisMatchLen]) {
            ++thisMatchLen;
        }

        if (thisMatchLen > matchLength) {
            matchLength = thisMatchLen;
            // Calculate position of the match in window
            matchPos = static_cast<uint16_t>(candidate - &m_window[0]);

            if (thisMatchLen == MAX_STR_LEN) {
                break;  // Found maximum match
            }
        }
    }

    // Add current string to index
    uint16_t idx = (firstPtr + info.nrPtrs) & INDEX_PTR_MASK;
    bucket[idx] = winPosPtr;
    ++info.nrPtrs;

    return matchLength;
}

void XSACompressor::deleteString(uint16_t strPos) {
    if (strPos >= m_freePos) {
        return;  // String not in window yet
    }

    uint8_t ch = m_window[strPos];
    auto& info = m_indexInfo[ch];
    --info.nrPtrs;
    ++info.firstPtr;
}

//=============================================================================
// Huffman Table
//=============================================================================

void XSACompressor::initHufInfo() {
    // Initialize distance code base values
    int offs = 1;
    for (int i = 0; i < TBL_SIZE; ++i) {
        m_cpDist[i] = offs;
        m_cpDbMask[i] = 1 << cpdExt[i];
        offs += m_cpDbMask[i];
    }
    m_cpDist[TBL_SIZE] = offs;

    // Initialize table sizes and mark leaf nodes
    for (int i = 0; i < TBL_SIZE; ++i) {
        m_tblSizes[i] = 0;
        m_hufTbl[i].child1 = nullptr;
    }

    // Build initial Huffman table
    mkHufTbl();
}

void XSACompressor::mkHufTbl() {
    // Initialize Huffman tree weights
    HufNode* hufPos = &m_hufTbl[0];
    for (int i = 0; i < TBL_SIZE; ++i) {
        m_tblSizes[i] >>= 1;  // Halve frequencies
        (hufPos++)->weight = 1 + m_tblSizes[i];
    }
    for (int i = TBL_SIZE; i < 2 * TBL_SIZE - 1; ++i) {
        (hufPos++)->weight = -1;  // Mark as unused internal node
    }

    // Build Huffman tree by combining nodes with lowest weights
    while (m_hufTbl[2 * TBL_SIZE - 2].weight == -1) {
        // Find first non-zero weight node
        for (hufPos = &m_hufTbl[0]; hufPos->weight == 0; ++hufPos) {
            // Skip nodes with zero weight
        }
        HufNode* l1Pos = hufPos++;

        // Find second non-zero weight node
        while (hufPos->weight == 0) {
            ++hufPos;
        }

        HufNode* l2Pos;
        if (hufPos->weight < l1Pos->weight) {
            l2Pos = l1Pos;
            l1Pos = hufPos++;
        } else {
            l2Pos = hufPos++;
        }

        // Find remaining nodes with lower weights
        int tempW;
        while ((tempW = hufPos->weight) != -1) {
            if (tempW != 0) {
                if (tempW < l1Pos->weight) {
                    l2Pos = l1Pos;
                    l1Pos = hufPos;
                } else if (tempW < l2Pos->weight) {
                    l2Pos = hufPos;
                }
            }
            ++hufPos;
        }

        // Create new internal node
        hufPos->weight = l1Pos->weight + l2Pos->weight;
        hufPos->child1 = l1Pos;
        hufPos->child2 = l2Pos;
        l1Pos->weight = 0;
        l2Pos->weight = 0;
    }

    // Reset update counter
    m_updHufCnt = MAX_HUF_CNT;
}

void XSACompressor::convertToHufCodes(HufNode* node, uint16_t bitCode, int nrBits) {
    if (node->child1 == nullptr) {
        // Leaf node - store the code
        auto nodeIdx = static_cast<size_t>(node - &m_hufTbl[0]);
        m_hufCodeTbl[nodeIdx].nrBits = nrBits;
        m_hufCodeTbl[nodeIdx].bitCode = bitCode;
    } else {
        // Internal node - recurse
        bitCode <<= 1;
        ++nrBits;
        convertToHufCodes(node->child1, bitCode, nrBits);
        convertToHufCodes(node->child2, bitCode | 1, nrBits);
    }
}

//=============================================================================
// Helper Methods
//=============================================================================

uint8_t XSACompressor::findMsb(uint8_t n) {
    uint8_t mskFlg = 128;
    while ((n & mskFlg) == 0 && mskFlg != 0) {
        mskFlg >>= 1;
    }
    return mskFlg;
}

} // namespace rde
