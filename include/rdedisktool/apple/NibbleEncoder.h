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

    // Standard DOS 3.3 track layout, in self-sync groups. Gap 2/3 match a
    // real DOS 3.3 disk captured by Applesauce (6 and 11 syncs); gap 1 for WOZ
    // makes the whole track 50,034 bits (300 rpm at 4 us is ~50,000 bits).
    static constexpr size_t GAP2_SYNCS = 6;
    static constexpr size_t GAP3_SYNCS = 11;
    static constexpr size_t WOZ_GAP1_SYNCS = 85;
    // Nibbles per sector: address field 14 + gap2 + data field 349 + gap3.
    static constexpr size_t SECTOR_NIBBLES = 14 + GAP2_SYNCS + 349 + GAP3_SYNCS;

    /**
     * A track as a nibble stream. isSync[i] marks a self-sync $FF, which a
     * bitstream stores as 10 bits ($FF followed by two zero bits).
     */
    struct TrackNibbles {
        std::vector<uint8_t> nibbles;
        std::vector<uint8_t> isSync;
    };

    /**
     * Sectors decoded from one track, indexed by DOS 3.3 logical sector.
     * Address fields carry the physical sector number; the data of physical
     * sector p is DOS logical sector L where DOS33_INTERLEAVE[L] == p.
     */
    struct ParsedTrack {
        std::array<std::vector<uint8_t>, 16> sectors;   // empty when not found
        std::array<bool, 16> found = {};
        // Address field of the accepted copy misses a fixed 1 bit of the
        // 4-and-4 pattern (DOS 3.3 still reads it; reported as a warning)
        std::array<bool, 16> fixedBitsMissing = {};
        uint8_t volume = 254;
        bool volumeKnown = false;
        uint8_t sectorCount = 16;   // 13 for DOS 3.2 tracks
        bool allFound() const;
    };

    /**
     * Build a standard 16-sector track.
     * @param sectors 16 sector buffers indexed by DOS 3.3 logical sector
     * @param gap1Syncs self-sync count before physical sector 0
     */
    static TrackNibbles buildTrackNibbles(
        const std::array<std::vector<uint8_t>, 16>& sectors,
        uint8_t volume, uint8_t track, size_t gap1Syncs);

    /**
     * Build a NIB/NB2 track of exactly trackSize nibbles (gap 1 fills the rest).
     */
    static std::vector<uint8_t> buildNibTrack(
        const std::array<std::vector<uint8_t>, 16>& sectors,
        uint8_t volume, uint8_t track, size_t trackSize);

    /**
     * Pack a track into a WOZ bitstream (MSB first, self-sync = 10 bits).
     * @param bitCount Output: number of valid bits
     */
    static std::vector<uint8_t> nibblesToWozBits(const TrackNibbles& track,
                                                 uint32_t& bitCount);

    /**
     * Read nibbles from a circular WOZ bitstream the way the Disk II state
     * machine latches them (shift in bits, a nibble is complete when its high
     * bit is set). Covers `revolutions` turns so fields spanning the index
     * point are seen whole. Idealised: no weak bits / MC3470 noise.
     */
    static std::vector<uint8_t> wozBitsToNibbles(const std::vector<uint8_t>& bits,
                                                 uint32_t bitCount,
                                                 int revolutions = 2);

    /**
     * Decode a nibble stream into sectors. The stream should cover the track
     * more than once (fields crossing the end of a circular track). The first
     * valid copy of each physical sector wins. A sector is valid only with a
     * correct address field (checksum, matching track, DE AA epilogue)
     * followed within 32 nibbles by a data field with a correct checksum and
     * DE AA epilogue - the same checks DOS 3.3 RWTS makes.
     */
    static ParsedTrack parseNibbleStream(const std::vector<uint8_t>& nibbles,
                                         uint8_t track);

    //=========================================================================
    // DOS 3.2 (13 sectors per track, 5-and-3 encoding) - read only
    //=========================================================================

    static constexpr uint8_t ADDR_PROLOGUE_3_13 = 0xB5;  // D5 AA B5
    static constexpr size_t NIBBLIZED53_SIZE = 411;     // 410 values + checksum

    /**
     * Decode 411 5-and-3 nibbles to 256 bytes (throws on an invalid nibble
     * or checksum). Bit layout derived from real DOS 3.2 disks.
     */
    static std::vector<uint8_t> decodeSector53(const std::vector<uint8_t>& nibbles);

    /**
     * Decode a nibble stream of a 13-sector track. Sectors are indexed by
     * their physical number (DOS 3.2 numbers sectors physically). Same
     * validity rules as parseNibbleStream; sectorCount = 13.
     */
    static ParsedTrack parseNibbleStream13(const std::vector<uint8_t>& nibbles,
                                           uint8_t track);

    /** True when the stream holds more valid 13-sector than 16-sector address fields. */
    static bool looksLike13Sector(const std::vector<uint8_t>& nibbles, uint8_t track);

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
};

} // namespace rde

#endif // RDEDISKTOOL_APPLE_NIBBLEENCODER_H
