#include "rdedisktool/apple/AppleD13Image.h"
#include "rdedisktool/DiskImageFactory.h"

namespace rde {

namespace {
    struct AppleD13Registrar {
        AppleD13Registrar() {
            DiskImageFactory::registerFormat(DiskFormat::AppleD13,
                []() -> std::unique_ptr<DiskImage> {
                    return std::make_unique<AppleD13Image>();
                });
        }
    };
    static AppleD13Registrar registrar;
}

void AppleD13Image::load(const std::filesystem::path& path) {
    AppleDOImage::load(path);
    if (m_geometry.sectorsPerTrack != SECTORS_13) {
        throw InvalidFormatException(".d13 image must be 35 x 13 x 256 = 116480 bytes");
    }
}

void AppleD13Image::create(const DiskGeometry& geometry) {
    DiskGeometry g = geometry;
    if (g.tracks == 0) g.tracks = TRACKS_35;
    g.sides = 1;
    g.sectorsPerTrack = SECTORS_13;
    g.bytesPerSector = BYTES_PER_SECTOR;
    AppleDOImage::create(g);
}

bool AppleD13Image::canConvertTo(DiskFormat /*format*/) const {
    // Use the CLI convert command (sector copy); no library conversion
    return false;
}

std::string AppleD13Image::getDiagnostics() const {
    std::string text = AppleDOImage::getDiagnostics();
    const std::string from = "Apple II DOS Order (.do/.dsk)";
    const size_t pos = text.find(from);
    if (pos != std::string::npos) {
        text.replace(pos, from.size(), "Apple II DOS 3.2 13-sector (.d13)");
    }
    return text;
}

} // namespace rde
