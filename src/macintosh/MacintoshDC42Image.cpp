#include "rdedisktool/macintosh/MacintoshDC42Image.h"
#include "rdedisktool/macintosh/DC42Checksum.h"
#include "rdedisktool/DiskImageFactory.h"
#include "rdedisktool/Exceptions.h"

#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>

namespace rde {

namespace {
struct MacDC42Registrar {
    MacDC42Registrar() {
        DiskImageFactory::registerFormat(DiskFormat::MacDC42,
            []() { return std::make_unique<MacintoshDC42Image>(); });
    }
};
static MacDC42Registrar s_registrar;

// Big-endian readers for the 0x54-byte DC42 header. The buffer is guaranteed
// to be at least HEADER_SIZE long at every call site here.
inline uint16_t readBE16(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}
inline uint32_t readBE32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
            static_cast<uint32_t>(p[3]);
}
} // namespace

MacintoshDC42Image::MacintoshDC42Image() = default;

void MacintoshDC42Image::load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw InvalidFormatException("Cannot open: " + path.string());
    }
    in.seekg(0, std::ios::end);
    const auto fileSize = static_cast<size_t>(in.tellg());
    in.seekg(0, std::ios::beg);

    // Detection rule per SPEC §285:
    //   1. file_size >= 0x54
    //   2. name_length <= 63
    //   3. magic == 0x0100
    //   4. data_size > 0
    //   5. data_size % 2 == 0
    //   6. data_size % 512 == 0
    //   7. file_size == 0x54 + data_size + tag_size
    //   8. computed_data_checksum == data_checksum

    if (fileSize < HEADER_SIZE) {
        throw InvalidFormatException("DC42 file too small: " + std::to_string(fileSize));
    }

    std::vector<uint8_t> buffer(fileSize);
    if (!in.read(reinterpret_cast<char*>(buffer.data()),
                 static_cast<std::streamsize>(fileSize))) {
        throw InvalidFormatException("Read failed: " + path.string());
    }

    const uint8_t* h = buffer.data();
    const uint8_t nameLen      = h[0x00];
    const uint32_t dataSize    = readBE32(h + 0x40);
    const uint32_t tagSize     = readBE32(h + 0x44);
    const uint32_t dataCksumHdr = readBE32(h + 0x48);
    const uint32_t tagCksumHdr  = readBE32(h + 0x4C);
    const uint8_t  diskEncoding = h[0x50];
    const uint8_t  formatByte   = h[0x51];
    const uint16_t magic       = readBE16(h + 0x52);

    if (nameLen > 63) {
        throw InvalidFormatException("DC42: image name length > 63");
    }
    if (magic != 0x0100) {
        std::ostringstream e;
        e << "DC42: magic mismatch (got 0x" << std::hex << std::setw(4)
          << std::setfill('0') << magic << ", expected 0x0100)";
        throw InvalidFormatException(e.str());
    }
    if (dataSize == 0) {
        throw InvalidFormatException("DC42: data_size == 0");
    }
    if ((dataSize % 2) != 0) {
        throw InvalidFormatException("DC42: data_size not even");
    }
    if ((dataSize % 512) != 0) {
        throw InvalidFormatException("DC42: data_size not divisible by 512");
    }
    const size_t expectedFileSize =
        static_cast<size_t>(HEADER_SIZE) + dataSize + tagSize;
    if (fileSize != expectedFileSize) {
        std::ostringstream e;
        e << "DC42: file size mismatch (got " << fileSize
          << ", expected " << expectedFileSize << ")";
        throw InvalidFormatException(e.str());
    }

    // Payload checksum (full file scan — defended against detect()'s 64KB
    // buffer limit by doing the verification here in the loader).
    const uint32_t dataCksumComputed =
        dc42Checksum(h + HEADER_SIZE, dataSize);
    if (dataCksumComputed != dataCksumHdr) {
        std::ostringstream e;
        e << "DC42 data checksum mismatch (header 0x" << std::hex << std::setw(8)
          << std::setfill('0') << dataCksumHdr << ", computed 0x"
          << std::setw(8) << std::setfill('0') << dataCksumComputed << ")";
        throw InvalidFormatException(e.str());
    }

    // Optional tag checksum.
    if (tagSize > 0) {
        const uint32_t tagCksumComputed =
            dc42Checksum(h + HEADER_SIZE + dataSize, tagSize);
        if (tagCksumComputed != tagCksumHdr) {
            std::ostringstream e;
            e << "DC42 tag checksum mismatch (header 0x" << std::hex << std::setw(8)
              << std::setfill('0') << tagCksumHdr << ", computed 0x"
              << std::setw(8) << std::setfill('0') << tagCksumComputed << ")";
            throw InvalidFormatException(e.str());
        }
    }

    // Strip header — m_data holds only the raw sector stream so the inherited
    // sector I/O works unmodified.
    m_data.assign(buffer.begin() + HEADER_SIZE,
                  buffer.begin() + HEADER_SIZE + dataSize);

    // Container metadata.
    m_imageName.assign(reinterpret_cast<const char*>(h + 0x01),
                       static_cast<size_t>(nameLen));
    m_dataSize     = dataSize;
    m_tagSize      = tagSize;
    m_dataChecksum = dataCksumHdr;
    m_tagChecksum  = tagCksumHdr;
    m_diskEncoding = diskEncoding;
    m_formatByte   = formatByte;

    m_filePath = path;
    m_modified = false;
    m_writeProtected = false;
    m_fileSystemDetected = false;
    initGeometryFromSize(dataSize);
}

void MacintoshDC42Image::save(const std::filesystem::path& /*path*/) {
    throw NotImplementedException("Macintosh DC42 save (Phase 2)");
}

void MacintoshDC42Image::create(const DiskGeometry& /*geometry*/) {
    throw NotImplementedException("Macintosh DC42 create (Phase 2)");
}

bool MacintoshDC42Image::canConvertTo(DiskFormat format) const {
    switch (format) {
        case DiskFormat::MacIMG:
            return false;  // Phase 3
        case DiskFormat::Unknown:
        case DiskFormat::AppleDO:
        case DiskFormat::ApplePO:
        case DiskFormat::AppleNIB:
        case DiskFormat::AppleNIB2:
        case DiskFormat::AppleWOZ1:
        case DiskFormat::AppleWOZ2:
        case DiskFormat::MSXDSK:
        case DiskFormat::MSXDMK:
        case DiskFormat::MSXXSA:
        case DiskFormat::X68000XDF:
        case DiskFormat::X68000DIM:
        case DiskFormat::MacDC42:
            return false;
    }
    return false;
}

std::unique_ptr<DiskImage> MacintoshDC42Image::convertTo(DiskFormat format) const {
    throw NotImplementedException("Macintosh DC42 convertTo " +
                                   std::string(formatToString(format)) + " (Phase 3)");
}

bool MacintoshDC42Image::validate() const {
    // load() already enforces all 8 detection-rule conditions plus the data
    // and tag checksums. If the loader succeeded the container is well-formed.
    return !m_data.empty() && m_dataSize == m_data.size();
}

std::string MacintoshDC42Image::getDiagnostics() const {
    std::ostringstream oss;
    oss << "Format: Apple Disk Copy 4.2 (.image / .dc42)\n";
    if (!m_imageName.empty()) {
        oss << "Image Name: " << m_imageName << "\n";
    }
    oss << "Data Size: " << m_dataSize << " bytes ("
        << (m_dataSize / SECTOR_SIZE) << " sectors)\n";
    oss << "Tag Size: " << m_tagSize << " bytes\n";
    oss << "Data Checksum: 0x" << std::hex << std::setw(8) << std::setfill('0')
        << m_dataChecksum << std::dec << std::setfill(' ') << "\n";
    if (m_tagSize > 0) {
        oss << "Tag Checksum: 0x" << std::hex << std::setw(8) << std::setfill('0')
            << m_tagChecksum << std::dec << std::setfill(' ') << "\n";
    }
    oss << "Disk Encoding: 0x" << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(m_diskEncoding) << std::dec << std::setfill(' ') << "\n";
    oss << "Format Byte: 0x" << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(m_formatByte) << std::dec << std::setfill(' ') << "\n";
    oss << "File System: " << fileSystemTypeToString(getFileSystemType()) << "\n";
    oss << "Write Protected: " << (m_writeProtected ? "Yes" : "No") << "\n";
    oss << "Modified: " << (m_modified ? "Yes" : "No") << "\n";
    return oss.str();
}

} // namespace rde
