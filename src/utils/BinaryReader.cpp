/**
 * @file BinaryReader.cpp
 * @brief Implementation of BinaryReader and BinaryWriter classes
 */

#include "rdedisktool/utils/BinaryReader.h"
#include <algorithm>
#include <cctype>

namespace rdedisktool {

// ============================================================================
// BinaryReader Implementation
// ============================================================================

BinaryReader::BinaryReader(const std::vector<uint8_t>& data, size_t baseOffset)
    : m_data(data.data())
    , m_size(data.size())
    , m_baseOffset(baseOffset)
{
}

BinaryReader::BinaryReader(const uint8_t* data, size_t size, size_t baseOffset)
    : m_data(data)
    , m_size(size)
    , m_baseOffset(baseOffset)
{
}

bool BinaryReader::isValidRange(size_t pos, size_t len) const {
    size_t absPos = m_baseOffset + pos;
    return absPos + len <= m_size;
}

uint8_t BinaryReader::readU8(size_t pos) const {
    if (!isValidRange(pos, 1)) return 0;
    return m_data[m_baseOffset + pos];
}

uint16_t BinaryReader::readU16LE(size_t pos) const {
    if (!isValidRange(pos, 2)) return 0;
    size_t absPos = m_baseOffset + pos;
    return static_cast<uint16_t>(m_data[absPos]) |
           (static_cast<uint16_t>(m_data[absPos + 1]) << 8);
}

uint32_t BinaryReader::readU24LE(size_t pos) const {
    if (!isValidRange(pos, 3)) return 0;
    size_t absPos = m_baseOffset + pos;
    return static_cast<uint32_t>(m_data[absPos]) |
           (static_cast<uint32_t>(m_data[absPos + 1]) << 8) |
           (static_cast<uint32_t>(m_data[absPos + 2]) << 16);
}

uint32_t BinaryReader::readU32LE(size_t pos) const {
    if (!isValidRange(pos, 4)) return 0;
    size_t absPos = m_baseOffset + pos;
    return static_cast<uint32_t>(m_data[absPos]) |
           (static_cast<uint32_t>(m_data[absPos + 1]) << 8) |
           (static_cast<uint32_t>(m_data[absPos + 2]) << 16) |
           (static_cast<uint32_t>(m_data[absPos + 3]) << 24);
}

int8_t BinaryReader::readS8(size_t pos) const {
    return static_cast<int8_t>(readU8(pos));
}

int16_t BinaryReader::readS16LE(size_t pos) const {
    return static_cast<int16_t>(readU16LE(pos));
}

int32_t BinaryReader::readS32LE(size_t pos) const {
    return static_cast<int32_t>(readU32LE(pos));
}

uint16_t BinaryReader::readU16BE(size_t pos) const {
    if (!isValidRange(pos, 2)) return 0;
    size_t absPos = m_baseOffset + pos;
    return (static_cast<uint16_t>(m_data[absPos]) << 8) |
           static_cast<uint16_t>(m_data[absPos + 1]);
}

uint32_t BinaryReader::readU32BE(size_t pos) const {
    if (!isValidRange(pos, 4)) return 0;
    size_t absPos = m_baseOffset + pos;
    return (static_cast<uint32_t>(m_data[absPos]) << 24) |
           (static_cast<uint32_t>(m_data[absPos + 1]) << 16) |
           (static_cast<uint32_t>(m_data[absPos + 2]) << 8) |
           static_cast<uint32_t>(m_data[absPos + 3]);
}

std::string BinaryReader::readString(size_t pos, size_t maxLen) const {
    if (!isValidRange(pos, maxLen)) {
        maxLen = (m_baseOffset + pos < m_size) ? (m_size - m_baseOffset - pos) : 0;
    }
    if (maxLen == 0) return "";

    size_t absPos = m_baseOffset + pos;
    return std::string(reinterpret_cast<const char*>(m_data + absPos), maxLen);
}

std::string BinaryReader::readNullTerminated(size_t pos, size_t maxLen) const {
    if (!isValidRange(pos, 1)) return "";

    size_t absPos = m_baseOffset + pos;
    size_t available = std::min(maxLen, m_size - absPos);

    size_t len = 0;
    while (len < available && m_data[absPos + len] != 0) {
        ++len;
    }

    return std::string(reinterpret_cast<const char*>(m_data + absPos), len);
}

std::string BinaryReader::readTrimmedString(size_t pos, size_t maxLen) const {
    std::string str = readString(pos, maxLen);

    // Trim trailing spaces and nulls
    size_t end = str.length();
    while (end > 0 && (str[end - 1] == ' ' || str[end - 1] == '\0')) {
        --end;
    }

    return str.substr(0, end);
}

std::string BinaryReader::readAppleString(size_t pos, size_t maxLen) const {
    std::string str = readString(pos, maxLen);

    // Strip high bit from each character and trim trailing spaces
    size_t end = str.length();
    while (end > 0) {
        char c = str[end - 1] & 0x7F;
        if (c != ' ' && c != '\0') break;
        --end;
    }

    std::string result;
    result.reserve(end);
    for (size_t i = 0; i < end; ++i) {
        result += static_cast<char>(str[i] & 0x7F);
    }

    return result;
}

void BinaryReader::readBytes(size_t pos, void* dest, size_t len) const {
    if (!isValidRange(pos, len) || dest == nullptr) {
        if (dest) std::memset(dest, 0, len);
        return;
    }
    std::memcpy(dest, m_data + m_baseOffset + pos, len);
}

std::vector<uint8_t> BinaryReader::readBytes(size_t pos, size_t len) const {
    if (!isValidRange(pos, len)) {
        len = (m_baseOffset + pos < m_size) ? (m_size - m_baseOffset - pos) : 0;
    }
    if (len == 0) return {};

    size_t absPos = m_baseOffset + pos;
    return std::vector<uint8_t>(m_data + absPos, m_data + absPos + len);
}

// ============================================================================
// BinaryWriter Implementation
// ============================================================================

BinaryWriter::BinaryWriter(std::vector<uint8_t>& data, size_t baseOffset)
    : m_data(data.data())
    , m_size(data.size())
    , m_baseOffset(baseOffset)
{
}

BinaryWriter::BinaryWriter(uint8_t* data, size_t size, size_t baseOffset)
    : m_data(data)
    , m_size(size)
    , m_baseOffset(baseOffset)
{
}

bool BinaryWriter::isValidRange(size_t pos, size_t len) const {
    size_t absPos = m_baseOffset + pos;
    return absPos + len <= m_size;
}

void BinaryWriter::writeU8(size_t pos, uint8_t value) {
    if (!isValidRange(pos, 1)) return;
    m_data[m_baseOffset + pos] = value;
}

void BinaryWriter::writeU16LE(size_t pos, uint16_t value) {
    if (!isValidRange(pos, 2)) return;
    size_t absPos = m_baseOffset + pos;
    m_data[absPos] = static_cast<uint8_t>(value & 0xFF);
    m_data[absPos + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void BinaryWriter::writeU24LE(size_t pos, uint32_t value) {
    if (!isValidRange(pos, 3)) return;
    size_t absPos = m_baseOffset + pos;
    m_data[absPos] = static_cast<uint8_t>(value & 0xFF);
    m_data[absPos + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    m_data[absPos + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
}

void BinaryWriter::writeU32LE(size_t pos, uint32_t value) {
    if (!isValidRange(pos, 4)) return;
    size_t absPos = m_baseOffset + pos;
    m_data[absPos] = static_cast<uint8_t>(value & 0xFF);
    m_data[absPos + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    m_data[absPos + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    m_data[absPos + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

void BinaryWriter::writeS8(size_t pos, int8_t value) {
    writeU8(pos, static_cast<uint8_t>(value));
}

void BinaryWriter::writeS16LE(size_t pos, int16_t value) {
    writeU16LE(pos, static_cast<uint16_t>(value));
}

void BinaryWriter::writeS32LE(size_t pos, int32_t value) {
    writeU32LE(pos, static_cast<uint32_t>(value));
}

void BinaryWriter::writeU16BE(size_t pos, uint16_t value) {
    if (!isValidRange(pos, 2)) return;
    size_t absPos = m_baseOffset + pos;
    m_data[absPos] = static_cast<uint8_t>((value >> 8) & 0xFF);
    m_data[absPos + 1] = static_cast<uint8_t>(value & 0xFF);
}

void BinaryWriter::writeU32BE(size_t pos, uint32_t value) {
    if (!isValidRange(pos, 4)) return;
    size_t absPos = m_baseOffset + pos;
    m_data[absPos] = static_cast<uint8_t>((value >> 24) & 0xFF);
    m_data[absPos + 1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    m_data[absPos + 2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    m_data[absPos + 3] = static_cast<uint8_t>(value & 0xFF);
}

void BinaryWriter::writeString(size_t pos, const std::string& str, size_t fieldLen, char padChar) {
    if (!isValidRange(pos, fieldLen)) return;

    size_t absPos = m_baseOffset + pos;
    size_t copyLen = std::min(str.length(), fieldLen);

    // Copy string
    std::memcpy(m_data + absPos, str.data(), copyLen);

    // Pad remainder
    if (copyLen < fieldLen) {
        std::memset(m_data + absPos + copyLen, padChar, fieldLen - copyLen);
    }
}

void BinaryWriter::writePaddedString(size_t pos, const std::string& str, size_t fieldLen) {
    writeString(pos, str, fieldLen, ' ');
}

void BinaryWriter::writeAppleString(size_t pos, const std::string& str, size_t fieldLen) {
    if (!isValidRange(pos, fieldLen)) return;

    size_t absPos = m_baseOffset + pos;
    size_t copyLen = std::min(str.length(), fieldLen);

    // Copy string with high bit set and uppercase conversion
    for (size_t i = 0; i < copyLen; ++i) {
        char c = static_cast<char>(std::toupper(static_cast<unsigned char>(str[i])));
        m_data[absPos + i] = static_cast<uint8_t>(c) | 0x80;
    }

    // Pad remainder with high-bit-set spaces
    for (size_t i = copyLen; i < fieldLen; ++i) {
        m_data[absPos + i] = ' ' | 0x80;
    }
}

void BinaryWriter::writeBytes(size_t pos, const void* src, size_t len) {
    if (!isValidRange(pos, len) || src == nullptr) return;
    std::memcpy(m_data + m_baseOffset + pos, src, len);
}

void BinaryWriter::fill(size_t pos, size_t len, uint8_t value) {
    if (!isValidRange(pos, len)) return;
    std::memset(m_data + m_baseOffset + pos, value, len);
}

} // namespace rdedisktool
