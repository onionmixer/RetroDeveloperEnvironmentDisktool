#include "rdedisktool/apple/AppleProDOS800Image.h"
#include "rdedisktool/DiskImageFactory.h"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace rde {

namespace {
    struct AppleProDOS800Registrar {
        AppleProDOS800Registrar() {
            DiskImageFactory::registerFormat(DiskFormat::Apple800PO,
                []() -> std::unique_ptr<DiskImage> {
                    return std::make_unique<AppleProDOS800Image>();
                });
        }
    };
    static AppleProDOS800Registrar registrar;
}

AppleProDOS800Image::AppleProDOS800Image() {
    m_geometry.tracks = TRACKS;
    m_geometry.sides = SIDES;
    m_geometry.sectorsPerTrack = SECTORS;
    m_geometry.bytesPerSector = BLOCK_SIZE;
}

void AppleProDOS800Image::load(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        throw FileNotFoundException(path.string());
    }
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw ReadException("Cannot open file: " + path.string());
    }
    const size_t fileSize = static_cast<size_t>(file.tellg());
    if (fileSize != IMAGE_SIZE) {
        throw InvalidFormatException("Apple II 800K image must be 1600 x 512 = 819200 bytes");
    }
    file.seekg(0, std::ios::beg);
    m_data.resize(fileSize);
    file.read(reinterpret_cast<char*>(m_data.data()), fileSize);
    if (!file) {
        throw ReadException("Failed to read file: " + path.string());
    }
    m_filePath = path;
    m_modified = false;
}

void AppleProDOS800Image::save(const std::filesystem::path& path) {
    std::filesystem::path savePath = path.empty() ? m_filePath : path;
    if (savePath.empty()) {
        throw WriteException("No file path specified");
    }
    if (m_writeProtected && savePath == m_filePath) {
        throw WriteProtectedException();
    }
    std::ofstream file(savePath, std::ios::binary);
    if (!file) {
        throw WriteException("Cannot create file: " + savePath.string());
    }
    file.write(reinterpret_cast<const char*>(m_data.data()), m_data.size());
    if (!file) {
        throw WriteException("Failed to write file: " + savePath.string());
    }
    if (path.empty() || path == m_filePath) {
        m_modified = false;
    }
    m_filePath = savePath;
}

void AppleProDOS800Image::create(const DiskGeometry& g) {
    // Only the one layout load() reads back; a zero field means "default"
    const bool ok = (g.tracks == 0 || g.tracks == TRACKS) &&
                    (g.sides == 0 || g.sides == SIDES) &&
                    (g.sectorsPerTrack == 0 || g.sectorsPerTrack == SECTORS) &&
                    (g.bytesPerSector == 0 || g.bytesPerSector == BLOCK_SIZE);
    if (!ok) {
        throw UnsupportedFormatException(
            "Apple II 800K images can only be created as 80 tracks, 2 sides, 10 sectors "
            "of 512 bytes (got " + std::to_string(g.tracks) + ":" + std::to_string(g.sides) +
            ":" + std::to_string(g.sectorsPerTrack) + ":" + std::to_string(g.bytesPerSector) + ")");
    }
    m_data.assign(IMAGE_SIZE, 0);
    m_modified = true;
    m_filePath.clear();
}

FileSystemType AppleProDOS800Image::getFileSystemType() const {
    // ProDOS volume directory header in block 2 (the only file system here)
    if (m_data.size() < 3 * BLOCK_SIZE) {
        return FileSystemType::Unknown;
    }
    const uint8_t* blk = m_data.data() + 2 * BLOCK_SIZE;
    const uint8_t storageType = (blk[0x04] >> 4) & 0x0F;
    const uint8_t nameLen = blk[0x04] & 0x0F;
    const uint16_t bitmapPtr = static_cast<uint16_t>(blk[0x27] | (blk[0x28] << 8));
    const uint16_t totalBlocks = static_cast<uint16_t>(blk[0x29] | (blk[0x2A] << 8));
    const bool ok = storageType == 0x0F && nameLen >= 1 && nameLen <= 15 &&
                    blk[0x23] == 0x27 && blk[0x24] > 0 &&
                    bitmapPtr > 2 && bitmapPtr < BLOCKS && totalBlocks > 0;
    return ok ? FileSystemType::ProDOS : FileSystemType::Unknown;
}

size_t AppleProDOS800Image::blockOf(size_t track, size_t side, size_t sector) const {
    if (track >= TRACKS || side >= SIDES || sector >= SECTORS) {
        throw SectorNotFoundException(static_cast<int>(track), static_cast<int>(sector));
    }
    return (track * SIDES + side) * SECTORS + sector;
}

SectorBuffer AppleProDOS800Image::readSector(size_t track, size_t side, size_t sector) {
    return readBlock(blockOf(track, side, sector));
}

void AppleProDOS800Image::writeSector(size_t track, size_t side, size_t sector,
                                      const SectorBuffer& data) {
    SectorBuffer block(data.begin(), data.begin() + std::min(data.size(), BLOCK_SIZE));
    block.resize(BLOCK_SIZE, 0);
    writeBlock(blockOf(track, side, sector), block);
}

TrackBuffer AppleProDOS800Image::readTrack(size_t track, size_t side) {
    const size_t first = blockOf(track, side, 0);
    if ((first + SECTORS) * BLOCK_SIZE > m_data.size()) {
        throw SectorNotFoundException(static_cast<int>(track), 0);
    }
    return TrackBuffer(m_data.begin() + first * BLOCK_SIZE,
                       m_data.begin() + (first + SECTORS) * BLOCK_SIZE);
}

void AppleProDOS800Image::writeTrack(size_t track, size_t side, const TrackBuffer& data) {
    if (m_writeProtected) {
        throw WriteProtectedException();
    }
    const size_t first = blockOf(track, side, 0);
    const size_t trackSize = SECTORS * BLOCK_SIZE;
    if ((first + SECTORS) * BLOCK_SIZE > m_data.size()) {
        throw SectorNotFoundException(static_cast<int>(track), 0);
    }
    const size_t copySize = std::min(data.size(), trackSize);
    auto dst = m_data.begin() + first * BLOCK_SIZE;
    std::copy(data.begin(), data.begin() + copySize, dst);
    std::fill(dst + copySize, dst + trackSize, 0);
    m_modified = true;
}

SectorBuffer AppleProDOS800Image::readBlock(size_t block) {
    if (block >= BLOCKS || (block + 1) * BLOCK_SIZE > m_data.size()) {
        throw SectorNotFoundException(static_cast<int>(block / (SIDES * SECTORS)),
                                      static_cast<int>(block % SECTORS));
    }
    return SectorBuffer(m_data.begin() + block * BLOCK_SIZE,
                        m_data.begin() + (block + 1) * BLOCK_SIZE);
}

void AppleProDOS800Image::writeBlock(size_t block, const SectorBuffer& data) {
    if (m_writeProtected) {
        throw WriteProtectedException();
    }
    if (block >= BLOCKS || (block + 1) * BLOCK_SIZE > m_data.size()) {
        throw SectorNotFoundException(static_cast<int>(block / (SIDES * SECTORS)),
                                      static_cast<int>(block % SECTORS));
    }
    if (data.size() < BLOCK_SIZE) {
        throw InvalidFormatException("Block data must be 512 bytes");
    }
    std::copy(data.begin(), data.begin() + BLOCK_SIZE, m_data.begin() + block * BLOCK_SIZE);
    m_modified = true;
}

void AppleProDOS800Image::setRawData(const std::vector<uint8_t>& data) {
    m_data = data;
    m_modified = true;
}

bool AppleProDOS800Image::canConvertTo(DiskFormat /*format*/) const {
    // Use the CLI convert command (block copy); no library conversion
    return false;
}

std::unique_ptr<DiskImage> AppleProDOS800Image::convertTo(DiskFormat format) const {
    throw UnsupportedFormatException("Cannot convert to " + std::string(formatToString(format)));
}

bool AppleProDOS800Image::validate() const {
    return m_data.size() == IMAGE_SIZE;
}

std::string AppleProDOS800Image::getDiagnostics() const {
    std::ostringstream oss;
    oss << "Format: Apple II ProDOS 800K (.po)\n";
    oss << "Size: " << m_data.size() << " bytes\n";
    oss << "Blocks: " << getTotalBlocks() << "\n";
    oss << "File System: "
        << (getFileSystemType() == FileSystemType::ProDOS ? "ProDOS" : "Unknown") << "\n";
    oss << "Write Protected: " << (m_writeProtected ? "Yes" : "No") << "\n";
    oss << "Modified: " << (m_modified ? "Yes" : "No") << "\n";
    return oss.str();
}

} // namespace rde
