#include "rdedisktool/CRC.h"
#include <vector>
#include <cstdint>

namespace rde {

/**
 * MFM (Modified Frequency Modulation) Encoder/Decoder for MSX
 *
 * MFM encoding is used by MSX floppy drives for double-density recording.
 * This is a simplified implementation that handles the basic encoding needs.
 */
class MFMEncoder {
public:
    // MFM constants
    static constexpr uint8_t SYNC_BYTE = 0x00;
    static constexpr uint8_t GAP_BYTE = 0x4E;
    static constexpr uint8_t INDEX_MARK = 0xFC;
    static constexpr uint8_t ID_MARK = 0xFE;
    static constexpr uint8_t DATA_MARK = 0xFB;
    static constexpr uint8_t DELETED_DATA_MARK = 0xF8;
    static constexpr uint8_t SYNC_A1 = 0xA1;  // Special sync with missing clock

    /**
     * Build a formatted track with sectors
     */
    static std::vector<uint8_t> buildTrack(
        size_t track, size_t side, size_t sectorsPerTrack,
        const std::vector<std::vector<uint8_t>>& sectorData,
        size_t bytesPerSector = 512) {

        std::vector<uint8_t> result;
        result.reserve(6250);  // Standard DD track length

        // Gap 4a (80 bytes of 0x4E)
        result.insert(result.end(), 80, GAP_BYTE);

        // Sync (12 bytes of 0x00)
        result.insert(result.end(), 12, SYNC_BYTE);

        // Index mark
        result.insert(result.end(), 3, SYNC_A1);
        result.push_back(INDEX_MARK);

        // Gap 1 (50 bytes of 0x4E)
        result.insert(result.end(), 50, GAP_BYTE);

        // Write sectors
        for (size_t sect = 0; sect < sectorsPerTrack; ++sect) {
            // Sync (12 bytes of 0x00)
            result.insert(result.end(), 12, SYNC_BYTE);

            // ID field sync
            result.insert(result.end(), 3, SYNC_A1);
            result.push_back(ID_MARK);

            // ID field: track, side, sector (1-based), size code
            result.push_back(static_cast<uint8_t>(track));
            result.push_back(static_cast<uint8_t>(side));
            result.push_back(static_cast<uint8_t>(sect + 1));

            // Size code: 0=128, 1=256, 2=512, 3=1024
            uint8_t sizeCode = 0;
            if (bytesPerSector >= 1024) sizeCode = 3;
            else if (bytesPerSector >= 512) sizeCode = 2;
            else if (bytesPerSector >= 256) sizeCode = 1;
            result.push_back(sizeCode);

            // ID CRC
            size_t crcStart = result.size() - 8;
            uint16_t idCrc = CRC::crc16_ccitt(&result[crcStart], 8);
            result.push_back((idCrc >> 8) & 0xFF);
            result.push_back(idCrc & 0xFF);

            // Gap 2 (22 bytes of 0x4E)
            result.insert(result.end(), 22, GAP_BYTE);

            // Data field sync (12 bytes of 0x00)
            result.insert(result.end(), 12, SYNC_BYTE);

            // Data sync
            result.insert(result.end(), 3, SYNC_A1);
            result.push_back(DATA_MARK);

            // Sector data
            size_t dataStart = result.size();
            if (sect < sectorData.size() && !sectorData[sect].empty()) {
                const auto& data = sectorData[sect];
                size_t copySize = std::min(data.size(), bytesPerSector);
                result.insert(result.end(), data.begin(), data.begin() + copySize);
                if (copySize < bytesPerSector) {
                    result.insert(result.end(), bytesPerSector - copySize, 0);
                }
            } else {
                result.insert(result.end(), bytesPerSector, 0xE5);
            }

            // Data CRC
            uint16_t dataCrc = CRC::crc16_ccitt(&result[dataStart - 4],
                                                 bytesPerSector + 4);
            result.push_back((dataCrc >> 8) & 0xFF);
            result.push_back(dataCrc & 0xFF);

            // Gap 3 (54 bytes of 0x4E for last sector, less for others)
            result.insert(result.end(), 54, GAP_BYTE);
        }

        // Gap 4b (fill rest with 0x4E to standard track length)
        while (result.size() < 6250) {
            result.push_back(GAP_BYTE);
        }

        return result;
    }

    /**
     * Calculate CRC-16 CCITT for a data block
     */
    static uint16_t calculateCRC(const uint8_t* data, size_t length) {
        return CRC::crc16_ccitt(data, length);
    }
};

} // namespace rde
