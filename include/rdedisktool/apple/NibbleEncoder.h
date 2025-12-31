#ifndef RDEDISKTOOL_APPLE_NIBBLEENCODER_H
#define RDEDISKTOOL_APPLE_NIBBLEENCODER_H

#include <cstdint>
#include <cstddef>
#include <vector>
#include <array>

namespace rde {

/**
 * Apple II 6-and-2 GCR Nibble Encoder/Decoder
 *
 * The Apple II Disk II uses 6-and-2 Group Code Recording (GCR) encoding
 * to store 256 bytes of data in 343 bytes of disk space.
 *
 * The encoding process:
 * 1. 256 data bytes → 342 bytes (6-and-2 pre-nibblizing)
 * 2. 342 bytes → 343 nibbles (XOR checksumming)
 * 3. 343 nibbles → 343 disk bytes (GCR translation)
 */
class NibbleEncoder {
public:
    // Nibble format constants
    static constexpr size_t SECTOR_DATA_SIZE = 256;
    static constexpr size_t NIBBLIZED_SIZE = 343;
    static constexpr size_t TRACK_NIBBLE_SIZE = 6656;   // Standard NIB track
    static constexpr size_t TRACK_NIBBLE_SIZE_NB2 = 6384;  // NB2 track

    // Sync bytes and markers
    static constexpr uint8_t SYNC_BYTE = 0xFF;
    static constexpr uint8_t D5 = 0xD5;
    static constexpr uint8_t AA = 0xAA;
    static constexpr uint8_t AD = 0xAD;
    static constexpr uint8_t DE = 0xDE;
    static constexpr uint8_t EB = 0xEB;

    // Address field prologue: D5 AA 96
    static constexpr uint8_t ADDR_PROLOGUE_1 = 0xD5;
    static constexpr uint8_t ADDR_PROLOGUE_2 = 0xAA;
    static constexpr uint8_t ADDR_PROLOGUE_3 = 0x96;

    // Data field prologue: D5 AA AD
    static constexpr uint8_t DATA_PROLOGUE_1 = 0xD5;
    static constexpr uint8_t DATA_PROLOGUE_2 = 0xAA;
    static constexpr uint8_t DATA_PROLOGUE_3 = 0xAD;

    // Epilogue: DE AA EB
    static constexpr uint8_t EPILOGUE_1 = 0xDE;
    static constexpr uint8_t EPILOGUE_2 = 0xAA;
    static constexpr uint8_t EPILOGUE_3 = 0xEB;

    /**
     * Encode a 256-byte sector to 343 nibblized bytes
     * @param data 256 bytes of sector data
     * @return 343 bytes of nibblized data
     */
    static std::vector<uint8_t> encodeSector(const std::vector<uint8_t>& data);

    /**
     * Decode 343 nibblized bytes to 256 bytes sector data
     * @param nibbles 343 bytes of nibblized data
     * @return 256 bytes of decoded data
     */
    static std::vector<uint8_t> decodeSector(const std::vector<uint8_t>& nibbles);

    /**
     * Encode an address field (volume, track, sector)
     * @param volume Volume number (usually 254)
     * @param track Track number (0-34)
     * @param sector Sector number (0-15)
     * @return Encoded address field (14 bytes including prologue/epilogue)
     */
    static std::vector<uint8_t> encodeAddressField(uint8_t volume, uint8_t track, uint8_t sector);

    /**
     * Decode an address field
     * @param data Address field bytes (minimum 8 bytes for data)
     * @param volume Output: volume number
     * @param track Output: track number
     * @param sector Output: sector number
     * @return true if valid, false otherwise
     */
    static bool decodeAddressField(const uint8_t* data, uint8_t& volume,
                                   uint8_t& track, uint8_t& sector);

    /**
     * Build a complete nibblized track
     * @param sectorData Array of 16 sector buffers (256 bytes each)
     * @param volume Volume number
     * @param track Track number
     * @return Complete nibblized track
     */
    static std::vector<uint8_t> buildTrack(
        const std::array<std::vector<uint8_t>, 16>& sectorData,
        uint8_t volume, uint8_t track);

    /**
     * Parse a nibblized track into sectors
     * @param trackData Nibblized track data
     * @param track Expected track number for verification
     * @return Array of 16 decoded sectors (256 bytes each)
     */
    static std::array<std::vector<uint8_t>, 16> parseTrack(
        const std::vector<uint8_t>& trackData, uint8_t track);

    /**
     * Get the 6-and-2 GCR encoding table
     */
    static const std::array<uint8_t, 64>& getEncodeTable();

    /**
     * Get the 6-and-2 GCR decoding table
     */
    static const std::array<uint8_t, 256>& getDecodeTable();

    /**
     * Encode 4-and-4 (used for address field)
     */
    static void encode44(uint8_t value, uint8_t& odd, uint8_t& even);

    /**
     * Decode 4-and-4
     */
    static uint8_t decode44(uint8_t odd, uint8_t even);

    /**
     * Find the next address field in nibble data
     * @param data Nibble data
     * @param startPos Starting position
     * @return Position of address prologue, or -1 if not found
     */
    static int findAddressField(const std::vector<uint8_t>& data, size_t startPos = 0);

    /**
     * Find the next data field in nibble data
     * @param data Nibble data
     * @param startPos Starting position
     * @return Position of data prologue, or -1 if not found
     */
    static int findDataField(const std::vector<uint8_t>& data, size_t startPos = 0);

private:
    // 6-and-2 encoding table (6-bit value → disk byte)
    static const std::array<uint8_t, 64> ENCODE_TABLE;

    // 6-and-2 decoding table (disk byte → 6-bit value, 0xFF = invalid)
    static const std::array<uint8_t, 256> DECODE_TABLE;

    // Physical to DOS 3.3 sector mapping for nibble track layout
    static const std::array<uint8_t, 16> PHYSICAL_SECTOR_ORDER;
};

} // namespace rde

#endif // RDEDISKTOOL_APPLE_NIBBLEENCODER_H
