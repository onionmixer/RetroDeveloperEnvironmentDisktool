#include "rdedisktool/macintosh/MacintoshIMGImage.h"
#include "rdedisktool/macintosh/MacintoshDC42Image.h"
#include "rdedisktool/macintosh/MacintoshMOOFImage.h"
#include "rdedisktool/DiskImageFactory.h"
#include "rdedisktool/Exceptions.h"

#include <fstream>
#include <iterator>
#include <sstream>

namespace rde {

namespace {
struct MacIMGRegistrar {
    MacIMGRegistrar() {
        DiskImageFactory::registerFormat(DiskFormat::MacIMG,
            []() { return std::make_unique<MacintoshIMGImage>(); });
    }
};
static MacIMGRegistrar s_registrar;

// Helpful but informational: known logical sizes warned only when the loader
// observes something else.
bool isKnownMacRawSize(size_t s) {
    return s == MacintoshDiskImage::SIZE_400K  ||
           s == MacintoshDiskImage::SIZE_720K  ||
           s == MacintoshDiskImage::SIZE_800K  ||
           s == MacintoshDiskImage::SIZE_1440K;
}
} // namespace

MacintoshIMGImage::MacintoshIMGImage() = default;

void MacintoshIMGImage::load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw InvalidFormatException("Cannot open: " + path.string());
    }
    in.seekg(0, std::ios::end);
    const auto sz = static_cast<size_t>(in.tellg());
    in.seekg(0, std::ios::beg);

    if (sz == 0 || (sz % SECTOR_SIZE) != 0) {
        throw InvalidFormatException(
            "Macintosh raw image must be a non-zero multiple of 512 bytes (got " +
            std::to_string(sz) + ")");
    }

    m_data.resize(sz);
    if (!in.read(reinterpret_cast<char*>(m_data.data()), static_cast<std::streamsize>(sz))) {
        throw InvalidFormatException("Read failed: " + path.string());
    }

    m_filePath = path;
    m_modified = false;
    m_writeProtected = false;
    m_fileSystemDetected = false;
    initGeometryFromSize(sz);
}

void MacintoshIMGImage::save(const std::filesystem::path& path) {
    const std::filesystem::path target = path.empty() ? m_filePath : path;
    if (target.empty()) {
        throw InvalidFormatException("Macintosh IMG save: no destination path");
    }
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw InvalidFormatException("Cannot open for write: " + target.string());
    }
    if (!m_data.empty()) {
        out.write(reinterpret_cast<const char*>(m_data.data()),
                  static_cast<std::streamsize>(m_data.size()));
    }
    if (!out) {
        throw InvalidFormatException("Write failed: " + target.string());
    }
    out.flush();
    if (!out) {
        throw InvalidFormatException("Flush failed: " + target.string());
    }
    m_modified = false;
}

void MacintoshIMGImage::create(const DiskGeometry& geometry) {
    // Mac IMG is a flat 512B-sector stream. The geometry's logical layout
    // (tracks × sides × sectorsPerTrack × bytesPerSector) just gives us the
    // total byte count to allocate; format() on the handler then fills in
    // boot/MDB/directory bytes.
    const size_t total = static_cast<size_t>(geometry.tracks) *
                          static_cast<size_t>(geometry.sides) *
                          static_cast<size_t>(geometry.sectorsPerTrack) *
                          static_cast<size_t>(geometry.bytesPerSector);
    if (total == 0 || (total % SECTOR_SIZE) != 0) {
        throw InvalidFormatException("Macintosh IMG create: geometry must yield "
                                      "a non-zero multiple of 512 bytes");
    }
    m_data.assign(total, 0);
    m_geometry = geometry;
    m_modified = true;
    m_writeProtected = false;
    m_fileSystemDetected = false;
}

bool MacintoshIMGImage::canConvertTo(DiskFormat format) const {
    switch (format) {
        case DiskFormat::MacDC42:
        case DiskFormat::MacMOOF:
            return true;
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
        case DiskFormat::MacIMG:
            return false;
    }
    return false;
}

std::unique_ptr<DiskImage> MacintoshIMGImage::convertTo(DiskFormat format) const {
    if (format == DiskFormat::MacDC42) {
        auto out = std::make_unique<MacintoshDC42Image>();
        out->setRawData(m_data);
        return out;
    }
    if (format == DiskFormat::MacMOOF) {
        auto out = std::make_unique<MacintoshMOOFImage>();
        out->setRawData(m_data);
        return out;
    }
    throw NotImplementedException("Macintosh IMG convertTo " +
                                   std::string(formatToString(format)) + " (Phase 3)");
}

bool MacintoshIMGImage::validate() const {
    if (m_data.empty() || (m_data.size() % SECTOR_SIZE) != 0) {
        return false;
    }
    return true;
}

std::string MacintoshIMGImage::getDiagnostics() const {
    std::ostringstream oss;
    oss << "Format: Macintosh Raw Image (.img / .dsk)\n";
    oss << "Size: " << m_data.size() << " bytes\n";
    oss << "Sectors: " << (m_data.size() / SECTOR_SIZE) << " (logical 512B)\n";
    if (!isKnownMacRawSize(m_data.size())) {
        oss << "Note: non-standard size (expected 400K/720K/800K/1440K)\n";
    }
    oss << "File System: " << fileSystemTypeToString(getFileSystemType()) << "\n";
    oss << "Write Protected: " << (m_writeProtected ? "Yes" : "No") << "\n";
    oss << "Modified: " << (m_modified ? "Yes" : "No") << "\n";
    return oss.str();
}

} // namespace rde
