#include "rdedisktool/macintosh/MacintoshIMGImage.h"
#include "rdedisktool/DiskImageFactory.h"
#include "rdedisktool/Exceptions.h"

namespace rde {

namespace {
struct MacIMGRegistrar {
    MacIMGRegistrar() {
        DiskImageFactory::registerFormat(DiskFormat::MacIMG,
            []() { return std::make_unique<MacintoshIMGImage>(); });
    }
};
static MacIMGRegistrar s_registrar;
} // namespace

MacintoshIMGImage::MacintoshIMGImage() = default;

void MacintoshIMGImage::load(const std::filesystem::path& /*path*/) {
    throw NotImplementedException("Macintosh IMG load (M2)");
}

void MacintoshIMGImage::save(const std::filesystem::path& /*path*/) {
    throw NotImplementedException("Macintosh IMG save (Phase 2)");
}

void MacintoshIMGImage::create(const DiskGeometry& /*geometry*/) {
    throw NotImplementedException("Macintosh IMG create (Phase 2)");
}

SectorBuffer MacintoshIMGImage::readSector(size_t /*track*/, size_t /*side*/, size_t /*sector*/) {
    throw NotImplementedException("Macintosh IMG readSector (M2)");
}

void MacintoshIMGImage::writeSector(size_t /*track*/, size_t /*side*/, size_t /*sector*/,
                                    const SectorBuffer& /*data*/) {
    throw NotImplementedException("Macintosh IMG writeSector (Phase 2)");
}

TrackBuffer MacintoshIMGImage::readTrack(size_t /*track*/, size_t /*side*/) {
    throw NotImplementedException("Macintosh IMG readTrack (M2)");
}

void MacintoshIMGImage::writeTrack(size_t /*track*/, size_t /*side*/, const TrackBuffer& /*data*/) {
    throw NotImplementedException("Macintosh IMG writeTrack (Phase 2)");
}

bool MacintoshIMGImage::canConvertTo(DiskFormat format) const {
    switch (format) {
        case DiskFormat::MacDC42:
            // Phase 3: MacIMG -> MacDC42 will become true. Stays false in M1/M2.
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
        case DiskFormat::MacIMG:
            return false;
    }
    return false;
}

std::unique_ptr<DiskImage> MacintoshIMGImage::convertTo(DiskFormat format) const {
    throw NotImplementedException("Macintosh IMG convertTo " +
                                   std::string(formatToString(format)) + " (Phase 3)");
}

bool MacintoshIMGImage::validate() const {
    return false;  // Skeleton: no payload to validate yet.
}

std::string MacintoshIMGImage::getDiagnostics() const {
    return "Format: Macintosh Raw Image (.img / .dsk)\nNote: M1 skeleton, full support pending.\n";
}

} // namespace rde
