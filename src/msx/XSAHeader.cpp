#include "rdedisktool/msx/XSAHeader.h"
#include "rdedisktool/utils/BinaryReader.h"

namespace rdedisktool {

// Static constexpr definition
constexpr uint8_t XSAHeader::MAGIC[4];

bool XSAHeader::isXSAFormat(const uint8_t* data, size_t size) {
    if (size < 4) return false;
    return data[0] == MAGIC[0] && data[1] == MAGIC[1] &&
           data[2] == MAGIC[2] && data[3] == MAGIC[3];
}

bool XSAHeader::isXSAFormat(const std::vector<uint8_t>& data) {
    return isXSAFormat(data.data(), data.size());
}

size_t XSAHeader::read(const uint8_t* data, size_t size) {
    if (size < HEADER_MIN_SIZE || !isXSAFormat(data, size)) {
        return 0;
    }

    // Use BinaryReader for endian-safe reading
    BinaryReader reader(data, size);

    // Read lengths (little-endian, offsets 4 and 8)
    originalLength = reader.readU32LE(4);
    compressedLength = reader.readU32LE(8);

    // Read null-terminated filename starting at offset 12
    originalFilename.clear();
    size_t pos = 12;
    while (pos < size && data[pos] != 0) {
        originalFilename += static_cast<char>(data[pos]);
        ++pos;
    }

    // Return header size including null terminator
    // If we didn't find null terminator, return 0 (error)
    return (pos < size) ? pos + 1 : 0;
}

size_t XSAHeader::read(const std::vector<uint8_t>& data) {
    return read(data.data(), data.size());
}

size_t XSAHeader::write(std::vector<uint8_t>& output) const {
    size_t startSize = output.size();

    // Write magic bytes
    for (uint8_t b : MAGIC) {
        output.push_back(b);
    }

    // Write original length placeholder (will be updated later)
    for (int i = 0; i < 4; ++i) {
        output.push_back(0);
    }

    // Write compressed length placeholder (will be updated later)
    for (int i = 0; i < 4; ++i) {
        output.push_back(0);
    }

    // Write filename + null terminator
    for (char c : originalFilename) {
        output.push_back(static_cast<uint8_t>(c));
    }
    output.push_back(0);

    return output.size() - startSize;
}

void XSAHeader::updateLengths(std::vector<uint8_t>& output,
                               uint32_t orgLen, uint32_t compLen) {
    if (output.size() < 12) return;

    // Use BinaryWriter for endian-safe writing
    BinaryWriter writer(output);
    writer.writeU32LE(4, orgLen);
    writer.writeU32LE(8, compLen);
}

size_t XSAHeader::headerSize() const {
    // magic(4) + originalLength(4) + compressedLength(4) + filename + null(1)
    return 4 + 4 + 4 + originalFilename.length() + 1;
}

void XSAHeader::clear() {
    originalLength = 0;
    compressedLength = 0;
    originalFilename.clear();
}

} // namespace rdedisktool
