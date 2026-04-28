#include "rdedisktool/macintosh/MacintoshMOOFImage.h"
#include "rdedisktool/macintosh/MacGcrDecoder.h"
#include "rdedisktool/macintosh/MacMfmDecoder.h"
#include "rdedisktool/DiskImageFactory.h"
#include "rdedisktool/Exceptions.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>

namespace rde {

namespace {

// Static registrar — wires MacMOOF into DiskImageFactory at startup.
struct MacMOOFRegistrar {
    MacMOOFRegistrar() {
        DiskImageFactory::registerFormat(DiskFormat::MacMOOF,
            []() { return std::make_unique<MacintoshMOOFImage>(); });
    }
};
static MacMOOFRegistrar s_registrar;

inline uint16_t readLE16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}
inline uint32_t readLE32(const uint8_t* p) {
    return  static_cast<uint32_t>(p[0])        |
           (static_cast<uint32_t>(p[1]) <<  8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

// CRC32-ISO-HDLC table, polynomial 0xEDB88320 (Gary S. Brown 1986). Same
// polynomial as zlib / PNG / Ethernet. Used by MOOF spec.
uint32_t kCrc32Table[256];
bool     kCrc32TableInit = false;
void initCrc32Table() {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        kCrc32Table[i] = c;
    }
    kCrc32TableInit = true;
}

} // namespace

uint32_t MacintoshMOOFImage::computeCrc32(const uint8_t* data, size_t len) {
    if (!kCrc32TableInit) initCrc32Table();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        c = kCrc32Table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

MacintoshMOOFImage::MacintoshMOOFImage() = default;

void MacintoshMOOFImage::load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw InvalidFormatException("Cannot open: " + path.string());
    }
    in.seekg(0, std::ios::end);
    const auto sz = static_cast<size_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    if (sz < 12) {
        throw InvalidFormatException("MOOF: file shorter than 12-byte header");
    }
    m_fileBytes.resize(sz);
    if (!in.read(reinterpret_cast<char*>(m_fileBytes.data()),
                 static_cast<std::streamsize>(sz))) {
        throw InvalidFormatException("MOOF: read failed: " + path.string());
    }

    // Header
    static constexpr uint8_t kMagic[8] = {'M','O','O','F',0xff,0x0a,0x0d,0x0a};
    if (std::memcmp(m_fileBytes.data(), kMagic, 8) != 0) {
        throw InvalidFormatException("MOOF: missing magic / LF CR LF");
    }
    const uint32_t crcStored = readLE32(m_fileBytes.data() + 8);
    if (crcStored != 0) {
        const uint32_t crcCalc =
            computeCrc32(m_fileBytes.data() + 12, m_fileBytes.size() - 12);
        if (crcStored != crcCalc) {
            std::ostringstream oss;
            oss << "MOOF: CRC mismatch (stored 0x" << std::hex << crcStored
                << ", calculated 0x" << crcCalc << ")";
            throw InvalidFormatException(oss.str());
        }
    }

    parseChunks(m_fileBytes);

    m_filePath = path;
    m_modified = false;
    m_writeProtected = m_info.writeProtected;
    m_fileSystemDetected = false;

    // Decode the bitstream tracks into a flat 512B sector image.
    //   GCR 400K / 800K  → MacGcrDecoder (Apple 6-and-2)
    //   MFM 1.44M        → MacMfmDecoder (IBM PC standard)
    switch (m_info.diskType) {
        case DiskType::SsDdGcr400K:
        case DiskType::DsDdGcr800K:
            decodeGcrIntoSectorStream();
            break;
        case DiskType::DsHdMfm144M:
            decodeMfmIntoSectorStream();
            break;
        case DiskType::Twiggy:
        case DiskType::Unknown:
            throw InvalidFormatException(
                "Macintosh MOOF: unsupported disk type " +
                std::to_string(static_cast<int>(m_info.diskType)));
    }

    m_writeProtected = true;  // E1/E2 are read-only
}

void MacintoshMOOFImage::decodeGcrIntoSectorStream() {
    const int sides = (m_info.diskType == DiskType::DsDdGcr800K) ? 2 : 1;
    const size_t totalSectors = static_cast<size_t>(macGcrLinearBlock(
        sides, 80, 0, 0));
    m_data.assign(totalSectors * SECTOR_SIZE, 0);

    std::vector<bool> filled(totalSectors, false);
    std::string errAccum;
    size_t decodedCount = 0;

    // Walk the TMAP. Per the MOOF spec (and snow's reference loader), TMAP
    // is always a fixed 80×2 = 160-byte structure with track-major / side-
    // minor layout: byte offset = track * 2 + side. For single-sided (400K)
    // the side-1 entries are 0xFF.
    for (int track = 0; track < 80; ++track) {
        for (int side = 0; side < sides; ++side) {
            const size_t tmapIdx = static_cast<size_t>(track * 2 + side);
            const uint8_t trkIdx = m_tmap[tmapIdx];
            if (trkIdx == 0xFF) continue;
            if (trkIdx >= m_trks.size()) continue;

            const TrkEntry& te = m_trks[trkIdx];
            if (te.blockCount == 0 || te.bitOrByteCount == 0) continue;
            const size_t startOff = static_cast<size_t>(te.startBlock) * 512u;
            const size_t bitCount = static_cast<size_t>(te.bitOrByteCount);
            const size_t byteLen  = (bitCount + 7) / 8;
            if (startOff + byteLen > m_fileBytes.size()) {
                throw InvalidFormatException(
                    "MOOF TRKS entry " + std::to_string(trkIdx) +
                    " runs past EOF");
            }

            auto sectors = decodeMacGcrTrack(
                m_fileBytes.data() + startOff, bitCount, errAccum);
            for (const auto& s : sectors) {
                if (!s.headerChecksumOk || !s.dataChecksumOk) continue;
                if (s.track != track || s.side != side) continue;
                if (s.sector >= macGcrSectorsForTrack(track)) continue;
                const size_t lin = macGcrLinearBlock(
                    sides, s.track, s.side, s.sector);
                if (lin >= totalSectors) continue;
                if (filled[lin]) continue;
                std::memcpy(m_data.data() + lin * SECTOR_SIZE,
                            s.data.data(), SECTOR_SIZE);
                filled[lin] = true;
                ++decodedCount;
            }
        }
    }

    if (decodedCount == 0) {
        throw InvalidFormatException(
            "Macintosh MOOF: no sectors decoded from any track" +
            (errAccum.empty() ? std::string() : (" (" + errAccum + ")")));
    }

    initGeometryFromSize(m_data.size());
}

void MacintoshMOOFImage::decodeMfmIntoSectorStream() {
    // 1.44M MFM: 80 cylinders × 2 heads × 18 sectors × 512B = 1,474,560 bytes.
    constexpr int kCylinders       = 80;
    constexpr int kHeads           = 2;
    constexpr int kSectorsPerTrack = 18;
    const size_t totalSectors =
        static_cast<size_t>(kCylinders * kHeads * kSectorsPerTrack);
    m_data.assign(totalSectors * SECTOR_SIZE, 0);

    std::vector<bool> filled(totalSectors, false);
    std::string errAccum;
    size_t decodedCount = 0;

    for (int cyl = 0; cyl < kCylinders; ++cyl) {
        for (int head = 0; head < kHeads; ++head) {
            // TMAP layout is always track-major / side-minor (track*2 + side).
            const size_t tmapIdx = static_cast<size_t>(cyl * 2 + head);
            const uint8_t trkIdx = m_tmap[tmapIdx];
            if (trkIdx == 0xFF) continue;
            if (trkIdx >= m_trks.size()) continue;

            const TrkEntry& te = m_trks[trkIdx];
            if (te.blockCount == 0 || te.bitOrByteCount == 0) continue;
            const size_t startOff = static_cast<size_t>(te.startBlock) * 512u;
            const size_t bitCount = static_cast<size_t>(te.bitOrByteCount);
            const size_t byteLen  = (bitCount + 7) / 8;
            if (startOff + byteLen > m_fileBytes.size()) {
                throw InvalidFormatException(
                    "MOOF TRKS entry " + std::to_string(trkIdx) +
                    " runs past EOF");
            }

            auto sectors = decodeMacMfmTrack(
                m_fileBytes.data() + startOff, bitCount, errAccum);
            for (const auto& s : sectors) {
                if (!s.headerCrcOk || !s.dataCrcOk) continue;
                if (s.cylinder != cyl || s.head != head) continue;
                if (s.sector < 1 || s.sector > kSectorsPerTrack) continue;
                if (s.sizeCode != 2) continue;
                const size_t lin = macMfmLinearBlock(s.cylinder, s.head, s.sector);
                if (lin >= totalSectors) continue;
                if (filled[lin]) continue;
                std::memcpy(m_data.data() + lin * SECTOR_SIZE,
                            s.data.data(), SECTOR_SIZE);
                filled[lin] = true;
                ++decodedCount;
            }
        }
    }

    if (decodedCount == 0) {
        throw InvalidFormatException(
            "Macintosh MOOF (MFM): no sectors decoded from any track" +
            (errAccum.empty() ? std::string() : (" (" + errAccum + ")")));
    }

    initGeometryFromSize(m_data.size());
}

void MacintoshMOOFImage::parseChunks(const std::vector<uint8_t>& bytes) {
    size_t pos = 12;
    bool sawInfo = false;
    bool sawTmap = false;
    bool sawTrks = false;
    while (pos + 8 <= bytes.size()) {
        const uint8_t* h = bytes.data() + pos;
        const char  cid[4] = { static_cast<char>(h[0]), static_cast<char>(h[1]),
                                static_cast<char>(h[2]), static_cast<char>(h[3]) };
        const uint32_t size = readLE32(h + 4);
        const size_t   payOff = pos + 8;
        if (payOff + size > bytes.size()) {
            throw InvalidFormatException(
                "MOOF: chunk '" + std::string(cid, 4) +
                "' runs past EOF (size=" + std::to_string(size) + ")");
        }
        const uint8_t* pay = bytes.data() + payOff;

        if (std::memcmp(cid, "INFO", 4) == 0) {
            if (size < 60) {
                throw InvalidFormatException(
                    "MOOF INFO: short payload (" + std::to_string(size) + " < 60)");
            }
            m_info.version          = pay[0];
            m_info.diskType         = static_cast<DiskType>(pay[1]);
            m_info.writeProtected   = pay[2] != 0;
            m_info.synchronized     = pay[3] != 0;
            m_info.optimalBitTiming = pay[4];
            // Creator: 32 bytes UTF-8, space-padded.
            std::string c(reinterpret_cast<const char*>(pay + 5), 32);
            while (!c.empty() && (c.back() == ' ' || c.back() == '\0')) c.pop_back();
            m_info.creator = std::move(c);
            // pay[37] padding
            m_info.largestTrackBlocks      = readLE16(pay + 38);
            m_info.fluxBlock               = readLE16(pay + 40);
            m_info.largestFluxTrackBlocks  = readLE16(pay + 42);
            sawInfo = true;
        } else if (std::memcmp(cid, "TMAP", 4) == 0) {
            if (size < 160) {
                throw InvalidFormatException("MOOF TMAP: short payload");
            }
            std::memcpy(m_tmap.data(), pay, 160);
            sawTmap = true;
        } else if (std::memcmp(cid, "FLUX", 4) == 0) {
            if (size < 160) {
                throw InvalidFormatException("MOOF FLUX: short payload");
            }
            std::memcpy(m_fluxMap.data(), pay, 160);
            m_hasFluxChunk = true;
        } else if (std::memcmp(cid, "TRKS", 4) == 0) {
            // 160 entries × 8 bytes header, then bit/byte data (skipped here —
            // the loader reads from m_fileBytes via TrkEntry.startBlock).
            if (size < 160U * 8U) {
                throw InvalidFormatException("MOOF TRKS: short header");
            }
            for (size_t i = 0; i < 160; ++i) {
                const uint8_t* e = pay + i * 8;
                m_trks[i].startBlock      = readLE16(e + 0);
                m_trks[i].blockCount      = readLE16(e + 2);
                m_trks[i].bitOrByteCount  = readLE32(e + 4);
            }
            sawTrks = true;
        } else if (std::memcmp(cid, "META", 4) == 0) {
            std::string raw(reinterpret_cast<const char*>(pay), size);
            std::istringstream iss(raw);
            std::string line;
            while (std::getline(iss, line)) {
                const auto tab = line.find('\t');
                if (tab == std::string::npos) continue;
                m_meta[line.substr(0, tab)] = line.substr(tab + 1);
            }
        }
        // Unknown chunks: silently skip (forward compat per spec).

        pos = payOff + size;
    }

    if (!sawInfo) {
        throw InvalidFormatException("MOOF: missing INFO chunk");
    }
    if (!sawTmap) {
        throw InvalidFormatException("MOOF: missing TMAP chunk");
    }
    if (!sawTrks) {
        throw InvalidFormatException("MOOF: missing TRKS chunk");
    }
}

void MacintoshMOOFImage::save(const std::filesystem::path& /*path*/) {
    throw NotImplementedException(
        "Macintosh MOOF save: deferred to E3 (GCR encode + chunk emit).");
}

void MacintoshMOOFImage::create(const DiskGeometry& /*geometry*/) {
    throw NotImplementedException(
        "Macintosh MOOF create: deferred to E3 (cannot generate empty MOOF "
        "without GCR/MFM encoder).");
}

bool MacintoshMOOFImage::canConvertTo(DiskFormat format) const {
    switch (format) {
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
        case DiskFormat::MacDC42:
        case DiskFormat::MacMOOF:
            return false;
    }
    return false;
}

std::unique_ptr<DiskImage> MacintoshMOOFImage::convertTo(DiskFormat format) const {
    throw NotImplementedException("Macintosh MOOF convertTo " +
                                   std::string(formatToString(format)) +
                                   " (deferred — E1-γ adds mac_img target).");
}

bool MacintoshMOOFImage::validate() const {
    return m_info.diskType != DiskType::Unknown;
}

std::string MacintoshMOOFImage::getDiagnostics() const {
    std::ostringstream oss;
    oss << "Format: Applesauce MOOF (.moof)\n";
    if (!m_info.creator.empty()) {
        oss << "Creator: " << m_info.creator << "\n";
    }
    auto diskTypeName = [&]() -> const char* {
        switch (m_info.diskType) {
            case DiskType::SsDdGcr400K: return "Single-sided 400K GCR";
            case DiskType::DsDdGcr800K: return "Double-sided 800K GCR";
            case DiskType::DsHdMfm144M: return "1.44M MFM";
            case DiskType::Twiggy:      return "Twiggy";
            case DiskType::Unknown:     return "(unknown)";
        }
        return "(unknown)";
    };
    oss << "Disk Type: " << static_cast<int>(m_info.diskType)
        << " (" << diskTypeName() << ")\n";
    oss << "INFO Version: " << static_cast<int>(m_info.version) << "\n";
    oss << "Write Protected: " << (m_info.writeProtected ? "Yes" : "No") << "\n";
    oss << "Synchronized: " << (m_info.synchronized ? "Yes" : "No") << "\n";
    oss << "Optimal Bit Timing: " << static_cast<int>(m_info.optimalBitTiming)
        << " (125ns units)\n";
    oss << "Largest Track: " << m_info.largestTrackBlocks << " blocks\n";
    oss << "FLUX chunk present: " << (m_hasFluxChunk ? "Yes" : "No") << "\n";
    if (!m_meta.empty()) {
        oss << "Metadata:\n";
        for (const auto& [k, v] : m_meta) {
            if (v.empty()) continue;
            oss << "  " << k << ": " << v << "\n";
        }
    }
    oss << "\n";
    oss << "MOOF Notice: read-only — GCR 400K/800K and 1.44M MFM decode\n"
        << "             supported. Write (E3/E4) deferred.\n";
    return oss.str();
}

} // namespace rde
