# RDE Disk Tool (Retro Developer Environment Disk Tool)

A cross-platform command-line tool for manipulating disk images used by retro computer emulators. Supports Apple II, MSX, X68000, and classic Macintosh (System 6/7) disk formats.

## Features

- **Multi-platform support**: Apple II, MSX, X68000, and classic Macintosh disk images
- **File operations**: List, extract, add, delete, and rename files
- **Subdirectory support**: Full subdirectory operations for ProDOS, MSX-DOS, Human68k, and HFS
- **Format conversion**: Convert between compatible disk formats (incl. `mac_img ↔ mac_dc42`)
- **XSA compression**: Compress/decompress MSX disk images (LZ77 + Huffman; an empty 720 KB disk becomes about 9 KB)
- **Macintosh forks**: AppleDouble v2 (`._<basename>` sidecar) and MacBinary v1 import (`add`) and export (`extract`) keep resource forks + Finder info
- **Disk creation**: Create new formatted disk images (incl. empty 800K / 1440K HFS and 400K MFS volumes)
- **Apple II nibble / bit images**: NIB, NB2 and WOZ read and write (DOS 3.3 RWTS-compatible encoding; WOZ 2.1 FLUX tracks read-only); 13-sector DOS 3.2 disks: `.d13` and 13-sector NIB/NB2/WOZ read, write, create (`--fs dos32`) and convert
- **Apple II 3.5" 800K ProDOS**: 1600-block `.po` (`800po`) and `.2mg` (`800mg`) images — read, write, create, convert between the two
- **Boot disk protection**: Multi-condition policy guards System / Finder files on bootable Macintosh / Apple II / MSX / X68000 disks
- **Validation**: Verify disk image integrity (incl. DC42 ROR32+BE16 checksum)
- **Repair**: `repair` corrects what older rdedisktool versions wrote wrong (DOS 3.3/3.2 VTOC bitmap, Human68k BPB size)
- **Sector dump**: Raw sector/track data inspection
- **Raw sector I/O**: `putraw` / `getraw` write and read fixed track/sector ranges on Apple II `.do` images for direct-boot (no-DOS) disk assembly, guarded by the DKFS build marker

## Supported Formats

### Apple II
| Format | Extension | Description |
|--------|-----------|-------------|
| DOS Order | .do, .dsk | Standard DOS 3.3 sector order |
| ProDOS Order | .po | ProDOS sector order |
| Nibble | .nib | Raw nibblized format (6656 bytes/track) |
| Nibble 6384 | .nb2 | Same as `.nib` with 6384 bytes/track |
| WOZ | .woz | WOZ v1/v2 bitstream format (reads both; new images are always written as WOZ2) |
| DOS 3.2 (13-sector) | .d13 | 35 × 13 × 256 sector image in physical order (116,480 bytes) |
| ProDOS 800K (`800po`) | .po | 3.5" disk: 1600 × 512-byte blocks in block order (819,200 bytes) |
| ProDOS 800K 2MG (`800mg`) | .2mg | The same 1600 blocks after a 64-byte 2MG header |

Sector numbers: `.do`/`.dsk`/`.nib`/`.nb2`/`.woz` use DOS 3.3 logical sector numbers, `.po` uses
ProDOS logical sector numbers, 13-sector disks (`.d13` and 13-sector NIB/WOZ) use physical
sector numbers. `convert` moves every sector to the same physical sector.

Writing a sector to a `.nib`/`.nb2`/`.woz` image works like DOS 3.3 RWTS: rdedisktool finds the
sector's address field and replaces only its data field (`D5 AA AD` … `DE AA`). Everything else
on the track stays as it was — gaps, other sectors (also ones that cannot be read), extra marks a
copy protection put there and weak bits (runs of zero bits). Only the sector being written must be
readable. A data field holding timing bits is written back as plain 8-bit nibbles (the WOZ track
gets that much shorter; a WOZ1 splice point after it moves along). Weak bits are not simulated:
a sector that reads through them is unreadable, as its checksum fails.

WOZ 2.1 FLUX tracks are read: each flux interval (125 ns ticks, `255` adding to the next byte)
becomes `n` bit cells, `n` = ticks / the INFO optimal bit timing (32 = 4 µs) rounded half up -
`n-1` zero bits and a one - and the sectors are read from those bits like any other track.
They are never written: `add`/`delete`/`rename` on a WOZ with FLUX tracks is refused and the file
left unchanged (convert it to `.do`/`.po`/`.nib`/`.woz` first). Checked on the Applesauce sample
with every other track as FLUX: all sectors read, and the disk converted to `.po` boots in an
emulator.

13-sector (DOS 3.2) disks: `.nib`/`.nb2`/`.woz` tracks with `D5 AA B5` address fields
(5-and-3 data) and `.d13` images are detected automatically, and `info`, `list`, `extract`,
`validate` work (file system "DOS 3.2"), and so do `add`/`delete`/`rename`.
The DOS 3.2 free-sector bitmap keeps a track's 13 bits as a big-endian word in bytes 0-1,
sector s = bit s+3 (measured on the Apple DOS 3.1 / 3.2 / 3.2.1 masters). On NIB/NB2/WOZ a
sector write replaces only its data field with 5-and-3 nibbles, like DOS 3.2 RWTS (same rules
as the 16-sector case above). DOS 3.2 INIT writes address fields only, so the free sectors of
a real DOS 3.2 disk often have no data field yet (Asimov's Utility/Plus masters: noise there);
such a sector gets its data field after the address field as DOS 3.2 writes it (14 syncs,
`D5 AA AD` .. `DE AA EB`; in a NIB down to 5 syncs when the gap is short) - with no room
before the next address field the write is refused and the image left unchanged.
`create x.d13 -f d13 --fs dos32` makes a DOS 3.2 volume byte-identical to what real DOS 3.2
INIT writes (VTOC, catalog 17/12..17/1, tracks 0-2 and 17 in use) except the DOS image itself
(tracks 0-2 stay empty: not bootable). 13-sector disks convert to `.d13`, `nib`, `nb2` and
`woz`; NIB/WOZ tracks are laid out like a real DOS 3.2 disk (measured on an Applesauce capture
of the DOS 3.2 System Master: 9-bit syncs, physical order 0,10,7,4,1,11,8,5,2,12,9,6,3 - that
capture converted to `.d13` and back is bit-identical). `.d13` only holds 13-sector disks.
Checked in an emulator: real DOS 3.2 reads files added this way to `.d13`, NIB and WOZ images
(also into never-written sectors and on a created volume), writes next to them, and boots from
a master converted from `.d13` to WOZ.

**800K (3.5") images** hold ProDOS only and are a different medium from both the 5.25"
images and Macintosh 800K disks:
- They are recognised **only by name and size**: `.po` of exactly 819,200 bytes is `800po`
  (any other `.po` is the 140K layout), `.2mg` with a `2IMG` header is `800mg`. A raw
  819,200-byte `.img`/`.dsk` is never opened as Apple II (ProDOS block 2 and the Macintosh
  MDB share offset `$400`).
- Sector commands (`dump`) address block *b* as track `b / 20`, side `(b % 20) / 10`,
  sector `b % 10` (80 × 2 × 10 × 512, numbered from 0). This is only a numbering of the
  blocks, not the physical 3.5" GCR layout.
- 2MG header (layout as read by MAME and AppleWin): only format 1 (ProDOS order) with 1600
  blocks is accepted; DOS order, nibble data, other block counts, a header size other than
  64, version > 1 or ranges outside the file are refused with the reason. Data length 0
  means 1600 × 512. Changing files rewrites only the data range, so the header, comment,
  creator data and any trailing bytes stay byte-identical. Flags bit 31 (locked) makes the
  image write-protected. New `.2mg` files get creator `RDET`. The byte-reversed signature
  `GMI2` (written by Bernie ][ The Rescue; MAME reads it too) is accepted and kept.

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

### Macintosh
| Format | Extension | Description |
|--------|-----------|-------------|
| Raw Image | .img, .dsk | Raw 512-byte-sector stream (400K / 720K / 800K / 1.44M) |
| Apple Disk Copy 4.2 | .image, .dc42 | 0x54-byte header + raw payload + optional tag bytes, validated by data/tag ROR32+BE16 checksum |
| Applesauce MOOF | .moof | Bitstream image (GCR 400K/800K, MFM 1.44M); files inside are **read-only** — convert to `mac_img` to modify |

> **Note**: Both HFS and MFS are detected automatically by the MDB signature at sector 2. `mac_img`, `mac_dc42` and `mac_moof` convert to each other in every direction. `mac_dc42` cannot be created from scratch — make a `mac_img` first, then convert.

## Supported File Systems

| File System | Platform | Subdirectories | Notes |
|-------------|----------|----------------|-------|
| DOS 3.3 | Apple II | No | VTOC-based allocation, 140KB max |
| DOS 3.2 | Apple II | No | 13 sectors/track; `.d13` and 13-sector NIB/NB2/WOZ read/write (`add`/`delete`/`rename`); `create` on `.d13` (`--fs dos32`) |
| ProDOS | Apple II | Yes | Block-based allocation; 140 KB 5.25" images (280 blocks) and 800 KB 3.5" images (1600 blocks) |
| MSX-DOS | MSX | Yes | FAT12, MSX-DOS 1/2 compatible |
| Human68k | X68000 | Yes | FAT12-based, 1024-byte sectors, 8.3 filenames |
| HFS | Macintosh | Yes | Hierarchical File System: catalog B-tree (auto leaf-split), extents overflow read, 800K / 1440K format, mkdir/rmdir/rename incl. resource-fork preservation |
| MFS | Macintosh | No | Flat directory + 12-bit allocation map; full read/write/format on 400K floppies (`create` cannot make 800K MFS — 792 allocation blocks exceed the 640-entry map) |

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
| `-DCMAKE_BUILD_TYPE=Release` | Release build with optimizations (default when not set) |
| `-DCMAKE_BUILD_TYPE=Debug` | Debug build with symbols |
| `-DCMAKE_INSTALL_PREFIX=<path>` | Custom installation prefix |
| `-DBUILD_TESTS=ON` | Register every `tests/test_*.sh` as a CTest test (run `ctest` in the build directory) |

The tests are shell scripts in `tests/` and can also be run directly (they use
`build/rdedisktool`, or `RDEDISKTOOL=<path>`); see `HOWTO_COMPILE.md`.

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
| `--bootdisk-profile <dos33|prodos|msxdos|human68k|macintosh|unknown>` | Force bootdisk profile for detection |
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

A disk counts as a boot disk when **any** of these holds:
- its root directory has one of the profile's system files — DOS 3.3: `INTBASIC`, `FPBASIC`,
  `MASTER`, `BOOT13`; ProDOS: `PRODOS`, `BASIC.SYSTEM`, `QUIT.SYSTEM`; MSX-DOS: `MSXDOS2.SYS`,
  `COMMAND2.COM`; Human68k: `HUMAN.SYS`, `COMMAND.X`, `CONFIG.SYS`, `AUTOEXEC.BAT`;
  Macintosh: `LK` boot block **and** both `System` and `Finder` (root or `System Folder`)
- the image path contains a `/bootdisk/` directory
- `--bootdisk-profile` names a profile other than `unknown`

Bootdisk mode behavior matrix:

| Mode | `add` | `delete` / `mkdir` / `rmdir` / `rename` |
|------|-------|------------------------------------------|
| `strict` (default) | safe-add verification (boot sectors guarded) | **blocked** — requires `--force-bootdisk` per call |
| `warn` | safe-add verification (boot sectors still guarded) | allowed with a stderr warning per call |
| `off` | unrestricted | unrestricted |

`safe-add` invariants (run in `strict` and `warn`):
- protected boot sectors unchanged after the write
- existing files unchanged (recursive, including subdirectories)

**Critical system files** (e.g. `System` / `Finder` / `COMMAND2.COM` / `HUMAN.SYS`) still require `[y/N]` confirmation on `delete` regardless of mode. Use `--force-system-file` to bypass the prompt intentionally. (`rmdir` / `rename` of critical files are NOT prompted today — opting into `warn` mode allows those without confirmation. Future PR may extend the gate.)

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
rdedisktool list <image_file> [path] [-v]
```

`-v` adds Macintosh file type, creator and Finder-flag columns (`MacTy Creat FFlg`) on
Macintosh disks; it changes nothing on other platforms.

Examples:
```bash
# List root directory
rdedisktool list mydisk.dsk

# List subdirectory
rdedisktool list mydisk.dsk GAMES

# List nested subdirectory
rdedisktool list mydisk.dsk GAMES/RPG
```

Output (MSX-DOS disk; Attr shows FAT attributes `R`/`H`/`S`):
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

On Apple II disks the Type column shows the file type as the disk's own catalog does —
DOS 3.3/3.2 `T` `I` `A` `B` `S` `R` (`a` / `b` for types `$20` / `$40`, which DOS itself
shows as `A` / `B`), ProDOS `TXT` `BIN` `SYS` `BAS` … or `$xx` for unnamed types, `DIR` for
directories — and Attr shows `L` for a locked file (DOS: type bit 7; ProDOS: write bit off,
as `CAT` marks it `*`).

#### extract - Extract files from disk image
```bash
rdedisktool extract [--raw] <image_file> <file> [output_path]
rdedisktool extract <image_file> <file> --apple-double|--macbinary <output_path>   # Macintosh
```

On DOS 3.3 disks the output is the file body: B files without their 4-byte
address/length header, A/I files without their 2-byte length, T files up to the
first `$00`, other types as all data sectors. `--raw` writes the file exactly as
stored (headers and whole sectors).

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
| `-a, --addr <addr>` | Load address for binary files (hex: `0x0803` or `'$0803'` — quote `$` values in the shell) |
| `--raw` | DOS 3.3: the host file already holds the DOS file bytes (B/A/I header included) |

**DOS 3.3 free-sector bitmap:** the standard VTOC layout is used (track entry byte 0
bit k = sector 8+k, byte 1 bit k = sector k). Versions before 2026-10 reversed the
bits inside each byte, so on a partially used track they could hand out sectors in
use, and real DOS could later overwrite their files. Before an add, sectors in use by
the catalog or a file that the bitmap shows free are marked used, with a warning
("N sector(s) in use were marked free ... marked them used"); `validate` reports them.

**DOS 3.3 disks:** the host file is the file body. B files get the DOS header
(load address, length) — without `--addr` the address is `$2000` and a warning is
printed; A/I files get the 2-byte length. Without `--type` the file is added as B.
Text (T) files are stored as given; DOS reads a sequential text file only up to its
first `$00`.

The `--type` option accepts three formats:
- **DOS 3.3 single-character codes**: `T`, `I`, `A`, `B`, `S`, `R`
- **ProDOS type names**: `SYS`, `BIN`, `TXT`, `BAS`, `CMD`, `INT`, `REL`
- **Hex values**: `0xFF` or `$FF` — the type code of the target file system (ProDOS: any
  code, e.g. `$04` = TXT; DOS 3.3: `$00 $01 $02 $04 $08 $10 $20 $40`)

On DOS 3.3 disks the ProDOS names `TXT`, `INT`, `BAS`, `BIN`, `REL` map to `T`, `I`, `A`,
`B`, `R`; `SYS` and `CMD` are rejected.

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
>
> `S` has no ProDOS equivalent and is rejected on ProDOS disks.

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
rdedisktool add disk.do ./PICTURE.BIN MYPIC -t B -a 0x4000

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

#### mkdir - Create directory (ProDOS, MSX-DOS, Human68k, HFS)
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

#### rmdir - Remove directory (ProDOS, MSX-DOS, Human68k, HFS)
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

#### rename - Rename file or directory

```bash
rdedisktool rename <image_file> <old_name> <new_name>
```

Examples:
```bash
# Rename a file
rdedisktool rename disk.po OLD.TXT NEW.TXT

# Rename a file in a subdirectory
rdedisktool rename disk.po DIR1/FILE.BIN DIR1/NEWFILE.BIN

# Rename a directory
rdedisktool rename disk.dsk MYDIR NEWDIR
```

> **Note**: Renames within the same directory only. Cross-directory move is not supported. ProDOS directory renames also update the subdirectory header to keep names consistent.

#### create - Create new disk image
```bash
rdedisktool create <file> -f <format> [--fs <filesystem>] [-n <volume>] [-g <geometry>] [--force]
```

| Option | Description |
|--------|-------------|
| `-f, --format <fmt>` | Disk format (required if not detectable from extension) |
| `--fs, --filesystem <fs>` | Initialize with filesystem: dos33, prodos, msxdos, fat12, human68k, hfs, mfs |
| `-n, --volume <name>` | Volume name (optional, ignored for DOS 3.3; ProDOS: 1-15 of A-Z, 0-9, `.`, starting with a letter — lower case is stored as upper case, anything else is refused; default `BLANK`) |
| `-g, --geometry <spec>` | Custom geometry: tracks:sides:sectors:bytes |
| `--force` | Overwrite existing file |

**Supported disk formats:**
| Platform | Formats |
|----------|---------|
| Apple II | do, po, nib, nb2, woz (`woz1` is written as WOZ2 with a warning), d13 (`--fs dos32`, or blank), 800po (`*.po` only), 800mg (`*.2mg` only) |
| MSX | msxdsk, dmk |
| X68000 | xdf, dim |
| Macintosh | mac_img, mac_moof (`mac_dc42` cannot be created — convert from `mac_img`) |

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

# Create Macintosh HFS volume (1440K default)
rdedisktool create mac.img -f mac_img --fs hfs -n MyVolume

# Create Macintosh HFS volume (800K)
rdedisktool create mac.img -f mac_img --fs hfs -n V -g 80:2:10:512

# Create Macintosh MFS volume (400K floppy)
rdedisktool create mfs.img -f mac_img --fs mfs -n V -g 80:1:10:512

# Create Apple II 3.5" 800K ProDOS volume (raw .po or .2mg; ProDOS only)
rdedisktool create big.po  -f 800po --fs prodos -n BIGDISK
rdedisktool create big.2mg -f 800mg --fs prodos -n BIGDISK

# 5.25" Apple II images are always 35 tracks x 1 side x 16 sectors x 256 bytes (DO, NIB,
# NB2 and WOZ also 13 sectors for DOS 3.2), 800K images 80:2:10:512; any other -g is
# refused before a file is written.

# DOS 3.2 volume (13 sectors; no DOS image on tracks 0-2), then as a NIB or WOZ
rdedisktool create dos32.d13 -f d13 --fs dos32
rdedisktool convert dos32.d13 dos32.woz -f woz

# Create blank disk (no filesystem)
rdedisktool create blank.po -f po
```

> **Note**: Created Apple II / MSX / X68000 disks are not bootable (no boot code). Macintosh HFS volumes carry a halt-loop boot block scaffold; they become runtime-bootable only after real `System` and `Finder` files are added.

> **Note**: `mac_dc42` cannot be created from scratch — make a `mac_img` first, then `convert mac.img mac.image -f mac_dc42`. MFS 800K is in-the-wild but exceeds the 12-bit allocation map; use an emulator or `hfsutils` for that geometry.

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
- Macintosh HFS: `File System: HFS`
- Macintosh MFS: `File System: MFS`

#### convert - Convert disk image format
```bash
rdedisktool convert <input_file> <output_file> [-f <format>]
```

| Option | Description |
|--------|-------------|
| `-f, --format <fmt>` | Output format (auto-detected from extension if not specified). Any case; the names of `create` plus the aliases `dsk`, `dos`, `prodos`, `nibble`, `nibble2`. An unknown value is an error |

Apple II 800K: `800po` and `800mg` convert only to each other (`.po` ⇄ `.2mg`, data
byte-identical); 5.25" and Macintosh formats are refused. A `.po` output name with an 800K
input means `800po`. The output must be named `*.po` for `800po` and `*.2mg` for `800mg`
(also for `create`) — other names would not be opened as that format again. A new `.2mg`
gets a fresh header; when the input `.2mg` has a comment, creator data or the lock flag,
a warning says they are not carried over.

Apple II: sectors are moved by physical position between DOS-order (`.do`/`.nib`/`.nb2`/`.woz`)
and ProDOS-order (`.po`) images. 13-sector disks convert to `.d13`, `nib`, `nb2` or `woz`
(DOS 3.2 tracks), and `.d13` only accepts 13-sector disks. If a sector cannot be read (for example an
unformatted WOZ track), the image is still written, every missing sector is listed
as a warning, and the exit code is **2**. Sectors are read with the same checks as
DOS 3.3 RWTS (address/data epilogue `DE AA`, checksums); a sector whose address bytes
miss a fixed 4-and-4 bit is still read (as DOS does) and reported as a warning.

On every platform, a sector that cannot be copied is reported as a warning and the
exit code is **2** (the output image is still written).

Output files (`convert`, `create`, `extract`): an output that is the input image itself is
refused; a new image whose extension belongs to another format (e.g. `-f po` written as
`*.do`, which would be read in DOS order) is refused — `.dsk`, `.img`, no extension and
extensions no format uses are allowed; an existing file is overwritten with a warning
(`Overwriting existing file: …`).

Examples:
```bash
# Convert Apple II DOS to ProDOS order
rdedisktool convert game.do game.po -f po

# Wrap an Apple II 800K .po in a 2MG container, and back
rdedisktool convert big.po big.2mg
rdedisktool convert big.2mg big.po

# Compress MSX DSK to XSA (format auto-detected from extension)
rdedisktool convert game.dsk game.xsa

# Decompress XSA to DSK
rdedisktool convert game.xsa game.dsk -f msxdsk

# Convert between MSX formats
rdedisktool convert game.dsk game.dmk -f dmk

# Wrap a raw Macintosh image with a DC42 header
rdedisktool convert mac.img mac.image -f mac_dc42

# Strip a DC42 header to recover the raw image
rdedisktool convert mac.image mac.img -f mac_img
```

**Supported format conversions** (within one platform only):
| Platform | Formats | Notes |
|----------|---------|-------|
| Apple II | do, po, nib, nb2, woz | Any direction; sectors keep their physical position |
| Apple II | 13-sector nib/nb2/woz/d13 → d13, nib, nb2, woz | DOS 3.2 disks; NIB/WOZ get real DOS 3.2 track layout |
| Apple II | 800po, 800mg | 3.5" 800K only, either direction; no other target |
| MSX | msxdsk, dmk, xsa | Any direction; XSA output is compressed |
| X68000 | xdf, dim | 2HD both ways, byte for byte |
| Macintosh | mac_img, mac_dc42, mac_moof | Any direction; `mac_dc42` output gets a fresh header and checksums |

#### list-formats - List registered disk image formats
```bash
rdedisktool list-formats
```
Prints every format name with its extensions and display name (tab-separated).

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
- Sector/block allocation verification (DOS 3.3: every sector used by the catalog or a
  file must be marked used in the VTOC bitmap; ProDOS: every block of every directory
  file and every index, master and data block must be marked used, a block marked used
  that nothing references is a warning, a block referenced twice is a warning)
- Chain loops: a ProDOS directory or DOS 3.3 catalog / track-sector list whose link leads
  back to a block or sector already read is an error (every command stops with
  "... chain loops back to ..." instead of running forever)
- Boot block integrity (ProDOS)
- Human68k: the BPB total sector count must fit the image

#### repair - Correct what older rdedisktool versions wrote wrong
```bash
rdedisktool repair <image_file> [--dry-run]
```

`add`/`delete`/`rename` correct these on the way; `repair` does it without changing files:
- DOS 3.3 / DOS 3.2: sectors the VTOC, the catalog or a file uses that the VTOC bitmap shows
  as free (older versions reversed the bit order of a bitmap byte, so a new file could
  overwrite them) are marked used. Sectors marked used that nothing refers to are only
  reported - they may hold a DOS image or data outside the catalog.
- Human68k: a BPB total sector count larger than the image (older `create` wrote 2,464 on a
  1,232-sector disk) is set to the image size.

Only those bytes change. A damaged catalog or track/sector list (a loop, a pointer off the
disk) is not touched: exit code 1, image unchanged - see `validate`. `--dry-run` only
reports; a disk with nothing to repair is left alone. On a boot disk the repair is blocked
in strict mode and allowed with a warning in `--bootdisk-mode warn` (like `rename`).

```bash
rdedisktool repair --dry-run old.dsk
rdedisktool repair old.xdf
```

The exit code is non-zero when errors are found.

#### dump - Dump sector/track data
```bash
rdedisktool dump <image_file> -t <track> -s <sector> [--side <n>] [-f <format>]
```

| Option | Description |
|--------|-------------|
| `-t, --track <n>` | Track number (0-based, required) |
| `-s, --sector <n>` | Sector number (required; 0-based, X68000 1-8) |
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

#### putraw - Write a raw host file to fixed track/sector (Apple II direct-boot)
```bash
rdedisktool putraw <image_file> <hostfile> -t <track> -s <sector> [--max-sectors <n>] [-f do]
```

| Option | Description |
|--------|-------------|
| `-t, --track <n>` | Start track (0-based, required) |
| `-s, --sector <n>` | Start sector (0-based, required) |
| `--max-sectors <n>` | Reject if the file would span more than `<n>` sectors |
| `-f, --format do` | Format hint (only `do` accepted; autodetect is authoritative) |

Writes a host file verbatim to consecutive logical sectors starting at `(track, sector)`, advancing sector-then-track; the final partial sector is zero-padded to 256 bytes. Intended for laying down `boot0` / `stage2` / RWTS / payload on an **Apple II direct-boot** (no-DOS, raw-sector) disk.

Restrictions (fixed, by design):
- `.do` extension + AppleDO format + exactly `35/1/16/256` geometry only.
- **Write guard**: a disk with a recognized filesystem (DOS 3.3 / ProDOS) is refused unless it carries the DKFS build marker (`DKFS20RAW` at track 0 sector 15) or the global `--force-bootdisk` flag is given. Blank / unrecognized `.do` images are accepted.
- All-or-nothing: a partial/failed write never touches the on-disk image.
- Empty (0-byte) host files and out-of-range / non-fitting writes are rejected.

Examples:
```bash
rdedisktool putraw boot.do boot0.bin   -t 0 -s 0
rdedisktool putraw boot.do payload.bin -t 1 -s 0 --max-sectors 32
rdedisktool --force-bootdisk putraw boot.do marker.bin -t 0 -s 15
```

#### getraw - Read raw sectors to a host file (Apple II direct-boot)
```bash
rdedisktool getraw <image_file> -o <out_file> -t <track> -s <sector> --count <n> [--force]
```

| Option | Description |
|--------|-------------|
| `-o, --output <file>` | Output host file (required) |
| `-t, --track <n>` | Start track (0-based, required) |
| `-s, --sector <n>` | Start sector (0-based, required) |
| `--count <n>` | Number of sectors to read (required, > 0) |
| `--force` | Overwrite `<file>` if it already exists |
| `-f, --format do` | Format hint (only `do` accepted; autodetect is authoritative) |

Mirror of `putraw`: reads `<count>` consecutive logical sectors from `(track, sector)` and writes them to `<file>`. Output is exactly `count*256` bytes (the trailing sector is not truncated). `.do`/AppleDO/`35/1/16/256` only; `-o` must not be the input image and an existing `-o` needs `--force`; written via a temp file + atomic rename so a failed read leaves no partial output.

Examples:
```bash
rdedisktool getraw boot.do -o boot0.out -t 0 -s 0 --count 1
rdedisktool getraw boot.do -o dump.bin  -t 1 -s 0 --count 32 --force
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
- Last recorded run: 2026-02-24, all four passed (`TESTCASE.md` scenario 12).

Apple II NIB/NB2/WOZ images can also be checked against real DOS 3.3 / ProDOS in an isolated
AppleWin (sa2) — see `tests/emu/README.md`.

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

> **Geometry**: a 2HD disk is 77 cylinders x 2 heads x 8 sectors (numbered 1-8) x 1024 bytes
> (1,232 sectors). `dump -t` takes the cylinder and `--side` the head. Disks formatted by
> rdedisktool before 2026-10 have a BPB that claims 2,464 sectors: rdedisktool warns, uses the
> real size, and writes the correct count into the BPB on the next change (`add`, `delete`, …).

### Working with XSA Compressed Disks

XSA is a compressed disk image format that significantly reduces file size while maintaining full compatibility. **XSA images are read-only** - you can view and extract files, but cannot modify them directly.

```bash
# View XSA disk information
rdedisktool info game.xsa

# List files in XSA disk
rdedisktool list game.xsa

# Extract a file from XSA disk
rdedisktool extract game.xsa GAME.COM ./game.com

# Compress DSK to XSA
rdedisktool convert game.dsk game.xsa

# Compress DMK to XSA
rdedisktool convert game.dmk game.xsa

# Decompress XSA to DSK
rdedisktool convert game.xsa game.dsk -f msxdsk

# Decompress XSA to DMK format
rdedisktool convert game.xsa game.dmk -f dmk
```

> **Modifying XSA contents**: To modify files in an XSA image, first decompress to DSK or DMK, make your changes, then re-compress to XSA.

**Measured examples** (rdedisktool 2026-10):
| Original | XSA size |
|----------|----------|
| Empty 720 KB MSX-DOS DSK | 8,810 bytes |
| Same disk with one 3,000-byte random file | 12,159 bytes |

> **Note**: Compression depends on disk content. Empty or repetitive data compresses extremely well; disks full of programs or compressed data shrink far less.

### Working with Apple II Disks

```bash
# Get disk information
rdedisktool info appleii.do

# List files
rdedisktool list appleii.do

# Extract Applesoft BASIC program
rdedisktool extract appleii.do HELLO hello.bas

# Add binary file (default type B; load address $2000 unless --addr is given)
rdedisktool add appleii.do ./newprog.bin NEWPROG
```

#### Adding DOS 3.3 Binary Files with Load Address

DOS 3.3 binary files require a load address to execute properly with `BRUN`. The `--type` and `--addr` options allow you to specify this metadata:

```bash
# Add binary file with load address $0803 (standard for most programs)
rdedisktool add disk.do ./HELLO.BIN HELLO --type B --addr 0x0803

# Add binary file at $4000 (common for hi-res graphics)
rdedisktool add disk.do ./PICTURE.BIN MYPIC -t B -a 0x4000

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
rdedisktool add disk.po ./DATA DATA --type '$06' --addr '$4000'   # quote $ in the shell

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

> Under 48K DOS 3.3, HIMEM is `$9600` (DOS and its buffers sit above it), so do not load
> programs at `$9600` or higher.

> **Note**: On DOS 3.3 disks the host file is always treated as the file body: B files get the
> 4-byte header (load address + length) and A/I files the 2-byte length, even if the host file
> already starts with such a header. To store a file that already holds the DOS bytes, use `--raw`.

### Working with Subdirectories

Subdirectory operations are supported for file systems that support directories: **ProDOS**, **MSX-DOS**, **Human68k**, and **HFS**.

> **Note**: DOS 3.3 and MFS do not support subdirectories.

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

#### HFS Subdirectory Example

```bash
# Create a new 1440K HFS volume
rdedisktool create mac.img -f mac_img --fs hfs -n MyVolume

# Nested mkdir — HFS catalog B-tree auto-splits as needed
rdedisktool mkdir mac.img "Documents"
rdedisktool mkdir mac.img "Documents/Reports"

# Add files into nested folders
rdedisktool add mac.img ./readme.txt "Documents/README"
rdedisktool add mac.img ./report.txt "Documents/Reports/Q1"

# Rename a folder (children stay attached — CNID is preserved)
rdedisktool rename mac.img "Documents" "Archive"

# rmdir requires the folder to be empty (POSIX semantics)
rdedisktool delete mac.img "Archive/Reports/Q1"
rdedisktool rmdir  mac.img "Archive/Reports"
```

> **Note**: HFS volume names allow spaces and any MacRoman character. Quote them in the shell.

### Working with Macintosh Disks

```bash
# Create a 1440K HFS volume (default geometry)
rdedisktool create mac.img -f mac_img --fs hfs -n MyVolume

# Create an 800K HFS volume
rdedisktool create mac800.img -f mac_img --fs hfs -n V -g 80:2:10:512

# Create a 400K MFS volume (single-sided floppy)
rdedisktool create mfs.img -f mac_img --fs mfs -n V -g 80:1:10:512

# Get info, including DC42 / HFS / MFS detection
rdedisktool info mac.img

# List the volume root
rdedisktool list mac.img

# List a subdirectory (HFS)
rdedisktool list mac.img "System Folder"

# Add / extract a data-fork-only file
rdedisktool add     mac.img ./hello.txt "Hello.txt"
rdedisktool extract mac.img "Hello.txt" ./out.txt

# Extract a file with its resource fork preserved (AppleDouble v2 sidecar).
# The output path must be a FILE path; the ._<basename> sidecar is created
# next to it in the same directory.
rdedisktool extract mac.img "TeachText" --apple-double ./TeachText
# (produces ./TeachText + ./._TeachText)

# Extract as MacBinary v1 (single .bin with both forks + Finder info)
rdedisktool extract mac.img "TeachText" --macbinary ./TeachText.bin

# Convert containers
rdedisktool convert mac.image mac.img  -f mac_img    # DC42 → raw
rdedisktool convert mac.img   mac.dc42 -f mac_dc42   # raw → DC42 (re-checksums)
```

> **Resource forks**: `extract` without `--apple-double` / `--macbinary` writes only the data fork. Mac applications and most resource-bearing files require one of those flags to round-trip correctly.

> **Boot disks**: HFS volumes created by `rdedisktool` carry a halt-loop boot block scaffold (LK signature + standard Pascal name fields) but are NOT runtime-bootable until you copy real `System` and `Finder` files into the root. Once both files exist, the boot disk policy treats the volume as bootable and guards the system files against accidental mutation.

## Technical Details

### MSX-DOS FAT12 Structure
- Boot sector with BPB (BIOS Parameter Block)
- Two FAT tables (FAT1 and FAT2)
- Root directory (112 entries for 720KB disk)
- Data clusters (2 sectors per cluster)
- Subdirectory support with `.` and `..` entries
- 8.3 filename format (8 characters name + 3 characters extension)

### Apple DOS 3.3 Structure
- Tracks 0-2: DOS image on a bootable disk (`create --fs dos33` writes no DOS; it keeps track 0
  and track 17 for the system and gives tracks 1-2 to files)
- Track 17, Sector 0: VTOC (Volume Table of Contents)
- Track 17, Sectors 15-1: Catalog (directory)
- Each file has a Track/Sector list
- VTOC free-sector bitmap: 4 bytes per track from offset `$38`; byte 0 bit k = sector 8+k,
  byte 1 bit k = sector k (1 = free)

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

Binary files (type B) start with a 4-byte header; Applesoft (type A) and Integer BASIC
(type I) files start with a 2-byte length only:

```
B:  Offset 0  2 bytes  Load address (little-endian)
    Offset 2  2 bytes  File length (little-endian)
    Offset 4  n bytes  Program data
A/I: Offset 0 2 bytes  Program length (little-endian)
     Offset 2 n bytes  Program
```

**Example**: A 59-byte program at $0803:
```
03 08        ; Load address: $0803
3B 00        ; Length: $003B (59 bytes)
[59 bytes of program data]
```

`add` writes this header (address `$2000` with a warning when `--addr` is not given);
`extract` removes it unless `--raw` is used. DOS 3.3 uses it for `BLOAD` / `BRUN`.

### Apple ProDOS Structure
- Block-based (512 bytes per block; 280 blocks on a 140KB disk, 1600 on an 800KB disk)
- Blocks 0-1: Boot blocks (boot-disk `add` guards exactly these 1024 bytes: 256-byte
  sectors 0-3 on 5.25" images, 512-byte sectors 0-1 on 800K)
- Blocks 2-5: Volume directory (key block 2)
- Block 6: Volume bitmap (one block per 4096 blocks; bit = 1 means free)
- Subdirectory support with linked directory blocks. A subdirectory header's
  `parent_pointer` is the directory block that holds its entry and
  `parent_entry_number` is that entry's slot in the block + 1 (the header is entry 1);
  `mkdir` writes the header and the entry as ProDOS 2.4.3 `CREATE` does: header version
  `$24`, access `$C3`, bytes `$14-$1B` = `$75, $24, $00, $C3, $27, $0D, $00, $00`; entry
  version `$24`, access `$E3` — measured in MAME, real ProDOS refuses (`I/O ERROR`) to add
  entries to a subdirectory whose header lacks them, and with a wrong parent entry it
  updates some other file's entry when the directory grows
- Three storage types for files:
  - **Seedling**: Files ≤ 512 bytes (1 data block)
  - **Sapling**: Files ≤ 128KB (1 index block + up to 256 data blocks)
  - **Tree**: Files ≤ 16MB (1 master index + 256 index blocks)
  - Index blocks hold pointer low bytes in bytes 0-255 and high bytes in 256-511;
    a zero pointer is an unallocated (sparse) block and reads as 512 zero bytes

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

**Length Encoding** (as decoded by `XSAExtractor::rdStrLen`; `x` = value bits):
| Bits | Length |
|------|--------|
| `0` | 2 |
| `10` | 3 |
| `110` | 4 |
| `111 0 xx` | 5-8 |
| `1111 0 xxx` | 9-16 |
| `11111 0 xxxx` | 17-32 |
| `111111 0 xxxxx` | 33-64 |
| `1111111 0 xxxxxx` | 65-128 |
| `11111111 xxxxxxx` | 129-254, 255 = end marker (`111111111111110`) |

**Supported Operations:**
- Read: Full support (automatic decompression on load)
- Write: **Read-only** (XSA images cannot be modified directly)
- Convert: Bi-directional conversion with DSK and DMK formats
- File operations: List and extract only (add/delete/modify not supported)

### Macintosh Disk Containers

**Raw Image (`mac_img`)**:
- A flat 512-byte-sector stream — same byte layout the Mac ROM sees.
- Auto-detected by the size + the HFS / MFS signature at sector 2 (offset 0x400).
- Default `create` geometry: 80 × 2 × 18 × 512 = 1440K. Use `-g` for other sizes.

**Apple Disk Copy 4.2 (`mac_dc42`)**:
- 0x54-byte header followed by raw payload + optional tag bytes.
- Header carries volume name (Pascal Str63), data size, tag size, and two
  ROR32+BE16 checksums (data fork + tag bytes).
- Bidirectional conversion with `mac_img` via the `convert` command.
  `mac_dc42` cannot be created from scratch.

**Applesauce MOOF (`mac_moof`)**:
- Bitstream / flux Macintosh floppy image — the format Applesauce hardware
  emits and the snow emulator reads/writes. GCR (400K single-sided / 800K
  double-sided) and MFM (1.44M IBM PC standard) tracks are decoded on load and
  encoded on `convert` / `create`. Flux tracks (FLUX chunk) are not decoded —
  pure bitstream MOOFs only.
- A loaded MOOF is **write-protected**: `add` / `delete` / `rename` / `mkdir` are refused.
  Convert to `mac_img`, modify, and convert back.
- Auto-detected by the 8-byte magic `MOOF\xff\x0a\x0d\x0a`; the CRC32 (ISO-HDLC) over the
  chunk stream is checked on load when the stored CRC is non-zero.
- Bidirectional conversion with `mac_img` and `mac_dc42` via `convert`.
  `create -f mac_moof` produces a blank GCR/MFM image (`--fs hfs` formats it).

> **Reference**: The MOOF chunk loader, the GCR 6-and-2 sector encoder,
> and the MFM bit-window / sync-marker / CRC16-CCITT constants were
> cross-validated against [snow](https://github.com/twvd/snow) (MIT, by
> Thomas W.) — specifically `floppy/src/loaders/moof.rs`,
> `floppy/src/macformat.rs`, and the SWIM2/ISM emulation in
> `core/src/mac/swim/ism.rs`. Snow itself adapts encoder logic from
> Greaseweazle / FluxEngine / MESS. The format definition follows the
> [Applesauce MOOF Disk Image Reference](https://applesaucefdc.com/moof-reference/).

### Macintosh HFS Structure

- **Master Directory Block (MDB)** at sector 2 (offset 0x400): drSigWord =
  `BD`, alloc-block layout, catalog / extents file metadata, blessed
  System Folder CNID (`drFndrInfo[0]`).
- **Volume Bitmap** starting at `drVBMSt` (default sector 3): MSB-first
  per Inside Macintosh convention.
- **Catalog B-tree** holds folder / file / thread records keyed by
  `(parentCNID, name)`. `rdedisktool` walks the leaf chain on read and
  splits the leaf automatically on write when full (depth 1→2 root
  promotion is supported; cascading index split is deferred).
- **Extents Overflow B-tree** for files whose forks exceed 3 initial
  extents: read only. `create` writes an empty tree; writes that would need
  overflow extents are not implemented (deferred — needs a fragmented
  fixture for cross-tool verification).
- **Boot block** (sectors 0..1, 1024 bytes total): `LK` signature +
  Pascal name fields (System / Finder / Macsbug / etc.) + boot loader
  code starting at 0x08a. `rdedisktool create --fs hfs` writes a
  scaffold with a halt-loop (`60 fe`) at the entry point.
- **Forks**: every file has a data fork and a resource fork (either may
  be empty). `extract --apple-double` or `extract --macbinary`
  preserves both forks + Finder info; bare `extract` writes only the
  data fork.

### Macintosh MFS Structure

- Older flat-directory file system (no subdirectories) used on the
  earliest Macintosh floppies.
- MDB at offset 0x400 with a **12-bit allocation map** packed into the
  bytes immediately after the MDB header (640 entries). `create` uses
  1024-byte allocation blocks, so 400K fits and 800K (792 blocks) is
  refused.
- Directory entries live in a fixed-size run after the allocation map.

### Macintosh Boot Disk Policy

A volume is treated as a "boot disk" when **all** of:

1. Boot block carries the `LK` signature
2. Either root contains both `System` and `Finder` files, OR a `System
   Folder` subdirectory contains them

In strict mode (`--bootdisk-mode strict`, default) every delete / mkdir /
rmdir / rename on a boot disk is blocked unless `--force-bootdisk` is given,
and `add` may not change existing files. Deleting `System` or `Finder` then
still asks `[y/N]`; `--force-system-file` skips only that question.

## License

This project is part of the Retro Developer Environment Project.

## Contributing

Contributions are welcome! Please see the project repository for guidelines.
