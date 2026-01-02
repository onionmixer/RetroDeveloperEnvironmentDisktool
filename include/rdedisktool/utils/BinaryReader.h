/**
 * @file BinaryReader.h
 * @brief Binary data reading and writing utilities
 *
 * Provides convenient methods for reading/writing multi-byte values
 * with proper endianness handling. Replaces repeated byte-shifting
 * patterns throughout the codebase.
 */

#ifndef RDEDISKTOOL_BINARY_READER_H
#define RDEDISKTOOL_BINARY_READER_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace rdedisktool {

/**
 * @class BinaryReader
 * @brief Utility class for reading binary data with endianness support
 *
 * All read positions are relative to the base offset specified in constructor.
 * Provides bounds checking to prevent buffer overruns.
 */
class BinaryReader {
public:
    /**
     * @brief Construct from vector with optional base offset
     * @param data Source data buffer
     * @param baseOffset Starting offset for all read operations
     */
    explicit BinaryReader(const std::vector<uint8_t>& data, size_t baseOffset = 0);

    /**
     * @brief Construct from raw pointer
     * @param data Pointer to data buffer
     * @param size Size of data buffer
     * @param baseOffset Starting offset for all read operations
     */
    BinaryReader(const uint8_t* data, size_t size, size_t baseOffset = 0);

    // Little-endian readers
    uint8_t readU8(size_t pos) const;
    uint16_t readU16LE(size_t pos) const;
    uint32_t readU24LE(size_t pos) const;
    uint32_t readU32LE(size_t pos) const;

    // Signed little-endian readers
    int8_t readS8(size_t pos) const;
    int16_t readS16LE(size_t pos) const;
    int32_t readS32LE(size_t pos) const;

    // Big-endian readers
    uint16_t readU16BE(size_t pos) const;
    uint32_t readU32BE(size_t pos) const;

    /**
     * @brief Read fixed-length string
     * @param pos Position relative to base offset
     * @param maxLen Maximum length to read
     * @return String (may contain embedded nulls)
     */
    std::string readString(size_t pos, size_t maxLen) const;

    /**
     * @brief Read null-terminated string
     * @param pos Position relative to base offset
     * @param maxLen Maximum length to scan
     * @return String up to null terminator
     */
    std::string readNullTerminated(size_t pos, size_t maxLen) const;

    /**
     * @brief Read string with trailing spaces trimmed
     * @param pos Position relative to base offset
     * @param maxLen Maximum length to read
     * @return String with trailing spaces removed
     */
    std::string readTrimmedString(size_t pos, size_t maxLen) const;

    /**
     * @brief Read string with high-bit stripped (Apple II format)
     * @param pos Position relative to base offset
     * @param maxLen Maximum length to read
     * @return String with 0x80 bit cleared from each character
     */
    std::string readAppleString(size_t pos, size_t maxLen) const;

    /**
     * @brief Read raw bytes to destination buffer
     * @param pos Position relative to base offset
     * @param dest Destination buffer
     * @param len Number of bytes to read
     */
    void readBytes(size_t pos, void* dest, size_t len) const;

    /**
     * @brief Read raw bytes as vector
     * @param pos Position relative to base offset
     * @param len Number of bytes to read
     * @return Vector containing the bytes
     */
    std::vector<uint8_t> readBytes(size_t pos, size_t len) const;

    /**
     * @brief Check if range is valid for reading
     * @param pos Position relative to base offset
     * @param len Number of bytes to check
     * @return true if range is within bounds
     */
    bool isValidRange(size_t pos, size_t len) const;

    /**
     * @brief Get total available size from base offset
     * @return Available bytes
     */
    size_t size() const { return m_size - m_baseOffset; }

    /**
     * @brief Get raw data pointer at base offset
     * @return Pointer to data at base offset
     */
    const uint8_t* data() const { return m_data + m_baseOffset; }

private:
    const uint8_t* m_data;
    size_t m_size;
    size_t m_baseOffset;
};

/**
 * @class BinaryWriter
 * @brief Utility class for writing binary data with endianness support
 *
 * All write positions are relative to the base offset specified in constructor.
 * Provides bounds checking to prevent buffer overruns.
 */
class BinaryWriter {
public:
    /**
     * @brief Construct from vector with optional base offset
     * @param data Target data buffer (must remain valid)
     * @param baseOffset Starting offset for all write operations
     */
    explicit BinaryWriter(std::vector<uint8_t>& data, size_t baseOffset = 0);

    /**
     * @brief Construct from raw pointer
     * @param data Pointer to data buffer
     * @param size Size of data buffer
     * @param baseOffset Starting offset for all write operations
     */
    BinaryWriter(uint8_t* data, size_t size, size_t baseOffset = 0);

    // Little-endian writers
    void writeU8(size_t pos, uint8_t value);
    void writeU16LE(size_t pos, uint16_t value);
    void writeU24LE(size_t pos, uint32_t value);
    void writeU32LE(size_t pos, uint32_t value);

    // Signed little-endian writers
    void writeS8(size_t pos, int8_t value);
    void writeS16LE(size_t pos, int16_t value);
    void writeS32LE(size_t pos, int32_t value);

    // Big-endian writers
    void writeU16BE(size_t pos, uint16_t value);
    void writeU32BE(size_t pos, uint32_t value);

    /**
     * @brief Write string with padding
     * @param pos Position relative to base offset
     * @param str String to write
     * @param fieldLen Total field length
     * @param padChar Padding character (default: 0)
     */
    void writeString(size_t pos, const std::string& str, size_t fieldLen, char padChar = 0);

    /**
     * @brief Write string with space padding (common for filenames)
     * @param pos Position relative to base offset
     * @param str String to write
     * @param fieldLen Total field length
     */
    void writePaddedString(size_t pos, const std::string& str, size_t fieldLen);

    /**
     * @brief Write string with high-bit set (Apple II format)
     * @param pos Position relative to base offset
     * @param str String to write
     * @param fieldLen Total field length
     */
    void writeAppleString(size_t pos, const std::string& str, size_t fieldLen);

    /**
     * @brief Write raw bytes from buffer
     * @param pos Position relative to base offset
     * @param src Source buffer
     * @param len Number of bytes to write
     */
    void writeBytes(size_t pos, const void* src, size_t len);

    /**
     * @brief Fill region with byte value
     * @param pos Position relative to base offset
     * @param len Number of bytes to fill
     * @param value Fill value
     */
    void fill(size_t pos, size_t len, uint8_t value);

    /**
     * @brief Check if range is valid for writing
     * @param pos Position relative to base offset
     * @param len Number of bytes to check
     * @return true if range is within bounds
     */
    bool isValidRange(size_t pos, size_t len) const;

    /**
     * @brief Get total available size from base offset
     * @return Available bytes
     */
    size_t size() const { return m_size - m_baseOffset; }

    /**
     * @brief Get raw data pointer at base offset
     * @return Pointer to data at base offset
     */
    uint8_t* data() { return m_data + m_baseOffset; }

private:
    uint8_t* m_data;
    size_t m_size;
    size_t m_baseOffset;
};

} // namespace rdedisktool

#endif // RDEDISKTOOL_BINARY_READER_H
