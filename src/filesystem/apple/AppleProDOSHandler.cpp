#include <cstdint>
#include <cstddef>

/**
 * Apple II ProDOS File System Handler
 *
 * ProDOS Structure:
 * - Block-based (512 bytes per block)
 * - Volume directory at block 2
 * - Subdirectories supported
 * - Index blocks for large files
 *
 * This is a stub implementation. Full implementation would include:
 * - Volume directory parsing
 * - Subdirectory navigation
 * - Block allocation map
 * - Index block handling (sapling, tree files)
 */

namespace rde {

// ProDOS constants
constexpr size_t PRODOS_BLOCK_SIZE = 512;
constexpr size_t PRODOS_BLOCKS_PER_TRACK = 8;
constexpr size_t PRODOS_VOLUME_DIR_BLOCK = 2;
constexpr size_t PRODOS_BITMAP_BLOCK = 6;

// Storage types
constexpr uint8_t STORAGE_DELETED = 0x00;
constexpr uint8_t STORAGE_SEEDLING = 0x10;  // < 512 bytes
constexpr uint8_t STORAGE_SAPLING = 0x20;   // 513 - 131072 bytes
constexpr uint8_t STORAGE_TREE = 0x30;      // > 131072 bytes
constexpr uint8_t STORAGE_SUBDIRECTORY = 0xD0;
constexpr uint8_t STORAGE_SUBDIR_HEADER = 0xE0;
constexpr uint8_t STORAGE_VOLUME_HEADER = 0xF0;

// Directory entry structure
constexpr size_t DIR_ENTRY_SIZE = 0x27;  // 39 bytes
constexpr size_t DIR_ENTRIES_PER_BLOCK = 13;

// Entry offsets
constexpr size_t ENTRY_STORAGE_TYPE = 0x00;
constexpr size_t ENTRY_FILENAME = 0x01;
constexpr size_t ENTRY_FILE_TYPE = 0x10;
constexpr size_t ENTRY_KEY_POINTER = 0x11;
constexpr size_t ENTRY_BLOCKS_USED = 0x13;
constexpr size_t ENTRY_EOF = 0x15;
constexpr size_t ENTRY_CREATION = 0x18;
constexpr size_t ENTRY_VERSION = 0x1C;
constexpr size_t ENTRY_MIN_VERSION = 0x1D;
constexpr size_t ENTRY_ACCESS = 0x1E;
constexpr size_t ENTRY_AUX_TYPE = 0x1F;
constexpr size_t ENTRY_MOD_DATE = 0x21;
constexpr size_t ENTRY_HEADER_POINTER = 0x25;

// File types
constexpr uint8_t PRODOS_TYPE_UNK = 0x00;
constexpr uint8_t PRODOS_TYPE_BAD = 0x01;
constexpr uint8_t PRODOS_TYPE_TXT = 0x04;
constexpr uint8_t PRODOS_TYPE_BIN = 0x06;
constexpr uint8_t PRODOS_TYPE_DIR = 0x0F;
constexpr uint8_t PRODOS_TYPE_ADB = 0x19;
constexpr uint8_t PRODOS_TYPE_AWP = 0x1A;
constexpr uint8_t PRODOS_TYPE_ASP = 0x1B;
constexpr uint8_t PRODOS_TYPE_BAS = 0xFC;
constexpr uint8_t PRODOS_TYPE_VAR = 0xFD;
constexpr uint8_t PRODOS_TYPE_REL = 0xFE;
constexpr uint8_t PRODOS_TYPE_SYS = 0xFF;

} // namespace rde
