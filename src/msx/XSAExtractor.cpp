#include "rdedisktool/msx/XSAExtractor.h"

namespace rde {

XSAExtractor::XSAExtractor(const std::vector<uint8_t>& data)
    : m_inputPtr(data.data())
    , m_inputEnd(data.data() + data.size())
    , m_updHufCnt(0)
    , m_bitFlg(0)
    , m_bitCnt(0)
{
    // Verify magic number
    if (!isXSAFormat(data)) {
        throw XSAException("Not an XSA image");
    }

    // Skip magic number
    m_inputPtr += 4;

    // Process header and decompress
    checkHeader();
    initHufInfo();
    unLz77();
}

std::vector<uint8_t> XSAExtractor::extract() {
    return std::move(m_output);
}

bool XSAExtractor::isXSAFormat(const std::vector<uint8_t>& data) {
    if (data.size() < 4) {
        return false;
    }
    return data[0] == MAGIC[0] &&
           data[1] == MAGIC[1] &&
           data[2] == MAGIC[2] &&
           data[3] == MAGIC[3];
}

uint8_t XSAExtractor::charIn() {
    if (m_inputPtr >= m_inputEnd) {
        throw XSAException("Corrupt XSA image: unexpected end of file");
    }
    return *m_inputPtr++;
}

bool XSAExtractor::bitIn() {
    if (m_bitCnt == 0) {
        m_bitFlg = charIn();
        m_bitCnt = 8;
    }
    bool result = m_bitFlg & 1;
    --m_bitCnt;
    m_bitFlg >>= 1;
    return result;
}

void XSAExtractor::checkHeader() {
    // Read original length (little-endian)
    unsigned outBufLen = 0;
    for (int i = 0; i < 4; ++i) {
        outBufLen |= static_cast<unsigned>(charIn()) << (8 * i);
    }

    // Allocate output buffer
    m_output.resize(outBufLen);

    // Skip compressed length (4 bytes)
    for (int i = 0; i < 4; ++i) {
        charIn();
    }

    // Skip original filename (null-terminated)
    while (charIn() != 0) {
        // Skip until null terminator
    }
}

void XSAExtractor::unLz77() {
    m_bitCnt = 0;  // No bits read yet

    size_t outIdx = 0;
    size_t remaining = m_output.size();

    while (true) {
        if (bitIn()) {
            // 1-bit: LZ77 back-reference
            unsigned strLen = rdStrLen();
            if (strLen == MAX_STR_LEN + 1) {
                // End of compressed data
                return;
            }

            unsigned strPos = static_cast<unsigned>(rdStrPos());

            if (strPos == 0 || strPos > outIdx) {
                throw XSAException("Corrupt XSA image: invalid offset");
            }
            if (remaining < strLen) {
                throw XSAException("Invalid XSA image: too small output buffer");
            }

            remaining -= strLen;
            while (strLen--) {
                m_output[outIdx] = m_output[outIdx - strPos];
                ++outIdx;
            }
        } else {
            // 0-bit: Literal byte
            if (remaining == 0) {
                throw XSAException("Invalid XSA image: too small output buffer");
            }
            --remaining;
            m_output[outIdx++] = charIn();
        }
    }
}

unsigned XSAExtractor::rdStrLen() {
    // Length encoding:
    // 0       -> 2
    // 10      -> 3
    // 110     -> 4
    // 1110    -> variable (5-254)
    // 1111110 -> 255 (end marker)

    if (!bitIn()) return 2;
    if (!bitIn()) return 3;
    if (!bitIn()) return 4;

    uint8_t nrBits = 2;
    while (nrBits != 7 && bitIn()) {
        ++nrBits;
    }

    unsigned len = 1;
    while (nrBits--) {
        len = (len << 1) | (bitIn() ? 1 : 0);
    }
    return len + 1;
}

int XSAExtractor::rdStrPos() {
    // Traverse Huffman tree to get distance code index
    HufNode* hufPos = &m_hufTbl[2 * TBL_SIZE - 2];

    while (hufPos->child1) {
        if (bitIn()) {
            hufPos = hufPos->child2;
        } else {
            hufPos = hufPos->child1;
        }
    }

    auto cpdIndex = static_cast<uint8_t>(hufPos - &m_hufTbl[0]);
    ++m_tblSizes[cpdIndex];

    // Read extra bits for distance
    auto getNBits = [&](unsigned n) -> uint8_t {
        uint8_t result = 0;
        for (unsigned i = 0; i < n; ++i) {
            result = static_cast<uint8_t>((result << 1) | (bitIn() ? 1 : 0));
        }
        return result;
    };

    int strPos;
    if (cpdExt[cpdIndex] >= 8) {
        uint8_t strPosLsb = charIn();
        uint8_t strPosMsb = getNBits(cpdExt[cpdIndex] - 8);
        strPos = strPosLsb + 256 * strPosMsb;
    } else {
        strPos = getNBits(cpdExt[cpdIndex]);
    }

    // Update Huffman table periodically
    if ((m_updHufCnt--) == 0) {
        mkHufTbl();
    }

    return strPos + m_cpDist[cpdIndex];
}

void XSAExtractor::initHufInfo() {
    // Initialize distance code base values
    int offs = 1;
    for (int i = 0; i < TBL_SIZE; ++i) {
        m_cpDist[i] = offs;
        offs += 1 << cpdExt[i];
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

void XSAExtractor::mkHufTbl() {
    // Initialize Huffman tree weights
    HufNode* hufPos = &m_hufTbl[0];
    for (int i = 0; i < TBL_SIZE; ++i) {
        (hufPos++)->weight = 1 + (m_tblSizes[i] >>= 1);
    }
    for (int i = TBL_SIZE; i < 2 * TBL_SIZE - 1; ++i) {
        (hufPos++)->weight = -1;  // Mark as unused internal node
    }

    // Build Huffman tree by combining nodes with lowest weights
    while (m_hufTbl[2 * TBL_SIZE - 2].weight == -1) {
        // Find first non-zero weight node
        for (hufPos = &m_hufTbl[0]; !(hufPos->weight); ++hufPos) {
            // Skip nodes with zero weight
        }
        HufNode* l1Pos = hufPos++;

        // Find second non-zero weight node
        while (!(hufPos->weight)) {
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
            if (tempW) {
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
        (hufPos->child1 = l1Pos)->weight = 0;
        (hufPos->child2 = l2Pos)->weight = 0;
    }

    // Reset update counter
    m_updHufCnt = MAX_HUF_CNT;
}

} // namespace rde
