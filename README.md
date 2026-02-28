# RDE Disk Tool (Retro Developer Environment Disk Tool)

A cross-platform command-line tool for manipulating disk images used by retro computer emulators. Supports Apple II, MSX, and X68000 disk formats.

## Features

- **Multi-platform support**: Apple II, MSX, and X68000 disk images
- **File operations**: List, extract, add, and delete files
- **Subdirectory support**: Full subdirectory operations for ProDOS, MSX-DOS, and Human68k
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

### X68000
| Format | Extension | Description |
|--------|-----------|-------------|
| XDF | .xdf | Raw sector dump (1.2MB, 1024 bytes/sector) |
| DIM | .dim | DIM format with 256-byte header (supports 2HD/2HS/2HC/2HDE/2HQ) |

> **Note**: X68000 uses 1024-byte sectors (for 2HD disks), different from the standard PC 512-byte sectors. Both XDF and DIM formats are fully read-write supported.

## Supported File Systems

| File System | Platform | Subdirectories | Notes |
|-------------|----------|----------------|-------|
| DOS 3.3 | Apple II | No | VTOC-based allocation, 140KB max |
| ProDOS | Apple II | Yes | Block-based allocation, up to 32MB |
| MSX-DOS | MSX | Yes | FAT12, MSX-DOS 1/2 compatible |
| Human68k | X68000 | Yes | FAT12-based, 1024-byte sectors, 8.3 filenames |

## Build & Installation

### Prerequisites
- CMake 3.16 or higher
- C++17 compatible compiler (GCC, Clang, or MSVC)

### Building from Source

```bash
# Clone the repository
git clone <repository-url>
cd RetroDeveloperEnvironmentDisktool

# Create build directory
mkdir build && cd build

# Configure and build
cmake ..
cmake --build .

# Or for Release build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

### Installation

```bash
# Install to system (requires root/admin privileges)
sudo cmake --install .

# Or specify install prefix
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
cmake --build .
sudo cmake --install .
```

### Uninstallation

```bash
# Remove installed files
sudo cmake --build . --target uninstall
```

### Build Options

| Option | Description |
|--------|-------------|
| `-DCMAKE_BUILD_TYPE=Release` | Release build with optimizations |
| `-DCMAKE_BUILD_TYPE=Debug` | Debug build with symbols |
| `-DBUILD_TESTS=ON` | Build test suite |
| `-DCMAKE_INSTALL_PREFIX=<path>` | Custom installation prefix |

## Usage

```bash
rdedisktool [options] <command> [arguments]
```

### Global Options

| Option | Description |
|--------|-------------|
| `-v, --verbose` | Enable verbose output |
| `-q, --quiet` | Suppress non-essential output |
| `--bootdisk-mode <strict|warn|off>` | Boot disk mutation protection mode (default: `strict`, safe add verification enabled) |
| `--force-bootdisk` | Override boot disk mutation block intentionally |
| `--force-system-file` | Force delete of boot-critical system files without prompt |
| `--bootdisk-profile <dos33|prodos|msxdos|human68k|unknown>` | Force bootdisk profile for detection |
| `--keep-backup` | Keep `.bak` file when saving modified image |
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

# Verbose mode - includes bootdisk detection and FAT/cluster details for MSX-DOS
rdedisktool info game.dsk -v
```

Bootdisk safety examples:
```bash
# Default strict mode: safe add verification runs automatically
rdedisktool add diskwork/bootdisk/msx/msxdos23.dsk ./PATCH.BIN PATCH.BIN

# Intentional override
rdedisktool --force-bootdisk add diskwork/bootdisk/msx/msxdos23.dsk ./PATCH.BIN PATCH.BIN
```

`strict` mode behavior on bootdisks:
- `delete/mkdir/rmdir` are blocked by default.
- `add` is allowed only when safe-add verification passes:
  - protected boot sectors unchanged
  - existing files unchanged (recursive, including subdirectories)

In verbose mode, `info -v` includes:
- `BootDisk` / `Profile` / `Confidence`
- `ProtectionMode`
- `Reason` (for example `invalid_bpb_or_filesystem_init_failed`)

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
rdedisktool add [options] <image_file> <host_file> [target_name]
```

| Option | Description |
|--------|-------------|
| `-f, --force` | Overwrite existing file without prompting |
| `-t, --type <type>` | File type for Apple II disks (see tables below) |
| `-a, --addr <addr>` | Load address for binary files (hex: 0x0803 or $0803) |

The `--type` option accepts three formats:
- **DOS 3.3 single-character codes**: `T`, `I`, `A`, `B`, `S`, `R`
- **ProDOS type names**: `SYS`, `BIN`, `TXT`, `BAS`, `CMD`, `INT`, `REL`
- **Hex values**: `0xFF` or `$FF` (any ProDOS file type code)

**DOS 3.3 File Types:**
| Type | Code | Description |
|------|------|-------------|
| T | 0x00 | Text file |
| I | 0x01 | Integer BASIC program |
| A | 0x02 | Applesoft BASIC program |
| B | 0x04 | Binary file (machine code) |
| S | 0x08 | S-type file |
| R | 0x10 | Relocatable object code |

**ProDOS File Types (can be used directly with `--type`):**
| Name | Code | Description |
|------|------|-------------|
| TXT | 0x04 | Text file |
| BIN | 0x06 | Binary file (machine code) |
| INT | 0xFA | Integer BASIC program |
| BAS | 0xFC | Applesoft BASIC program |
| REL | 0xFE | Relocatable object code |
| SYS | 0xFF | ProDOS system file (loaded at $2000 by ProDOS) |
| CMD | 0xF0 | ProDOS command file |

> **Note**: When adding files to **ProDOS** disks, DOS 3.3 file type codes are automatically converted to their ProDOS equivalents:
> | DOS 3.3 | ProDOS | ProDOS Code |
> |---------|--------|-------------|
> | T (0x00) | TXT | 0x04 |
> | I (0x01) | INT | 0xFA |
> | A (0x02) | BAS | 0xFC |
> | B (0x04) | BIN | 0x06 |
> | R (0x10) | REL | 0xFE |

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

# Add DOS 3.3 binary file with load address
rdedisktool add disk.do ./HELLO.BIN HELLO --type B --addr 0x0803

# Add binary at hi-res graphics page 2
rdedisktool add disk.do ./PICTURE.BIN MYPIC -t B -a $4000

# Add Applesoft BASIC program
rdedisktool add disk.do ./HELLO.BAS HELLO --type A

# Add ProDOS binary using type name directly
rdedisktool add disk.po ./HELLO HELLO --type BIN --addr 0x0803

# Add ProDOS system file
rdedisktool add disk.po ./MYSYS MYSYS --type SYS --addr 0x2000

# Add file using hex type code
rdedisktool add disk.po ./DATA DATA --type 0x04
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

Bootdisk safety on delete:
- If the target is a boot-critical system file, `rdedisktool` asks `yes/no` before deletion.
- `--force-system-file` skips the prompt and deletes immediately (no extra confirmation step).

#### mkdir - Create directory (ProDOS, MSX-DOS, Human68k)
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

#### rmdir - Remove directory (ProDOS, MSX-DOS, Human68k)
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
| `--fs, --filesystem <fs>` | Initialize with filesystem: dos33, prodos, msxdos, fat12, human68k |
| `-n, --volume <name>` | Volume name (optional, ignored for DOS 3.3) |
| `-g, --geometry <spec>` | Custom geometry: tracks:sides:sectors:bytes |
| `--force` | Overwrite existing file |

**Supported disk formats:**
| Platform | Formats |
|----------|---------|
| Apple II | do, po, nib, nb2, woz, woz1, woz2 |
| MSX | msxdsk, dmk |
| X68000 | xdf, dim |

Examples:
```bash
# Create Apple II DOS 3.3 disk
rdedisktool create disk.do -f do --fs dos33

# Create Apple II ProDOS disk with volume name
rdedisktool create game.po -f po --fs prodos -n MYGAME

# Create MSX-DOS disk with volume name
rdedisktool create msx.dsk -f msxdsk --fs msxdos -n MSXDISK

# Create X68000 XDF disk with Human68k filesystem
rdedisktool create x68k.xdf -f xdf --fs human68k -n X68KDISK

# Create X68000 DIM disk with Human68k filesystem
rdedisktool create x68k.dim -f dim --fs human68k -n X68KDISK

# Create disk with custom geometry
rdedisktool create custom.do -f do -g 40:1:16:256

# Create blank disk (no filesystem)
rdedisktool create blank.po -f po
```

> **Note**: Created disks are not bootable (no boot code included).

생성 검증(스크립트 권장):
```bash
# 1) create 종료코드 확인 (실패 시 즉시 처리)
rdedisktool create x68k.xdf -f xdf --fs human68k --force

# 2) info 결과에서 파일시스템 식별 문자열 확인
rdedisktool info x68k.xdf | rg -q "File System: Human68k"
```

두 검사는 모두 필수입니다. 즉, `create`가 성공(exit code 0)하고
`info` 출력의 파일시스템 문자열이 기대값과 일치해야 정상 생성/인식으로 판단합니다.

플랫폼별 문자열 예시:
- Apple DOS 3.3: `File System: DOS 3.3`
- Apple ProDOS: `File System: ProDOS`
- MSX: `File System: MSX-DOS` (MSX-DOS 1/2 공통 부분 문자열)
- X68000: `File System: Human68k`

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

## Bootdisk Disk-Add Smoke Tests

Project-root scripts for bootdisk copy -> file add -> emulator boot:

```bash
./run_applewin_dos33_diskaddtest.sh
./run_applewin_prodos_diskaddtest.sh
./run_openmsx_msxdos2_diskaddtest.sh
./run_px68k_humanos_diskaddtest.sh
```

Notes:
- Each script uses a single emulated drive for bootdisk file-control verification.
- DOS 3.3 diskaddtest includes a pre-step that removes non-essential files from the copied bootdisk before add tests.
- Current status: all four scripts pass boot smoke (`4/4`).

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

### Working with X68000 Disks

```bash
# Create a new X68000 disk with Human68k filesystem
rdedisktool create x68k.xdf -f xdf --fs human68k -n MYDISK

# Get disk information
rdedisktool info x68k.xdf

# List files
rdedisktool list x68k.xdf

# Add a file
rdedisktool add x68k.xdf ./game.x GAME.X

# Extract a file
rdedisktool extract x68k.xdf GAME.X ./game_backup.x

# Delete a file
rdedisktool delete x68k.xdf OLDFILE.DAT

# Create and manage subdirectories
rdedisktool mkdir x68k.xdf GAMES
rdedisktool add x68k.xdf ./shooter.x GAMES/SHOOTER.X
rdedisktool list x68k.xdf GAMES
rdedisktool rmdir x68k.xdf GAMES  # (must be empty)
```

> **Note**: X68000 uses 8.3 filename format. Long filenames will be truncated (e.g., `test_file.txt` becomes `TEST_FIL.TXT`).

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

# Add binary file (default type)
rdedisktool add appleii.do ./newprog.bin NEWPROG
```

#### Adding DOS 3.3 Binary Files with Load Address

DOS 3.3 binary files require a load address to execute properly with `BRUN`. The `--type` and `--addr` options allow you to specify this metadata:

```bash
# Add binary file with load address $0803 (standard for most programs)
rdedisktool add disk.do ./HELLO.BIN HELLO --type B --addr 0x0803

# Add binary file at $4000 (common for hi-res graphics)
rdedisktool add disk.do ./PICTURE.BIN MYPIC -t B -a $4000

# Add binary file at $6000 (alternative address)
rdedisktool add disk.do ./GAME.BIN GAME --type B --addr 0x6000

# Add Applesoft BASIC program
rdedisktool add disk.do ./HELLO.BAS HELLO --type A

# Add text file
rdedisktool add disk.do ./README.TXT README --type T
```

#### Adding ProDOS Files with Type Names

ProDOS disks accept type names directly via `--type`, in addition to the DOS 3.3 single-character codes and hex values:

```bash
# Add ProDOS binary with type name
rdedisktool add disk.po ./HELLO HELLO --type BIN --addr 0x0803

# Add ProDOS system file (loaded at $2000 by ProDOS kernel)
rdedisktool add disk.po ./MYSYS MYSYS --type SYS --addr 0x2000

# Add text file using ProDOS type name
rdedisktool add disk.po ./README.TXT README --type TXT

# Using hex type code for any ProDOS file type
rdedisktool add disk.po ./DATA DATA --type 0x06 --addr 0x4000
rdedisktool add disk.po ./DATA DATA --type $06 --addr $4000

# DOS 3.3 codes also work on ProDOS disks (auto-converted)
rdedisktool add disk.po ./HELLO HELLO --type B --addr 0x0803
```

**Common Load Addresses:**
| Address | Typical Use |
|---------|-------------|
| $0801 | Applesoft BASIC programs |
| $0803 | Binary programs (after BASIC stub) |
| $2000 | Hi-res graphics page 1 |
| $4000 | Hi-res graphics page 2 |
| $6000 | Common program area |
| $9600 | RWTS buffer area |

> **Note**: When `--addr` is specified for binary files (type B), a 4-byte header (load address + length) is automatically prepended to the file data. If the file already contains a valid DOS 3.3 header, it will not be added again.

### Working with Subdirectories

Subdirectory operations are supported for file systems that support directories: **ProDOS**, **MSX-DOS**, and **Human68k**.

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

#### Human68k Subdirectory Example

```bash
# Create a new Human68k formatted disk
rdedisktool create mydisk.xdf -f xdf --fs human68k -n MYDISK

# Create a directory structure
rdedisktool mkdir mydisk.xdf GAMES
rdedisktool mkdir mydisk.xdf GAMES/ACTION

# Add files to subdirectories
rdedisktool add mydisk.xdf ./shooter.x GAMES/ACTION/SHOOTER.X

# List subdirectory contents
rdedisktool list mydisk.xdf GAMES
rdedisktool list mydisk.xdf GAMES/ACTION

# Extract file from subdirectory
rdedisktool extract mydisk.xdf GAMES/ACTION/SHOOTER.X

# Delete and cleanup
rdedisktool delete mydisk.xdf GAMES/ACTION/SHOOTER.X
rdedisktool rmdir mydisk.xdf GAMES/ACTION
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

#### DOS 3.3 File Types

| Code | Type | Description |
|------|------|-------------|
| 0x00 | T | Text file (sequential access) |
| 0x01 | I | Integer BASIC program |
| 0x02 | A | Applesoft BASIC program |
| 0x04 | B | Binary file (machine code) |
| 0x08 | S | S-type file (special system) |
| 0x10 | R | Relocatable object code |
| 0x20 | a | A-type file |
| 0x40 | b | B-type file |

> **Note**: Bit 7 (0x80) of the file type byte indicates a locked file.

#### DOS 3.3 Binary File Format

Binary files (type B), Applesoft (type A), and Integer BASIC (type I) files include a 4-byte header:

```
Offset  Size  Description
------  ----  -----------
0       2     Load address (little-endian)
2       2     File length (little-endian)
4       n     Actual program data
```

**Example**: A 59-byte program at $0803:
```
03 08        ; Load address: $0803
3B 00        ; Length: $003B (59 bytes)
[59 bytes of program data]
```

This header is automatically added when using `--addr` with the `add` command. DOS 3.3 uses this information when executing `BRUN` or `BLOAD` commands.

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
| Human68k | `0xE5` | First byte of filename | File entry marked as deleted |

These markers are normal and indicate previously deleted files. The disk space is available for reuse.

### Human68k File System Structure (X68000)

Human68k is the native operating system for Sharp X68000 computers, using a FAT12-based file system with X68000-specific characteristics.

**Disk Geometry (2HD):**
- 77 cylinders × 2 heads × 8 sectors = 1,232 sectors
- 1,024 bytes per sector (different from PC's 512 bytes)
- Total capacity: 1,261,568 bytes (~1.2MB)

**File System Layout:**
| Sector | Contents |
|--------|----------|
| 0 | Boot sector with BPB |
| 1-4 | FAT1 and FAT2 (2 sectors each) |
| 5-10 | Root directory (192 entries) |
| 11+ | Data area |

**Boot Sector BPB (BIOS Parameter Block):**
- Bytes/sector: 1024
- Sectors/cluster: 1
- Reserved sectors: 1
- Number of FATs: 2
- Root entries: 192
- Media descriptor: 0xFE (2HD)

**Directory Entry (32 bytes):**
| Offset | Size | Description |
|--------|------|-------------|
| 0x00 | 8 | Filename (space-padded) |
| 0x08 | 3 | Extension (space-padded) |
| 0x0B | 1 | Attributes |
| 0x0C | 10 | Reserved |
| 0x16 | 2 | Time (DOS format) |
| 0x18 | 2 | Date (DOS format) |
| 0x1A | 2 | Start cluster |
| 0x1C | 4 | File size |

**File Attributes:**
| Bit | Value | Description |
|-----|-------|-------------|
| 0 | 0x01 | Read-only |
| 1 | 0x02 | Hidden |
| 2 | 0x04 | System |
| 3 | 0x08 | Volume label |
| 4 | 0x10 | Directory |
| 5 | 0x20 | Archive |

**DIM File Format:**
DIM format includes a 256-byte header before the disk data:
| Offset | Size | Description |
|--------|------|-------------|
| 0x00 | 1 | Disk type (0=2HD, 1=2HS, 2=2HC, 3=2HDE, 9=2HQ) |
| 0x01 | 170 | Track existence flags (1=present, 0=absent) |
| 0xAB | 15 | Header info ("DIFC HEADER" signature) |
| 0xBA | 4 | Creation date |
| 0xBE | 4 | Creation time |
| 0xC2 | 61 | Comment |
| 0xFF | 1 | Overtrack flag |

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
