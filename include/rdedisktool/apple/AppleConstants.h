/**
 * Apple II File System Constants
 *
 * Centralized constants for DOS 3.3 and ProDOS file system handlers.
 * This file provides organized access to all Apple II file system constants
 * without forcing any inheritance structure.
 */

#ifndef RDEDISKTOOL_APPLE_APPLECONSTANTS_H
#define RDEDISKTOOL_APPLE_APPLECONSTANTS_H

#include <cstdint>
#include <cstddef>

namespace rde {
namespace AppleConstants {

//=============================================================================
// DOS 3.3 Constants
//=============================================================================
namespace DOS33 {

    // Disk Geometry
    constexpr size_t SECTOR_SIZE = 256;
    constexpr size_t SECTORS_PER_TRACK = 16;
    constexpr size_t MAX_TRACKS = 35;

    // VTOC Location (Volume Table of Contents)
    constexpr size_t VTOC_TRACK = 17;
    constexpr size_t VTOC_SECTOR = 0;

    // Catalog Location
    constexpr size_t CATALOG_TRACK = 17;
    constexpr size_t FIRST_CATALOG_SECTOR = 15;

    // Directory Entry
    constexpr size_t DIR_ENTRY_SIZE = 35;
    constexpr size_t ENTRIES_PER_SECTOR = 7;
    constexpr size_t MAX_FILENAME_LENGTH = 30;

    // File Type Codes
    constexpr uint8_t FILETYPE_TEXT = 0x00;
    constexpr uint8_t FILETYPE_INTEGER = 0x01;
    constexpr uint8_t FILETYPE_APPLESOFT = 0x02;
    constexpr uint8_t FILETYPE_BINARY = 0x04;
    constexpr uint8_t FILETYPE_STYPE = 0x08;
    constexpr uint8_t FILETYPE_RELOCATABLE = 0x10;
    constexpr uint8_t FILETYPE_A = 0x20;
    constexpr uint8_t FILETYPE_B = 0x40;

    // File Flags
    constexpr uint8_t FLAG_LOCKED = 0x80;
    constexpr uint8_t FLAG_DELETED = 0xFF;

    // T/S List
    constexpr size_t TS_PAIRS_PER_SECTOR = 122;
    constexpr size_t TS_LIST_DATA_OFFSET = 0x0C;

    // Catalog Entry Offsets
    constexpr size_t CATALOG_ENTRY_OFFSET = 0x0B;

} // namespace DOS33

//=============================================================================
// ProDOS Constants
//=============================================================================
namespace ProDOS {

    // Disk Geometry
    constexpr size_t BLOCK_SIZE = 512;
    constexpr size_t BLOCKS_PER_TRACK = 8;
    constexpr size_t TOTAL_BLOCKS = 280;          // 140K disk

    // System Block Locations
    constexpr size_t BOOT_BLOCK = 0;
    constexpr size_t VOLUME_DIR_BLOCK = 2;
    constexpr size_t BITMAP_BLOCK = 6;

    // Directory Entry
    constexpr size_t DIR_ENTRY_SIZE = 0x27;       // 39 bytes
    constexpr size_t ENTRIES_PER_BLOCK = 13;
    constexpr size_t MAX_FILENAME_LENGTH = 15;

    // Storage Types (upper nibble of storage_type_and_name_length)
    constexpr uint8_t STORAGE_DELETED = 0x00;
    constexpr uint8_t STORAGE_SEEDLING = 0x01;    // 1 data block
    constexpr uint8_t STORAGE_SAPLING = 0x02;     // 1 index + up to 256 data blocks
    constexpr uint8_t STORAGE_TREE = 0x03;        // 1 master + up to 256 index blocks
    constexpr uint8_t STORAGE_PASCAL_AREA = 0x04;
    constexpr uint8_t STORAGE_GSOS_FORK = 0x05;
    constexpr uint8_t STORAGE_SUBDIRECTORY = 0x0D;
    constexpr uint8_t STORAGE_SUBDIR_HEADER = 0x0E;
    constexpr uint8_t STORAGE_VOLUME_HEADER = 0x0F;

    // File Types
    constexpr uint8_t FILETYPE_UNK = 0x00;
    constexpr uint8_t FILETYPE_BAD = 0x01;
    constexpr uint8_t FILETYPE_PCD = 0x02;        // Pascal code
    constexpr uint8_t FILETYPE_PTX = 0x03;        // Pascal text
    constexpr uint8_t FILETYPE_TXT = 0x04;
    constexpr uint8_t FILETYPE_PDA = 0x05;        // Pascal data
    constexpr uint8_t FILETYPE_BIN = 0x06;
    constexpr uint8_t FILETYPE_FNT = 0x07;        // Font
    constexpr uint8_t FILETYPE_FOT = 0x08;        // Graphics screen
    constexpr uint8_t FILETYPE_BA3 = 0x09;        // Business BASIC program
    constexpr uint8_t FILETYPE_DA3 = 0x0A;        // Business BASIC data
    constexpr uint8_t FILETYPE_WPF = 0x0B;        // Word processor
    constexpr uint8_t FILETYPE_SOS = 0x0C;        // SOS system
    constexpr uint8_t FILETYPE_DIR = 0x0F;
    constexpr uint8_t FILETYPE_RPD = 0x10;        // RPS data
    constexpr uint8_t FILETYPE_RPI = 0x11;        // RPS index
    constexpr uint8_t FILETYPE_AFD = 0x12;        // AppleFile discard
    constexpr uint8_t FILETYPE_AFM = 0x13;        // AppleFile model
    constexpr uint8_t FILETYPE_AFR = 0x14;        // AppleFile report
    constexpr uint8_t FILETYPE_SCL = 0x15;        // Screen library
    constexpr uint8_t FILETYPE_PFS = 0x16;        // PFS document
    constexpr uint8_t FILETYPE_ADB = 0x19;        // AppleWorks database
    constexpr uint8_t FILETYPE_AWP = 0x1A;        // AppleWorks word proc
    constexpr uint8_t FILETYPE_ASP = 0x1B;        // AppleWorks spreadsheet
    constexpr uint8_t FILETYPE_CMD = 0xF0;        // ProDOS added command
    constexpr uint8_t FILETYPE_INT = 0xFA;        // Integer BASIC
    constexpr uint8_t FILETYPE_IVR = 0xFB;        // Integer BASIC variables
    constexpr uint8_t FILETYPE_BAS = 0xFC;        // Applesoft BASIC
    constexpr uint8_t FILETYPE_VAR = 0xFD;        // Applesoft variables
    constexpr uint8_t FILETYPE_REL = 0xFE;        // Relocatable code
    constexpr uint8_t FILETYPE_SYS = 0xFF;        // ProDOS system

    // Access Flags
    constexpr uint8_t ACCESS_READ = 0x01;
    constexpr uint8_t ACCESS_WRITE = 0x02;
    constexpr uint8_t ACCESS_BACKUP = 0x20;
    constexpr uint8_t ACCESS_RENAME = 0x40;
    constexpr uint8_t ACCESS_DESTROY = 0x80;
    constexpr uint8_t ACCESS_DEFAULT = 0xC3;      // Read, write, rename, destroy

    // Index Block
    constexpr size_t INDEX_ENTRIES_PER_BLOCK = 256;

} // namespace ProDOS

} // namespace AppleConstants
} // namespace rde

#endif // RDEDISKTOOL_APPLE_APPLECONSTANTS_H
