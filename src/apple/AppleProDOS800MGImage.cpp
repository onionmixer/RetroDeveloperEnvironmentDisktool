#include "rdedisktool/apple/AppleProDOS800MGImage.h"
#include "rdedisktool/DiskImageFactory.h"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace rde {

namespace {
    struct AppleProDOS800MGRegistrar {
        AppleProDOS800MGRegistrar() {
            DiskImageFactory::registerFormat(DiskFormat::Apple800MG,
                []() -> std::unique_ptr<DiskImage> {
                    return std::make_unique<AppleProDOS800MGImage>();
                });
        }
    };
    static AppleProDOS800MGRegistrar registrar;

    uint16_t u16(const std::vector<uint8_t>& d, size_t o) {
        return static_cast<uint16_t>(d[o] | (d[o + 1] << 8));
    }
    uint32_t u32(const std::vector<uint8_t>& d, size_t o) {
        return static_cast<uint32_t>(d[o]) | (static_cast<uint32_t>(d[o + 1]) << 8) |
               (static_cast<uint32_t>(d[o + 2]) << 16) | (static_cast<uint32_t>(d[o + 3]) << 24);
    }
    void put32(std::vector<uint8_t>& d, size_t o, uint32_t v) {
        for (int i = 0; i < 4; ++i) d[o + i] = static_cast<uint8_t>(v >> (8 * i));
    }
    [[noreturn]] void bad(const std::string& why) {
        throw InvalidFormatException("2MG: " + why);
    }
}

void AppleProDOS800MGImage::load(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        throw FileNotFoundException(path.string());
    }
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw ReadException("Cannot open file: " + path.string());
    }
    const size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> raw(fileSize);
    file.read(reinterpret_cast<char*>(raw.data()), fileSize);
    if (!file) {
        throw ReadException("Failed to read file: " + path.string());
    }

    // "GMI2": the signature byte-reversed, as Bernie ][ The Rescue writes it
    // (other fields valid; MAME ap_dsk35.cpp accepts it too). Kept as found.
    const bool magic = raw.size() >= HEADER_SIZE &&
                       ((raw[0] == '2' && raw[1] == 'I' && raw[2] == 'M' && raw[3] == 'G') ||
                        (raw[0] == 'G' && raw[1] == 'M' && raw[2] == 'I' && raw[3] == '2'));
    if (!magic) {
        bad("missing \"2IMG\" header");
    }
    if (u16(raw, 0x08) != HEADER_SIZE) {
        bad("header size " + std::to_string(u16(raw, 0x08)) + " (expected 64)");
    }
    if (u16(raw, 0x0A) > 1) {
        bad("unsupported header version " + std::to_string(u16(raw, 0x0A)));
    }
    const uint32_t format = u32(raw, 0x0C);
    if (format != 1) {
        bad(format == 0 ? "DOS 3.3 sector order is not supported (only ProDOS order 800K)"
            : format == 2 ? "nibble data is not supported (only ProDOS order 800K)"
                          : "unknown image format " + std::to_string(format));
    }
    const uint32_t blocks = u32(raw, 0x14);
    if (blocks != BLOCKS) {
        bad(std::to_string(blocks) + " blocks (only 800K = 1600 blocks is supported)");
    }
    const uint64_t offset = u32(raw, 0x18);
    uint64_t length = u32(raw, 0x1C);
    if (length == 0) {
        length = IMAGE_SIZE;  // some writers leave it 0 (AppleWin accepts that)
    }
    if (length != IMAGE_SIZE) {
        bad("data length " + std::to_string(length) + " does not match 1600 blocks");
    }
    if (offset < HEADER_SIZE || offset + length > raw.size()) {
        bad("data range " + std::to_string(offset) + "+" + std::to_string(length) +
            " is outside the file (" + std::to_string(raw.size()) + " bytes)");
    }
    // Comment and creator data: inside the file, clear of the header and data
    for (const size_t at : {size_t(0x20), size_t(0x28)}) {
        const uint64_t o = u32(raw, at);
        const uint64_t n = u32(raw, at + 4);
        if (n == 0) continue;
        const char* what = at == 0x20 ? "comment" : "creator data";
        if (o < HEADER_SIZE || o + n > raw.size() || (o < offset + length && offset < o + n)) {
            bad(std::string(what) + " range " + std::to_string(o) + "+" + std::to_string(n) +
                " overlaps the header or data, or is outside the file");
        }
    }

    m_file = std::move(raw);
    m_dataOffset = static_cast<size_t>(offset);
    m_flags = u32(m_file, 0x10);
    m_commentLength = u32(m_file, 0x20) ? u32(m_file, 0x24) : 0;
    m_creatorDataLength = u32(m_file, 0x28) ? u32(m_file, 0x2C) : 0;
    m_creator.assign(m_file.begin() + 4, m_file.begin() + 8);
    m_data.assign(m_file.begin() + m_dataOffset, m_file.begin() + m_dataOffset + IMAGE_SIZE);
    m_writeProtected = isLocked();
    m_filePath = path;
    m_modified = false;
}

void AppleProDOS800MGImage::save(const std::filesystem::path& path) {
    std::filesystem::path savePath = path.empty() ? m_filePath : path;
    if (savePath.empty()) {
        throw WriteException("No file path specified");
    }
    if (isLocked()) {
        // Locked in the 2MG header: never written back, whatever the path
        throw WriteProtectedException();
    }
    if (m_writeProtected && savePath == m_filePath) {
        throw WriteProtectedException();
    }
    std::vector<uint8_t> out = m_file;
    std::copy(m_data.begin(), m_data.end(), out.begin() + m_dataOffset);

    std::ofstream file(savePath, std::ios::binary);
    if (!file) {
        throw WriteException("Cannot create file: " + savePath.string());
    }
    file.write(reinterpret_cast<const char*>(out.data()), out.size());
    if (!file) {
        throw WriteException("Failed to write file: " + savePath.string());
    }
    m_file = std::move(out);
    if (path.empty() || path == m_filePath) {
        m_modified = false;
    }
    m_filePath = savePath;
}

void AppleProDOS800MGImage::create(const DiskGeometry& geometry) {
    AppleProDOS800Image::create(geometry);  // checks the geometry, zero data
    m_file.assign(HEADER_SIZE + IMAGE_SIZE, 0);
    const char id[] = {'2', 'I', 'M', 'G', 'R', 'D', 'E', 'T'};
    std::copy(id, id + 8, m_file.begin());
    m_file[0x08] = static_cast<uint8_t>(HEADER_SIZE);
    m_file[0x0A] = 1;                          // version
    put32(m_file, 0x0C, 1);                    // ProDOS order
    put32(m_file, 0x14, static_cast<uint32_t>(BLOCKS));
    put32(m_file, 0x18, static_cast<uint32_t>(HEADER_SIZE));
    put32(m_file, 0x1C, static_cast<uint32_t>(IMAGE_SIZE));
    m_dataOffset = HEADER_SIZE;
    m_flags = 0;
    m_commentLength = 0;
    m_creatorDataLength = 0;
    m_creator = "RDET";
}

void AppleProDOS800MGImage::setRawData(const std::vector<uint8_t>& data) {
    if (m_writeProtected) {
        throw WriteProtectedException();
    }
    AppleProDOS800Image::setRawData(data);
}

bool AppleProDOS800MGImage::hasContainerData() const {
    return m_commentLength > 0 || m_creatorDataLength > 0 || isLocked();
}

std::string AppleProDOS800MGImage::getDiagnostics() const {
    std::ostringstream oss;
    oss << "Format: Apple II ProDOS 800K (.2mg)\n";
    oss << "2MG Creator: " << m_creator << "\n";
    oss << "2MG Flags: 0x" << std::hex << m_flags << std::dec
        << (isLocked() ? " (locked)" : "") << "\n";
    oss << "2MG Comment: " << m_commentLength << " bytes\n";
    oss << "2MG Creator Data: " << m_creatorDataLength << " bytes\n";
    std::string rest = AppleProDOS800Image::getDiagnostics();
    rest.erase(0, rest.find('\n') + 1);  // drop the parent's "Format:" line
    oss << rest;
    return oss.str();
}

} // namespace rde
