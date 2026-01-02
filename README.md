# RDE Disk Tool (Retro Developer Environment Disk Tool)

A cross-platform command-line tool for manipulating disk images used by retro computer emulators. Supports Apple II and MSX disk formats.

## Features

- **Multi-platform support**: Apple II and MSX disk images
- **File operations**: List, extract, add, and delete files
- **Subdirectory support**: Full subdirectory operations for ProDOS and MSX-DOS
- **Format conversion**: Convert between compatible disk formats
- **XSA compression**: Compress/decompress MSX disk images (LZ77 + Huffman, ~99% compression)
- **Disk creation**: Create new formatted disk images
- **Validation**: Verify disk image integrity
- **Sector dump**: Raw sector/track data inspection

## Supported Formats

### Apple II
| Format | Extension | Description |
|--------|-----------|-------------|
| DOS Order | .do, .dsk | Standard DOS 3.3 sector order |
| ProDOS Order | .po | ProDOS sector order |
| Nibble | .nib | Raw nibblized format (6656 bytes/track) |
| WOZ | .woz | WOZ v1/v2 flux-level format |

### MSX
| Format | Extension | Description |
|--------|-----------|-------------|
| DSK | .dsk | Raw sector dump (720KB/360KB) |
| DMK | .dmk | DMK format with IDAM tables |
| XSA | .xsa | XSA compressed format (LZ77 + Huffman, read-only) |

> **Note**: XSA format is **read-only**. You can list and extract files, but cannot add, delete, or modify files directly. Use `convert` to decompress to DSK/DMK for modifications, then re-compress if needed.

## Supported File Systems

| File System | Platform | Subdirectories | Notes |
|-------------|----------|----------------|-------|
| DOS 3.3 | Apple II | No | VTOC-based allocation, 140KB max |
| ProDOS | Apple II | Yes | Block-based allocation, up to 32MB |
| MSX-DOS | MSX | Yes | FAT12, MSX-DOS 1/2 compatible |

## Usage

```bash
rdedisktool [options] <command> [arguments]
```

### Global Options

| Option | Description |
|--------|-------------|
| `-v, --verbose` | Enable verbose output |
| `-q, --quiet` | Suppress non-essential output |
| `-h, --help` | Show help message |
| `-V, --version` | Show version information |

### Commands

#### info - Display disk image information
```bash
rdedisktool info <image_file>
rdedisktool info <image_file> -v   # Verbose mode
```

Examples:
```bash
# Basic disk information
rdedisktool info game.dsk

# Verbose mode - includes FAT/cluster details for MSX-DOS
rdedisktool info game.dsk -v
```

Verbose output for MSX-DOS disks includes:
```
Cluster Information:
  Total Clusters:    713
  Used Clusters:     2
  Free Clusters:     711
  Cluster Size:      1024 bytes

FAT Cluster Map:
  Cluster 0: 0xFF9 (Media descriptor)
  Cluster 1: 0xFFF (Reserved)
  Cluster   2: EOF (0xFF8)
  Cluster   3: -> 4
  Cluster   4: FREE
  ...
```

#### list - List files in disk image
```bash
rdedisktool list <image_file> [path]
```

Examples:
```bash
# List root directory
rdedisktool list mydisk.dsk

# List subdirectory
rdedisktool list mydisk.dsk GAMES

# List nested subdirectory
rdedisktool list mydisk.dsk GAMES/RPG
```

Output:
```
Directory listing for: mydisk.dsk
Volume: MYDISK

Name                                Size  Type  Attr
----------------------------------------------------
HELLO.BAS                            256  FILE
GAME.COM                            8192  FILE
GAMES                                  0   DIR
README.TXT                           512  FILE
----------------------------------------------------
4 file(s), 8960 bytes
Free space: 358400 bytes
```

#### extract - Extract files from disk image
```bash
rdedisktool extract <image_file> <file> [output_path]
```

Examples:
```bash
# Extract file from root directory
rdedisktool extract game.dsk PLAYER.BIN ./player.bin

# Extract file from subdirectory (saves as GAME.COM in current dir)
rdedisktool extract game.dsk GAMES/GAME.COM

# Extract file from subdirectory with explicit output path
rdedisktool extract game.dsk GAMES/RPG/SAVE.DAT ./mysave.dat
```

#### add - Add file to disk image
```bash
rdedisktool add [--force] <image_file> <host_file> [target_name]
```

| Option | Description |
|--------|-------------|
| `-f, --force` | Overwrite existing file without prompting |

Examples:
```bash
# Add file to root directory
rdedisktool add mydisk.dsk ./newgame.com NEWGAME.COM

# Add file to subdirectory
rdedisktool add mydisk.dsk ./game.com GAMES/GAME.COM

# Add file to nested subdirectory
rdedisktool add mydisk.dsk ./save.dat GAMES/RPG/SAVE.DAT

# Overwrite existing file
rdedisktool add --force mydisk.dsk ./updated.com GAME.COM
```

#### delete - Delete file from disk image
```bash
rdedisktool delete <image_file> <file>
```

Examples:
```bash
# Delete file from root directory
rdedisktool delete mydisk.dsk OLDFILE.TXT

# Delete file from subdirectory
rdedisktool delete mydisk.dsk GAMES/OLD.COM
```

#### mkdir - Create directory (ProDOS, MSX-DOS)
```bash
rdedisktool mkdir <image_file> <directory> [-f <format>]
```

| Option | Description |
|--------|-------------|
| `-f <format>` | Specify disk format (auto-detected if not specified) |

Examples:
```bash
# Create directory in root
rdedisktool mkdir mydisk.dsk GAMES

# Create nested directory
rdedisktool mkdir mydisk.dsk GAMES/RPG
```

#### rmdir - Remove directory (ProDOS, MSX-DOS)
```bash
rdedisktool rmdir <image_file> <directory> [-f <format>]
```

| Option | Description |
|--------|-------------|
| `-f <format>` | Specify disk format (auto-detected if not specified) |

Examples:
```bash
# Remove directory (must be empty)
rdedisktool rmdir mydisk.dsk GAMES/RPG

# Remove directory from root
rdedisktool rmdir mydisk.dsk GAMES
```

> **Note**: Directories must be empty before they can be removed.

#### create - Create new disk image
```bash
rdedisktool create <file> -f <format> [--fs <filesystem>] [-n <volume>] [-g <geometry>] [--force]
```

| Option | Description |
|--------|-------------|
| `-f, --format <fmt>` | Disk format (required if not detectable from extension) |
| `--fs, --filesystem <fs>` | Initialize with filesystem: dos33, prodos, msxdos, fat12 |
| `-n, --volume <name>` | Volume name (optional, ignored for DOS 3.3) |
| `-g, --geometry <spec>` | Custom geometry: tracks:sides:sectors:bytes |
| `--force` | Overwrite existing file |

**Supported disk formats:**
| Platform | Formats |
|----------|---------|
| Apple II | do, po, nib, nb2, woz, woz1, woz2 |
| MSX | msxdsk, dmk |

Examples:
```bash
# Create Apple II DOS 3.3 disk
rdedisktool create disk.do -f do --fs dos33

# Create Apple II ProDOS disk with volume name
rdedisktool create game.po -f po --fs prodos -n MYGAME

# Create MSX-DOS disk with volume name
rdedisktool create msx.dsk -f msxdsk --fs msxdos -n MSXDISK

# Create disk with custom geometry
rdedisktool create custom.do -f do -g 40:1:16:256

# Create blank disk (no filesystem)
rdedisktool create blank.po -f po
```

> **Note**: Created disks are not bootable (no boot code included).

#### convert - Convert disk image format
```bash
rdedisktool convert <input_file> <output_file> [-f <format>]
```

| Option | Description |
|--------|-------------|
| `-f, --format <fmt>` | Output format (auto-detected from extension if not specified) |

Examples:
```bash
# Convert Apple II DOS to ProDOS order
rdedisktool convert game.do game.po -f po

# Compress MSX DSK to XSA (format auto-detected from extension)
rdedisktool convert game.dsk game.xsa

# Decompress XSA to DSK
rdedisktool convert game.xsa game.dsk -f msxdsk

# Convert between MSX formats
rdedisktool convert game.dsk game.dmk -f dmk
```

**Supported format conversions:**
| From | To | Notes |
|------|-----|-------|
| DSK | XSA | Compresses ~99% |
| DMK | XSA | Compresses ~99% |
| XSA | DSK | Decompresses to raw |
| XSA | DMK | Decompresses to DMK |
| DSK | DMK | Sector to DMK |
| DMK | DSK | DMK to sector |
| DO | PO | Apple II order swap |
| PO | DO | Apple II order swap |

#### validate - Validate disk image integrity
```bash
rdedisktool validate <image_file>
```

Examples:
```bash
rdedisktool validate mydisk.dsk
rdedisktool validate corrupted.po
```

**Validation checks:**
- Disk image structure integrity
- File system metadata consistency
- Sector/block allocation verification
- Boot block integrity (ProDOS)

#### dump - Dump sector/track data
```bash
rdedisktool dump <image_file> -t <track> -s <sector> [--side <n>] [-f <format>]
```

| Option | Description |
|--------|-------------|
| `-t, --track <n>` | Track number (0-based, required) |
| `-s, --sector <n>` | Sector number (0-based, required) |
| `--side <n>` | Side number (0-based, default: 0) |
| `-f, --format <fmt>` | Disk format (auto-detected if not specified) |

Examples:
```bash
# Dump sector from Apple II disk
rdedisktool dump disk.do -t 17 -s 0

# Dump sector from MSX disk (side 1)
rdedisktool dump disk.dsk --track 0 --sector 0 --side 1

# Dump with explicit format
rdedisktool dump disk.dsk -t 0 -s 0 -f msxdsk
```

## Examples

### Working with MSX Disks

```bash
# List files on an MSX disk
rdedisktool list game.dsk

# Extract a file
rdedisktool extract game.dsk GAME.COM ./game.com

# Add a new file
rdedisktool add game.dsk ./patch.bin PATCH.BIN

# Delete a file
rdedisktool delete game.dsk OLD.COM
```

### Working with XSA Compressed Disks

XSA is a compressed disk image format that significantly reduces file size while maintaining full compatibility. **XSA images are read-only** - you can view and extract files, but cannot modify them directly.

```bash
# View XSA disk information
rdedisktool info game.xsa

# List files in XSA disk
rdedisktool list game.xsa

# Extract a file from XSA disk
rdedisktool extract game.xsa GAME.COM ./game.com

# Compress DSK to XSA (typically achieves 98%+ compression)
rdedisktool convert game.dsk game.xsa

# Compress DMK to XSA
rdedisktool convert game.dmk game.xsa

# Decompress XSA to DSK
rdedisktool convert game.xsa game.dsk -f msxdsk

# Decompress XSA to DMK format
rdedisktool convert game.xsa game.dmk -f dmk
```

> **Modifying XSA contents**: To modify files in an XSA image, first decompress to DSK or DMK, make your changes, then re-compress to XSA.

**Typical compression results:**
| Original | Compressed | Ratio |
|----------|------------|-------|
| 720KB DSK | ~8KB XSA | ~99% |
| 360KB DSK | ~4KB XSA | ~99% |
| 1MB DMK | ~9KB XSA | ~99% |

> **Note**: Compression ratio depends on disk content. Empty or repetitive data compresses extremely well.

### Working with Apple II Disks

```bash
# Get disk information
rdedisktool info appleii.do

# List files
rdedisktool list appleii.do

# Extract Applesoft BASIC program
rdedisktool extract appleii.do HELLO hello.bas

# Add binary file
rdedisktool add appleii.do ./newprog.bin NEWPROG
```

### Working with Subdirectories

Subdirectory operations are supported for file systems that support directories: **ProDOS** and **MSX-DOS**.

> **Note**: DOS 3.3 does not support subdirectories.

#### MSX-DOS Subdirectory Example

```bash
# Create a new MSX-DOS formatted disk
rdedisktool create mydisk.dmk -f dmk --fs msxdos

# Create a directory structure
rdedisktool mkdir mydisk.dmk GAMES
rdedisktool mkdir mydisk.dmk GAMES/RPG
rdedisktool mkdir mydisk.dmk GAMES/ACTION

# Add files to subdirectories
rdedisktool add mydisk.dmk ./dragon.com GAMES/RPG/DRAGON.COM
rdedisktool add mydisk.dmk ./shooter.com GAMES/ACTION/SHOOTER.COM

# List subdirectory contents
rdedisktool list mydisk.dmk GAMES
rdedisktool list mydisk.dmk GAMES/RPG

# Extract file from subdirectory
rdedisktool extract mydisk.dmk GAMES/RPG/DRAGON.COM ./dragon_backup.com

# Delete file from subdirectory
rdedisktool delete mydisk.dmk GAMES/ACTION/SHOOTER.COM

# Remove empty directory
rdedisktool rmdir mydisk.dmk GAMES/ACTION
```

#### ProDOS Subdirectory Example

```bash
# Create a new ProDOS formatted disk
rdedisktool create mydisk.po -f po --fs prodos -n MYDISK

# Create a directory structure
rdedisktool mkdir mydisk.po DOCS
rdedisktool mkdir mydisk.po DOCS/MANUAL

# Add files to subdirectories
rdedisktool add mydisk.po ./readme.txt DOCS/README.TXT
rdedisktool add mydisk.po ./chapter1.txt DOCS/MANUAL/CHAPTER1.TXT

# List subdirectory contents
rdedisktool list mydisk.po DOCS
rdedisktool list mydisk.po DOCS/MANUAL

# Extract file from subdirectory
rdedisktool extract mydisk.po DOCS/MANUAL/CHAPTER1.TXT

# Delete and cleanup
rdedisktool delete mydisk.po DOCS/MANUAL/CHAPTER1.TXT
rdedisktool rmdir mydisk.po DOCS/MANUAL
```

## Technical Details

### MSX-DOS FAT12 Structure
- Boot sector with BPB (BIOS Parameter Block)
- Two FAT tables (FAT1 and FAT2)
- Root directory (112 entries for 720KB disk)
- Data clusters (2 sectors per cluster)
- Subdirectory support with `.` and `..` entries
- 8.3 filename format (8 characters name + 3 characters extension)

### Apple DOS 3.3 Structure
- Track 0: DOS boot code
- Track 17, Sector 0: VTOC (Volume Table of Contents)
- Track 17, Sectors 15-1: Catalog (directory)
- Each file has a Track/Sector list

### Apple ProDOS Structure
- Block-based (512 bytes per block, 280 blocks on 140KB disk)
- Blocks 0-1: Boot blocks
- Block 2+: Volume directory (key block)
- Block 6: Volume bitmap (block allocation)
- Subdirectory support with linked directory blocks
- Three storage types for files:
  - **Seedling**: Files ≤ 512 bytes (1 data block)
  - **Sapling**: Files ≤ 128KB (1 index block + up to 256 data blocks)
  - **Tree**: Files ≤ 16MB (1 master index + 256 index blocks)

### Deleted File Markers

When using `dump` to inspect directory sectors, you may see special marker bytes indicating deleted files:

| File System | Marker | Location | Description |
|-------------|--------|----------|-------------|
| MSX-DOS/FAT12 | `0xE5` | First byte of filename | File entry marked as deleted |
| DOS 3.3 | `0xFF` | T/S list track field | Catalog entry marked as deleted |
| ProDOS | `0x00` | Storage type nibble | Entry marked as deleted |

These markers are normal and indicate previously deleted files. The disk space is available for reuse.

### XSA Compressed Format
XSA (eXtendable Storage Archive) is a compressed disk image format developed by XelaSoft for MSX computers in 1994.

> **Reference**: The XSA compression/decompression implementation is based on the [MSX Disk Image Utility](https://www.msx.org/downloads/dsk-and-xsa-image-utility-linux-and-windows) (msxdiskimage.zip) source code.

**File Structure:**
- Magic number: `PCK\x08` (4 bytes)
- Original data length (4 bytes, little-endian)
- Compressed data length (4 bytes, little-endian)
- Original filename (null-terminated string)
- Compressed data stream (LZ77 + Huffman bitstream)

**Compression Algorithm:**
- LZ77-based compression with adaptive Huffman coding
- 8KB sliding window for back-references
- Maximum match length: 254 bytes
- 16 distance code buckets with variable extra bits
- Huffman tree rebuilt every 127 distance codes
- Bit-level encoding for optimal compression

**Length Encoding:**
| Bits | Length |
|------|--------|
| 0 | 2 |
| 10 | 3 |
| 110 | 4 |
| 111... | 5-254 (variable) |
| 1111110 | 255 (EOF marker) |

**Supported Operations:**
- Read: Full support (automatic decompression on load)
- Write: **Read-only** (XSA images cannot be modified directly)
- Convert: Bi-directional conversion with DSK and DMK formats
- File operations: List and extract only (add/delete/modify not supported)

## License

This project is part of the Retro Developer Environment Project.

## Contributing

Contributions are welcome! Please see the project repository for guidelines.
