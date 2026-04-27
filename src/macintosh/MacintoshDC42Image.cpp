#include "rdedisktool/macintosh/MacintoshDC42Image.h"
#include "rdedisktool/DiskImageFactory.h"
#include "rdedisktool/Exceptions.h"

namespace rde {

namespace {
struct MacDC42Registrar {
    MacDC42Registrar() {
        DiskImageFactory::registerFormat(DiskFormat::MacDC42,
            []() { return std::make_unique<MacintoshDC42Image>(); });
    }
};
static MacDC42Registrar s_registrar;
} // namespace

MacintoshDC42Image::MacintoshDC42Image() = default;

void MacintoshDC42Image::load(const std::filesystem::path& /*path*/) {
    throw NotImplementedException("Macintosh DC42 load (M2)");
}

void MacintoshDC42Image::save(const std::filesystem::path& /*path*/) {
    throw NotImplementedException("Macintosh DC42 save (Phase 2)");
}

void MacintoshDC42Image::create(const DiskGeometry& /*geometry*/) {
    throw NotImplementedException("Macintosh DC42 create (Phase 2)");
}

SectorBuffer MacintoshDC42Image::readSector(size_t /*track*/, size_t /*side*/, size_t /*sector*/) {
    throw NotImplementedException("Macintosh DC42 readSector (M2)");
}

void MacintoshDC42Image::writeSector(size_t /*track*/, size_t /*side*/, size_t /*sector*/,
                                     const SectorBuffer& /*data*/) {
    throw NotImplementedException("Macintosh DC42 writeSector (Phase 2)");
}

TrackBuffer MacintoshDC42Image::readTrack(size_t /*track*/, size_t /*side*/) {
    throw NotImplementedException("Macintosh DC42 readTrack (M2)");
}

void MacintoshDC42Image::writeTrack(size_t /*track*/, size_t /*side*/, const TrackBuffer& /*data*/) {
    throw NotImplementedException("Macintosh DC42 writeTrack (Phase 2)");
}

bool MacintoshDC42Image::canConvertTo(DiskFormat format) const {
    switch (format) {
        case DiskFormat::MacIMG:
            // Phase 3: DC42 -> raw IMG (drop header). Stays false in M1/M2.
            return false;
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
    return false;  // Skeleton: no payload checksum to verify yet.
}

std::string MacintoshDC42Image::getDiagnostics() const {
    return "Format: Apple Disk Copy 4.2 (.image / .dc42)\nNote: M1 skeleton, full support pending.\n";
}

} // namespace rde
