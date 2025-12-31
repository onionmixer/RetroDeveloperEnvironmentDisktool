# RDE Disk Tool (Retro Developer Environment Disk Tool)

A cross-platform command-line tool for manipulating disk images used by retro computer emulators. Supports Apple II and MSX disk formats.

## Features

- **Multi-platform support**: Apple II and MSX disk images
- **File operations**: List, extract, add, and delete files
- **Format conversion**: Convert between compatible disk formats
- **Disk creation**: Create new formatted disk images
- **Validation**: Verify disk image integrity

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
| XSA | .xsa | XSA compressed format |

## Supported File Systems

- **DOS 3.3**: Apple II DOS 3.3 with VTOC-based allocation
- **MSX-DOS**: FAT12 file system (MSX-DOS 1/2 compatible)

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
```

Example:
```bash
rdedisktool info game.dsk
```

#### list - List files in disk image
```bash
rdedisktool list <image_file> [path]
```

Example:
```bash
rdedisktool list mydisk.dsk
```

Output:
```
Directory listing for: mydisk.dsk
Volume: MYDISK

Name                                Size  Type  Attr
----------------------------------------------------
HELLO.BAS                            256  FILE
GAME.COM                            8192  FILE
README.TXT                           512  FILE
----------------------------------------------------
3 file(s), 8960 bytes
Free space: 358400 bytes
```

#### extract - Extract files from disk image
```bash
rdedisktool extract <image_file> <file> [output_path]
```

Example:
```bash
rdedisktool extract game.dsk PLAYER.BIN ./player.bin
```

#### add - Add file to disk image
```bash
rdedisktool add <image_file> <host_file> [target_name]
```

Example:
```bash
rdedisktool add mydisk.dsk ./newgame.com NEWGAME.COM
```

#### delete - Delete file from disk image
```bash
rdedisktool delete <image_file> <file>
```

Example:
```bash
rdedisktool delete mydisk.dsk OLDFILE.TXT
```

#### create - Create new disk image
```bash
rdedisktool create <image_file> --format <format> [--size <size>]
```

Example:
```bash
rdedisktool create newdisk.dsk --format msx-720k
```

#### convert - Convert disk image format
```bash
rdedisktool convert <input_file> <output_file> [--format <format>]
```

Example:
```bash
rdedisktool convert game.do game.po --format prodos
```

#### validate - Validate disk image integrity
```bash
rdedisktool validate <image_file>
```

Example:
```bash
rdedisktool validate corrupted.dsk
```

#### dump - Dump sector/track data
```bash
rdedisktool dump <image_file> --track <n> [--sector <n>] [--side <n>]
```

Example:
```bash
rdedisktool dump mydisk.dsk --track 17 --sector 0
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

## Technical Details

### MSX-DOS FAT12 Structure
- Boot sector with BPB (BIOS Parameter Block)
- Two FAT tables (FAT1 and FAT2)
- Root directory (112 entries for 720KB disk)
- Data clusters (2 sectors per cluster)

### Apple DOS 3.3 Structure
- Track 0: DOS boot code
- Track 17, Sector 0: VTOC (Volume Table of Contents)
- Track 17, Sectors 15-1: Catalog (directory)
- Each file has a Track/Sector list

## License

This project is part of the Retro Developer Environment Project.

## Contributing

Contributions are welcome! Please see the project repository for guidelines.
