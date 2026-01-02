# Retro Developer Environment Disk Tool - Development Plan

## Implementation Status Summary (2026-01-02)

| Category | Status | Notes |
|----------|--------|-------|
| **Core Infrastructure** | ✅ Complete | DiskImage base, CRC, error handling |
| **Apple II Formats** | ✅ Complete | DO, PO, NIB, WOZ v1/v2 |
| **MSX Formats** | ✅ Complete | DSK, DMK, XSA (bidirectional) |
| **Format Conversion** | ✅ Complete | DO↔PO, DSK↔DMK, XSA↔DSK/DMK |
| **CLI Application** | ✅ Complete | All commands implemented |
| **DOS 3.3 File System** | ✅ Complete | Full CRUD, extended validation |
| **ProDOS File System** | ✅ Complete | Full CRUD, subdirectories |
| **MSX-DOS File System** | ✅ Complete | Full CRUD, subdirectories |
| **Validation** | ✅ Complete | All file systems |

---

## 1. Project Overview

### 1.1 Purpose
AppleWin(Apple II) 및 openMSX(MSX) 에뮬레이터에서 사용하는 다양한 floppy disk image 포맷을 통합적으로 관리, 분석, 변환하는 도구 개발

### 1.2 Target Platforms
- **AppleWin (Apple II)**: 5.25" floppy disk emulation
- **openMSX (MSX)**: 3.5" / 5.25" floppy disk emulation

### 1.3 Binary Name
- **Executable**: `rdedisktool` (Retro Developer Environment Disk Tool)
- **Naming Convention**: `rde` prefix for all RDE project binaries

### 1.4 Core Features
1. 디스크 이미지 읽기/쓰기
2. 포맷 변환 (format conversion)
3. 파일 시스템 분석 (DOS 3.3, ProDOS, MSX-DOS)
4. 디스크 이미지 생성 (blank disk creation)
5. 이미지 정보 표시 (metadata extraction)
6. 섹터 수준 편집 (sector-level editing)

---

## 2. Source Code Analysis Summary

### 2.1 AppleWin Disk Image Implementation

#### 2.1.1 Core Source Files
| File | Description | Lines |
|------|-------------|-------|
| `source/DiskDefs.h` | 디스크 상수 정의 | ~50 |
| `source/Disk.h/cpp` | Disk2InterfaceCard 클래스 | ~2,566 |
| `source/DiskImage.h/cpp` | 이미지 조작 API | ~200 |
| `source/DiskImageHelper.h/cpp` | 포맷별 구현 클래스 | ~2,374 |
| `source/DiskFormatTrack.h/cpp` | 트랙 포맷팅 도우미 | ~300 |

#### 2.1.2 Supported Formats
| Format | Extension | Structure | Read | Write | Create |
|--------|-----------|-----------|------|-------|--------|
| DOS Order | .do, .dsk | 35 tracks × 16 sectors × 256 bytes = 140KB | O | O | O |
| ProDOS Order | .po | 35 tracks × 16 sectors × 256 bytes = 140KB | O | O | X |
| Nibble 6656 | .nib | 35 tracks × 6,656 bytes = 232,960 bytes | O | O | O |
| Nibble 6384 | .nb2 | 35 tracks × 6,384 bytes = 223,440 bytes | O | O | X |
| WOZ v1 | .woz | Bit-level, variable track size | O | O | O |
| WOZ v2 | .woz | Bit-level, enhanced v1 | O | O | O |
| Hard Disk | .hdv | Block device | O | O | X |

#### 2.1.3 Key Constants (DiskDefs.h)
```cpp
NIBBLES_PER_TRACK_NIB   = 0x1A00  // 6,656 nibbles
NIBBLES_PER_TRACK_WOZ2  = 0x1A18  // 6,680 nibbles (max)
NUM_SECTORS             = 16
TRACKS_STANDARD         = 35
TRACKS_MAX              = 40
TRACK_DENIBBLIZED_SIZE  = 4,096  // 16 × 256 bytes
```

#### 2.1.4 Nibble Encoding (6-and-2 GCR)
- **Purpose**: 256 bytes → 343 bytes (nibblized)
- **Process**:
  1. 256 bytes를 342개의 6-bit 값으로 변환 (3 bytes → 4 nibbles)
  2. XOR 체크섬 1 byte 추가
  3. 6-bit → 8-bit disk byte 변환 (lookup table)

#### 2.1.5 Track Structure (NIB format)
```
[Gap1: 48 × 0xFF]
[Sector 0-15]:
  ├─ Address Field: D5 AA 96 | Vol(2) | Trk(2) | Sec(2) | Chk(2) | DE AA EB
  ├─ Gap2: 6 × 0xFF
  ├─ Data Field: D5 AA AD | Data(343) | DE AA EB
  └─ Gap3: 27 × 0xFF
```

#### 2.1.6 WOZ Format Structure
```
[Header: 12 bytes]
  ├─ ID1: 'WOZ1' or 'WOZ2'
  ├─ ID2: 0x0A0D0AFF
  └─ CRC32: whole file checksum

[INFO Chunk]
  ├─ version, diskType, writeProtected
  └─ creator (UTF-8, 32 bytes)

[TMAP Chunk: 160 bytes]
  └─ Quarter-track mapping (40 tracks × 4)

[TRKS Chunk]
  └─ Track data with bytesUsed, bitCount
```

#### 2.1.7 Sector Order Mapping
| Logical | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | A | B | C | D | E | F |
|---------|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DOS Order | 0 | D | B | 9 | 7 | 5 | 3 | 1 | E | C | A | 8 | 6 | 4 | 2 | F |
| ProDOS | 0 | 8 | 1 | 9 | 2 | A | 3 | B | 4 | C | 5 | D | 6 | E | 7 | F |

---

### 2.2 openMSX Disk Image Implementation

#### 2.2.1 Core Source Files
| File | Description |
|------|-------------|
| `src/fdc/Disk.hh/cc` | Disk 추상 클래스 |
| `src/fdc/SectorAccessibleDisk.hh` | 섹터 접근 인터페이스 |
| `src/fdc/SectorBasedDisk.hh/cc` | 섹터 기반 디스크 |
| `src/fdc/DSKDiskImage.hh/cc` | .dsk 포맷 |
| `src/fdc/DMKDiskImage.hh/cc` | .dmk 포맷 |
| `src/fdc/XSADiskImage.hh/cc` | .xsa 압축 포맷 |
| `src/fdc/DiskFactory.hh/cc` | 이미지 팩토리 |
| `src/fdc/RawTrack.hh` | raw track 데이터 구조 |
| `src/fdc/DiskImageUtils.hh/cc` | 디스크 유틸리티 |
| `src/fdc/XSAExtractor.hh/cc` | XSA 압축 해제 |

#### 2.2.2 Supported Formats
| Format | Extension | Structure | Read | Write | Notes |
|--------|-----------|-----------|------|-------|-------|
| DSK | .dsk, .di1, .di2, .fd1, .fd2 | Pure sector data | O | O | 720KB standard |
| DMK | .dmk | Raw track + IDAM table | O | O | TRS-80 origin |
| XSA | .xsa | LZ77 compressed | O | X | Xelasoft Archived |
| RamDSK | (virtual) | Memory-based | O | O | 720KB |
| DirAsDSK | (directory) | Host directory mapping | O | O/X | Sync mode |

#### 2.2.3 Key Constants
```cpp
SECTOR_SIZE             = 512 bytes
STANDARD_TRACK_SIZE     = 6,250 bytes (RawTrack)
SECTORS_PER_TRACK       = 9 (720KB) or 18 (1.44MB)
NUM_TRACKS              = 80
SECTORS_PER_CLUSTER     = 2
```

#### 2.2.4 DMK Header Structure (16 bytes)
```cpp
struct DmkHeader {
    uint8_t writeProtected;     // +0: 0x00 or 0xFF
    uint8_t numTracks;          // +1: track count
    uint16_t trackLen;          // +2-3: track length (LE)
    uint8_t flags;              // +4: FLAG_SINGLE_SIDED (0x10)
    uint8_t reserved[7];        // +5-11: reserved
    uint8_t format[4];          // +12-15: format info
};
```

#### 2.2.5 DMK Track Layout
```
[IDAM Table: 128 bytes]
  └─ 64 entries × 2 bytes (position + flags)
[Track Data: trackLen - 128 bytes]
  └─ Raw MFM-encoded data
```

#### 2.2.6 MSX Boot Sector Structure
```cpp
struct MSXBootSector {
    uint8_t  jumpCode[3];      // +0: 0xE5 or 0xEB
    char     name[8];          // +3: OEM name
    uint16_t bytesPerSector;   // +11: always 512
    uint8_t  sectorsPerCluster;// +13
    uint16_t reservedSectors;  // +14
    uint8_t  numFATs;          // +16
    uint16_t dirEntries;       // +17
    uint16_t totalSectors;     // +19
    uint8_t  mediaDescriptor;  // +21
    uint16_t sectorsPerFAT;    // +22
    uint16_t sectorsPerTrack;  // +24
    uint16_t numSides;         // +26
    // ... additional fields
};
```

#### 2.2.7 Media Descriptor Table
| Descriptor | 0xF8 | 0xF9 | 0xFA | 0xFB | 0xFC | 0xFD | 0xFE | 0xFF |
|------------|------|------|------|------|------|------|------|------|
| Sectors/Track | 9 | 9 | 8 | 8 | 9 | 9 | 8 | 8 |
| Sides | 1 | 2 | 1 | 2 | 1 | 2 | 1 | 2 |
| Tracks/Side | 80 | 80 | 80 | 80 | 40 | 40 | 40 | 40 |

#### 2.2.8 XSA Compression Format
- **Header Magic**: `'P' 'C' 'K' '\010'`
- **Algorithm**: LZ77 with Huffman coding
- **Structure**:
  - 4 bytes: original length (LE)
  - 4 bytes: compressed length (skipped)
  - null-terminated filename (skipped)
  - compressed data stream

---

## 3. Unified Disk Tool Architecture

### 3.1 Class Hierarchy Design

```
DiskImage (abstract base)
├── AppleDiskImage (abstract)
│   ├── AppleDOImage      (.do, .dsk - DOS order)
│   ├── ApplePOImage      (.po - ProDOS order)
│   ├── AppleNibImage     (.nib - 6656 bytes/track)
│   ├── AppleNib2Image    (.nb2 - 6384 bytes/track)
│   ├── AppleWoz1Image    (.woz - WOZ v1)
│   └── AppleWoz2Image    (.woz - WOZ v2)
│
├── MSXDiskImage (abstract)
│   ├── MSXDSKImage       (.dsk, .di1, .di2, .fd1, .fd2)
│   ├── MSXDMKImage       (.dmk - raw track)
│   └── MSXXSAImage       (.xsa - compressed)
│
└── Utilities
    ├── DiskImageFactory  (format detection & creation)
    ├── NibbleEncoder     (6-and-2 GCR for Apple II)
    ├── MFMEncoder        (MFM encoding for MSX)
    ├── XSACompressor     (LZ77 compression)
    └── CRCCalculator     (CRC-16, CRC-32)
```

### 3.2 Core Interfaces

#### 3.2.1 DiskImage Interface
```cpp
class DiskImage {
public:
    virtual ~DiskImage() = default;

    // Identification
    virtual std::string getFormatName() const = 0;
    virtual std::string getPlatform() const = 0;  // "AppleII" or "MSX"

    // Properties
    virtual size_t getTrackCount() const = 0;
    virtual size_t getSideCount() const = 0;
    virtual size_t getSectorsPerTrack() const = 0;
    virtual size_t getBytesPerSector() const = 0;
    virtual size_t getTotalSize() const = 0;
    virtual bool isWriteProtected() const = 0;

    // I/O Operations
    virtual bool open(const std::string& path) = 0;
    virtual bool save(const std::string& path) = 0;
    virtual bool close() = 0;

    // Sector Access
    virtual bool readSector(int track, int side, int sector,
                           std::vector<uint8_t>& buffer) = 0;
    virtual bool writeSector(int track, int side, int sector,
                            const std::vector<uint8_t>& buffer) = 0;

    // Raw Track Access (optional)
    virtual bool hasRawTrackSupport() const { return false; }
    virtual bool readRawTrack(int track, int side,
                             std::vector<uint8_t>& buffer) { return false; }
    virtual bool writeRawTrack(int track, int side,
                              const std::vector<uint8_t>& buffer) { return false; }

    // Metadata
    virtual std::string getMetadata() const = 0;
};
```

#### 3.2.2 DiskImageFactory Interface
```cpp
class DiskImageFactory {
public:
    // Auto-detect format from file content
    static std::unique_ptr<DiskImage> open(const std::string& path);

    // Create new blank disk
    static std::unique_ptr<DiskImage> create(
        const std::string& format,   // "do", "po", "nib", "woz1", "woz2", "msx-dsk", "dmk"
        const std::string& path,
        size_t tracks = 0,           // 0 = default for format
        size_t sides = 0
    );

    // Format conversion
    static bool convert(
        const DiskImage& source,
        const std::string& targetFormat,
        const std::string& targetPath
    );

    // List supported formats
    static std::vector<std::string> getSupportedFormats();
};
```

---

## 4. Implementation Modules

### 4.1 Module: Apple II Nibble Encoder

**Purpose**: DO/PO 형식과 NIB/WOZ 형식 간 변환

**Key Functions**:
```cpp
class AppleNibbleEncoder {
public:
    // Sector encoding (256 bytes → 343 nibbles)
    static std::vector<uint8_t> encodeSector(
        const std::vector<uint8_t>& data,  // 256 bytes
        uint8_t volume,
        uint8_t track,
        uint8_t sector
    );

    // Sector decoding (343 nibbles → 256 bytes)
    static std::vector<uint8_t> decodeSector(
        const std::vector<uint8_t>& nibbles,
        uint8_t& volume,
        uint8_t& track,
        uint8_t& sector
    );

    // Track encoding (4096 bytes → ~6600 nibbles)
    static std::vector<uint8_t> encodeTrack(
        const std::vector<uint8_t>& trackData,  // 16 × 256 bytes
        uint8_t volume,
        uint8_t trackNum,
        SectorOrder order
    );

    // Track decoding (~6600 nibbles → 4096 bytes)
    static std::vector<uint8_t> decodeTrack(
        const std::vector<uint8_t>& nibbles,
        SectorOrder order
    );

private:
    static const uint8_t DISK_BYTE_TABLE[64];  // 6-bit to 8-bit
    static const uint8_t REVERSE_TABLE[256];    // 8-bit to 6-bit

    static std::vector<uint8_t> code62(const std::vector<uint8_t>& data);
    static std::vector<uint8_t> decode62(const std::vector<uint8_t>& nibbles);
};
```

### 4.2 Module: MSX MFM Encoder

**Purpose**: DSK 형식과 DMK raw track 간 변환

**Key Functions**:
```cpp
class MSXMFMEncoder {
public:
    // Generate raw track from sectors
    static std::vector<uint8_t> encodeTrack(
        const std::vector<std::vector<uint8_t>>& sectors,  // 9 × 512 bytes
        uint8_t track,
        uint8_t side
    );

    // Extract sectors from raw track
    static std::vector<std::vector<uint8_t>> decodeTrack(
        const std::vector<uint8_t>& rawTrack,
        std::vector<int>& idamPositions
    );

    // CRC calculation (CCITT)
    static uint16_t calculateCRC(const std::vector<uint8_t>& data);

private:
    static const size_t TRACK_SIZE = 6250;
    static const size_t SECTOR_SIZE = 512;
    static const size_t SECTORS_PER_TRACK = 9;
};
```

### 4.3 Module: XSA Compressor

**Purpose**: XSA 압축 형식 읽기/쓰기

**Key Functions**:
```cpp
class XSACompressor {
public:
    // Decompress XSA to raw sectors
    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& compressed);

    // Compress raw sectors to XSA
    static std::vector<uint8_t> compress(
        const std::vector<uint8_t>& data,
        const std::string& filename = ""
    );

    // Check if data is XSA format
    static bool isXSAFormat(const std::vector<uint8_t>& data);

private:
    static const uint8_t MAGIC[4];  // 'P', 'C', 'K', '\010'

    // LZ77 implementation
    static void lz77Decompress(/*...*/);
    static void lz77Compress(/*...*/);

    // Huffman coding
    struct HuffNode { /*...*/ };
    static void buildHuffTree(/*...*/);
};
```

### 4.4 Module: WOZ Handler

**Purpose**: WOZ v1/v2 파일 읽기/쓰기

**Key Structures**:
```cpp
struct WOZHeader {
    uint32_t id1;       // 'WOZ1' or 'WOZ2'
    uint32_t id2;       // 0x0A0D0AFF
    uint32_t crc32;
};

struct WOZInfoChunk {
    uint8_t version;
    uint8_t diskType;       // 1 = 5.25", 2 = 3.5"
    uint8_t writeProtected;
    uint8_t synchronized;
    uint8_t cleaned;
    char creator[32];
};

struct WOZTrackV1 {
    uint16_t bytesUsed;
    uint16_t bitCount;
    uint16_t splicePoint;
    uint8_t spliceNibble;
    uint8_t spliceBitCount;
    // followed by track data
};

struct WOZTrackV2 {
    uint16_t startBlock;
    uint16_t blockCount;
    uint32_t bitCount;
};

class WOZHandler {
public:
    bool open(const std::string& path);
    bool save(const std::string& path);

    uint8_t getVersion() const;
    bool isWriteProtected() const;
    std::string getCreator() const;

    // Track access (quarter-track precision)
    bool readTrack(float quarterTrack, std::vector<uint8_t>& data,
                   uint32_t& bitCount);
    bool writeTrack(float quarterTrack, const std::vector<uint8_t>& data,
                    uint32_t bitCount);

    // Metadata
    std::string getMetadata(const std::string& key) const;
    void setMetadata(const std::string& key, const std::string& value);

private:
    WOZHeader header;
    WOZInfoChunk info;
    std::vector<uint8_t> tmap;  // 160 entries
    std::vector<std::vector<uint8_t>> tracks;
};
```

---

## 5. Command-Line Interface Design

### 5.1 Usage Examples

```bash
# Display disk image information
$ rdedisktool info game.dsk
Format: Apple II DOS Order (.dsk)
Tracks: 35, Sides: 1, Sectors/Track: 16
Sector Size: 256 bytes, Total: 143,360 bytes
Write Protected: No

# Convert formats
$ rdedisktool convert game.dsk game.woz --format=woz2
Converting: DOS Order → WOZ v2
Done: game.woz (245,760 bytes)

$ rdedisktool convert game.nib game.po --format=po
Converting: Nibble → ProDOS Order
Done: game.po (143,360 bytes)

# Create blank disk
$ rdedisktool create blank.dsk --format=do --tracks=35
Created: blank.dsk (143,360 bytes, DOS 3.3 formatted)

$ rdedisktool create msx.dsk --format=msx-dsk --size=720k
Created: msx.dsk (737,280 bytes, MSX-DOS formatted)

# Read/write sectors
$ rdedisktool read game.dsk 17 0 0 --output=sector.bin
Read track 17, side 0, sector 0 → sector.bin (256 bytes)

$ rdedisktool write game.dsk 17 0 0 --input=sector.bin
Wrote sector.bin → track 17, side 0, sector 0

# Hexdump sector
$ rdedisktool dump game.dsk 17 0 0
Track 17, Side 0, Sector 0:
0000: 11 0F 03 00 00 00 FE 00  00 00 00 00 00 00 00 00
0010: 00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
...

# List files (filesystem aware)
$ rdedisktool ls game.dsk
Volume: GAME DISK
Files:
  HELLO          A   002  $0801
  GAME.OBJ       B   045  $2000
  DATA           B   012  $6000

# Extract files
$ rdedisktool extract game.dsk HELLO --output=hello.bas
Extracted: HELLO → hello.bas (512 bytes)

# Compare images
$ rdedisktool compare game1.dsk game2.dsk
Comparing game1.dsk and game2.dsk...
Track 17, Sector 0: DIFFERENT
Track 17, Sector 1: DIFFERENT
2 sectors differ

# Verify integrity
$ rdedisktool verify game.woz
Checking WOZ integrity...
Header CRC: OK
Track 0: 6250 bits, OK
Track 1: 6248 bits, OK
...
All tracks verified.

# Show version
$ rdedisktool --version
rdedisktool v1.0.0 - Retro Developer Environment Disk Tool
```

### 5.2 Command Structure

```
rdedisktool <command> [options] <arguments>

Commands:
  info      Display disk image information
  convert   Convert between disk image formats
  create    Create a new blank disk image
  read      Read a sector to file
  write     Write a file to sector
  dump      Hexdump a sector
  ls        List files (DOS 3.3, ProDOS, MSX-DOS)
  extract   Extract file from disk image
  insert    Insert file into disk image
  compare   Compare two disk images
  verify    Verify disk image integrity
  format    Format disk with filesystem

Options:
  --format=<fmt>   Target format (do, po, nib, woz1, woz2, msx-dsk, dmk)
  --tracks=<n>     Number of tracks (default: format-specific)
  --sides=<n>      Number of sides (1 or 2)
  --size=<size>    Disk size (360k, 720k, 1.44m)
  --output=<file>  Output file path
  --input=<file>   Input file path
  --force          Overwrite without confirmation
  --verbose        Verbose output
  --version        Show version information
  --help           Show help
```

---

## 6. File System Support

### 6.1 Apple II File Systems

#### 6.1.1 DOS 3.3
- **VTOC** (Volume Table of Contents): Track 17, Sector 0
- **Catalog**: Track 17, Sectors 1-15
- **File Types**: T (Text), I (Integer BASIC), A (Applesoft BASIC), B (Binary), R (Relocatable), S (S-type), A/B (A/B)

#### 6.1.2 ProDOS
- **Volume Directory**: Block 2-5
- **File Types**: TXT, BIN, BAS, SYS, etc.
- **Block Size**: 512 bytes

### 6.2 MSX File Systems

#### 6.2.1 MSX-DOS 1
- **Boot Sector**: Sector 0
- **FAT**: Sector 1-2
- **Root Directory**: Sector 3-9
- **12-bit FAT entries**

#### 6.2.2 MSX-DOS 2
- **Extended boot sector**
- **Subdirectory support**
- **Long filename support (optional)**

---

## 7. Development Phases

### Phase 1: Core Infrastructure ✅ COMPLETE
- [x] DiskImage abstract base class
- [x] File I/O utilities
- [x] CRC calculation (CRC-16 CCITT, CRC-32)
- [x] Basic error handling

### Phase 2: Apple II Format Support ✅ COMPLETE
- [x] DO/DSK format read/write
- [x] PO format read/write
- [x] 6-and-2 GCR nibble encoder/decoder
- [x] NIB format read/write
- [x] WOZ v1 format read/write
- [x] WOZ v2 format read/write

### Phase 3: MSX Format Support ✅ COMPLETE
- [x] DSK format read/write
- [x] DMK format read/write
- [x] MFM encoder/decoder
- [x] XSA decompression
- [x] XSA compression (full bidirectional support)

### Phase 4: Format Conversion ✅ COMPLETE
- [x] DO ↔ PO conversion
- [x] DSK ↔ DMK conversion
- [x] XSA ↔ DSK/DMK bidirectional conversion

### Phase 5: CLI Application ✅ COMPLETE
- [x] Command-line parser
- [x] info command
- [x] convert command
- [x] create command
- [x] read/write/dump commands
- [x] list command
- [x] add/extract/delete commands
- [x] mkdir/rmdir commands
- [x] validate command

### Phase 6: File System Support ✅ COMPLETE
- [x] DOS 3.3 catalog parsing & file operations
- [x] DOS 3.3 extended validation
- [x] ProDOS directory parsing & file operations
- [x] ProDOS subdirectory support
- [x] MSX-DOS FAT parsing & file operations
- [x] MSX-DOS subdirectory support
- [x] File extraction (all file systems)
- [x] File insertion (all file systems)
- [x] File deletion (all file systems)
- [x] File rename (all file systems)

### Phase 7: Advanced Features ✅ COMPLETE
- [x] Integrity verification (validate command)
- [x] Extended validation (DOS 3.3, ProDOS, MSX-DOS)

---

## 8. Technical Considerations

### 8.1 Endianness
- Apple II: Little-endian
- MSX: Little-endian
- WOZ format: Little-endian
- DMK format: Little-endian

### 8.2 Platform Compatibility
- C++17 or later
- Cross-platform (Windows, macOS, Linux)
- No external dependencies for core functionality
- Optional: zlib for gzip/zip support

### 8.3 Error Handling
```cpp
enum class DiskError {
    None,
    FileNotFound,
    InvalidFormat,
    ReadError,
    WriteError,
    WriteProtected,
    SectorNotFound,
    CRCError,
    UnsupportedFormat,
    ConversionError
};

class DiskException : public std::exception {
    DiskError error;
    std::string message;
public:
    DiskError getError() const;
    const char* what() const noexcept override;
};
```

### 8.4 Memory Management
- Use RAII and smart pointers
- Lazy loading for large images
- Memory-mapped I/O option for performance
- Sector caching for frequent access

---

## 9. Testing Strategy

### 9.1 Unit Tests
- Nibble encoder/decoder
- MFM encoder/decoder
- CRC calculations
- Sector order conversions
- XSA compression/decompression

### 9.2 Integration Tests
- Round-trip format conversion
- File extraction and verification
- Compatibility with AppleWin/openMSX

### 9.3 Test Images
- Standard blank disks
- Known game images (public domain)
- Edge cases (protected disks, non-standard formats)

---

## 10. References

### 10.1 AppleWin Source Code
- `AppleWin/source/DiskDefs.h` - Core constants
- `AppleWin/source/DiskImageHelper.h/cpp` - Format implementations
- `AppleWin/source/Disk.h/cpp` - Drive emulation

### 10.2 openMSX Source Code
- `openMSX/src/fdc/Disk.hh/cc` - Disk abstraction
- `openMSX/src/fdc/DMKDiskImage.hh/cc` - DMK format
- `openMSX/src/fdc/XSAExtractor.hh/cc` - XSA decompression

### 10.3 Format Specifications
- [WOZ 2.0 Disk Image Reference](https://applesaucefdc.com/woz/reference2/)
- [DMK Format Documentation](http://www.trs-80.com/wordpress/dsk-and-dmk-image-utilities/)
- [Apple II DOS 3.3 Technical Reference](https://www.apple2.org.za/gswv/a2zine/Docs/DossyDos.html)
- [MSX-DOS Technical Handbook](https://www.msx.org/wiki/MSX-DOS)

---

## 11. File Structure

```
RetroDeveloperEnvironmentDisktool/
├── include/
│   ├── rdedisktool/
│   │   ├── DiskImage.h
│   │   ├── DiskImageFactory.h
│   │   ├── AppleDiskImage.h
│   │   ├── MSXDiskImage.h
│   │   ├── NibbleEncoder.h
│   │   ├── MFMEncoder.h
│   │   ├── XSACompressor.h
│   │   ├── WOZHandler.h
│   │   ├── CRC.h
│   │   ├── Version.h
│   │   └── Exceptions.h
│   └── rdedisktool.h
├── src/
│   ├── core/
│   │   ├── DiskImage.cpp
│   │   ├── DiskImageFactory.cpp
│   │   └── CRC.cpp
│   ├── apple/
│   │   ├── AppleDOImage.cpp
│   │   ├── ApplePOImage.cpp
│   │   ├── AppleNibImage.cpp
│   │   ├── AppleWozImage.cpp
│   │   └── NibbleEncoder.cpp
│   ├── msx/
│   │   ├── MSXDSKImage.cpp
│   │   ├── MSXDMKImage.cpp
│   │   ├── MSXXSAImage.cpp
│   │   ├── MFMEncoder.cpp
│   │   └── XSACompressor.cpp
│   └── cli/
│       └── main.cpp
├── tests/
│   ├── test_nibble.cpp
│   ├── test_mfm.cpp
│   ├── test_woz.cpp
│   ├── test_xsa.cpp
│   └── test_conversion.cpp
├── docs/
│   └── Disktool_Development_Plan.md
├── CMakeLists.txt
└── README.md
```

### 11.1 CMakeLists.txt Configuration

```cmake
cmake_minimum_required(VERSION 3.16)
project(rdedisktool VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Version information
configure_file(
    "${CMAKE_SOURCE_DIR}/include/rdedisktool/Version.h.in"
    "${CMAKE_SOURCE_DIR}/include/rdedisktool/Version.h"
)

# Library target
add_library(rdedisktool_lib STATIC
    src/core/DiskImage.cpp
    src/core/DiskImageFactory.cpp
    src/core/CRC.cpp
    src/apple/AppleDOImage.cpp
    src/apple/ApplePOImage.cpp
    src/apple/AppleNibImage.cpp
    src/apple/AppleWozImage.cpp
    src/apple/NibbleEncoder.cpp
    src/msx/MSXDSKImage.cpp
    src/msx/MSXDMKImage.cpp
    src/msx/MSXXSAImage.cpp
    src/msx/MFMEncoder.cpp
    src/msx/XSACompressor.cpp
)

target_include_directories(rdedisktool_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# Executable target - Binary name: rdedisktool
add_executable(rdedisktool src/cli/main.cpp)
target_link_libraries(rdedisktool PRIVATE rdedisktool_lib)

# Installation
install(TARGETS rdedisktool DESTINATION bin)
install(DIRECTORY include/rdedisktool DESTINATION include)

# Testing (optional)
option(BUILD_TESTS "Build test suite" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

### 11.2 Version Header Template (Version.h.in)

```cpp
#ifndef RDEDISKTOOL_VERSION_H
#define RDEDISKTOOL_VERSION_H

#define RDEDISKTOOL_NAME        "rdedisktool"
#define RDEDISKTOOL_FULL_NAME   "Retro Developer Environment Disk Tool"
#define RDEDISKTOOL_VERSION     "@PROJECT_VERSION@"
#define RDEDISKTOOL_VERSION_MAJOR @PROJECT_VERSION_MAJOR@
#define RDEDISKTOOL_VERSION_MINOR @PROJECT_VERSION_MINOR@
#define RDEDISKTOOL_VERSION_PATCH @PROJECT_VERSION_PATCH@

#endif // RDEDISKTOOL_VERSION_H
```

---

## 12. File Merge Feature - Cross-Platform Binary/File Injection

### 12.1 Feature Overview

Linux 환경에서 크로스 툴체인으로 빌드한 바이너리 파일이나 텍스트 파일 등을 Apple II 및 MSX 디스크 이미지에 삽입하는 기능

**주요 사용 사례:**
- cc65로 빌드한 Apple II 바이너리를 .dsk/.po/.woz 이미지에 삽입
- SDCC/z88dk로 빌드한 MSX 바이너리를 .dsk/.dmk 이미지에 삽입
- 어셈블러로 생성한 .bin 파일 삽입
- 텍스트/데이터 파일 삽입

### 12.2 Feasibility Analysis

#### 12.2.1 Apple II - 구현 가능성: **높음**

| 파일 시스템 | 구현 상태 | 필요 작업 |
|------------|----------|-----------|
| ProDOS | **완전 구현됨** | AppleWin의 `Util_ProDOS_AddFile()` 참조 |
| DOS 3.3 | **부분 구현** | VTOC/Catalog 조작 함수 추가 필요 |

**ProDOS 파일 삽입 (AppleWin 소스 참조):**
- 핵심 함수: `ProDOS_Utils.cpp` - `Util_ProDOS_AddFile()`
- 블록 할당: `ProDOS_BlockGetFirstFree()`, `ProDOS_BlockSetUsed()`
- 저장 타입: SEED (≤512B), SAPL (≤128KB), TREE (>128KB)

**DOS 3.3 파일 삽입 (추가 구현 필요):**
- VTOC 비트맵 조작
- Catalog 엔트리 생성
- Track/Sector 체인 구성

#### 12.2.2 MSX - 구현 가능성: **높음**

| 파일 시스템 | 구현 상태 | 필요 작업 |
|------------|----------|-----------|
| MSX-DOS 1/2 | **완전 구현됨** | openMSX의 `MSXtar` 클래스 참조 |
| FAT12/FAT16 | **완전 구현됨** | DirAsDSK/MSXtar 로직 활용 |

**MSX-DOS 파일 삽입 (openMSX 소스 참조):**
- 핵심 클래스: `MSXtar.cc` - `addFileToDSK()`, `alterFileInDSK()`
- FAT 조작: `readFAT()`, `writeFAT()`
- 클러스터 할당: `findFirstFreeCluster()`, `findNextFreeCluster()`

---

### 12.3 Apple II File System Structures

#### 12.3.1 DOS 3.3 VTOC Structure (Track 17, Sector 0)

```
Offset  Size    Description
------  ------  ----------------------------------------
0x00    1       (unused)
0x01    1       Catalog Track (usually 0x11)
0x02    1       Catalog Sector (usually 0x0F)
0x03    1       DOS Version
0x06    1       Volume Number (default: 254)
0x27    1       Max Track/Sector pairs per T/S list
0x30    1       Last Track Allocated
0x31    1       Allocation Direction (+1 or -1)
0x34    1       Number of Tracks
0x35    1       Sectors per Track (16)
0x36-37 2       Bytes per Sector (256)
0x38+   4*N     Track Bitmap (4 bytes per track)
```

**Track Bitmap (per track, 4 bytes):**
```
Byte 0: Sectors F-8 free bitmap (bit=1: free)
Byte 1: Sectors 7-0 free bitmap (bit=1: free)
Byte 2-3: Reserved (0x00)
```

#### 12.3.2 DOS 3.3 Catalog Entry (35 bytes)

```
Offset  Size    Description
------  ------  ----------------------------------------
0x00    1       Track of first T/S list sector
0x01    1       Sector of first T/S list sector
0x02    1       File type + flags
                  Bit 7: Deleted flag
                  Bit 0-6: Type (T=0x00, I=0x01, A=0x02, B=0x04)
0x03-20 30      Filename (padded with spaces)
0x21-22 2       Length in sectors
```

**File Types:**
| Code | Type | Description |
|------|------|-------------|
| 0x00 | T | Text file |
| 0x01 | I | Integer BASIC |
| 0x02 | A | Applesoft BASIC |
| 0x04 | B | Binary file |
| 0x08 | S | S-type file |
| 0x10 | R | Relocatable |
| 0x20 | (new A) | New A file |
| 0x40 | (new B) | New B file |

#### 12.3.3 DOS 3.3 Track/Sector List

```
Offset  Size    Description
------  ------  ----------------------------------------
0x00    1       (unused)
0x01    1       Next T/S list Track (0 if last)
0x02    1       Next T/S list Sector
0x05-06 2       Sector offset in file
0x0C+   2*N     Track/Sector pairs (122 max per sector)
```

#### 12.3.4 ProDOS Volume Directory (Block 2)

```
Offset  Size    Description
------  ------  ----------------------------------------
0x00-01 2       Previous directory block
0x02-03 2       Next directory block
0x04    1       Storage type (0xF0) + name length
0x05-13 15      Volume name
0x1C-1D 2       Creation date
0x1E-1F 2       Creation time
0x20    1       Version
0x21    1       Min version
0x22    1       Access rights
0x23    1       Entry length (0x27 = 39 bytes)
0x24    1       Entries per block (13)
0x25-26 2       Active file count
0x27-28 2       Bitmap block pointer
0x29-2A 2       Total blocks
```

#### 12.3.5 ProDOS File Entry (39 bytes)

```
Offset  Size    Description
------  ------  ----------------------------------------
0x00    1       Storage type + name length
                  0x1: Seedling (1 block)
                  0x2: Sapling (≤256 blocks)
                  0x3: Tree (>256 blocks)
                  0xD: Subdirectory
0x01-0F 15      Filename
0x10    1       File type
0x11-12 2       Key block pointer
0x13-14 2       Blocks used
0x15-17 3       EOF (file size)
0x18-19 2       Creation date
0x1A-1B 2       Creation time
0x1C    1       Version
0x1D    1       Min version
0x1E    1       Access
0x1F-20 2       Aux type (load address for BIN)
0x21-22 2       Last modified date
0x23-24 2       Last modified time
0x25-26 2       Header pointer
```

**ProDOS File Types (common):**
| Code | Type | Description |
|------|------|-------------|
| 0x00 | UNK | Unknown |
| 0x04 | TXT | Text file |
| 0x06 | BIN | Binary file |
| 0xFC | BAS | BASIC program |
| 0xFF | SYS | System file |

---

### 12.4 MSX File System Structures

#### 12.4.1 MSX Boot Sector (Sector 0)

```
Offset  Size    Description
------  ------  ----------------------------------------
0x00-02 3       Jump code (0xEB, 0xFE, 0x90)
0x03-0A 8       OEM name ("NMS 2.0P")
0x0B-0C 2       Bytes per sector (512)
0x0D    1       Sectors per cluster (2)
0x0E-0F 2       Reserved sectors (1)
0x10    1       Number of FATs (2)
0x11-12 2       Root directory entries (112)
0x13-14 2       Total sectors
0x15    1       Media descriptor (0xF9=2-sided, 0xF8=1-sided)
0x16-17 2       Sectors per FAT
0x18-19 2       Sectors per track (9)
0x1A-1B 2       Number of sides (2)
0x1C-1D 2       Hidden sectors (0)
```

**Media Descriptor Table:**
| Value | Sides | Tracks | Sectors/Track |
|-------|-------|--------|---------------|
| 0xF8 | 1 | 80 | 9 |
| 0xF9 | 2 | 80 | 9 |
| 0xFA | 1 | 80 | 8 |
| 0xFB | 2 | 80 | 8 |
| 0xFC | 1 | 40 | 9 |
| 0xFD | 2 | 40 | 9 |
| 0xFE | 1 | 40 | 8 |
| 0xFF | 2 | 40 | 8 |

#### 12.4.2 MSX FAT12 Structure

**FAT Entry Encoding (12-bit):**
```cpp
// Reading FAT12 entry
offset = (cluster * 3) / 2;
if (cluster & 1) {  // Odd cluster
    value = (fat[offset] >> 4) | (fat[offset+1] << 4);
} else {  // Even cluster
    value = fat[offset] | ((fat[offset+1] & 0x0F) << 8);
}
```

**FAT Values:**
| Value | Meaning |
|-------|---------|
| 0x000 | Free cluster |
| 0x001 | Reserved |
| 0x002-0xFF6 | Next cluster in chain |
| 0xFF7 | Bad cluster |
| 0xFF8-0xFFF | End of chain |

#### 12.4.3 MSX Directory Entry (32 bytes)

```
Offset  Size    Description
------  ------  ----------------------------------------
0x00-07 8       Filename (space-padded)
0x08-0A 3       Extension (space-padded)
0x0B    1       Attributes
                  0x01: Read-only
                  0x02: Hidden
                  0x04: System
                  0x08: Volume label
                  0x10: Directory
                  0x20: Archive
0x0C-15 10      Reserved
0x16-17 2       Time (DOS format)
                  Bits 0-4: Seconds/2
                  Bits 5-10: Minutes
                  Bits 11-15: Hours
0x18-19 2       Date (DOS format)
                  Bits 0-4: Day
                  Bits 5-8: Month
                  Bits 9-15: Year-1980
0x1A-1B 2       Start cluster
0x1C-1F 4       File size (bytes)
```

**Special Filename Bytes:**
| Value | Meaning |
|-------|---------|
| 0x00 | Entry never used |
| 0xE5 | Deleted entry |
| 0x05 | First char is 0xE5 |

---

### 12.5 File Merge Implementation Architecture

#### 12.5.1 Core Interface

```cpp
class FileSystemHandler {
public:
    virtual ~FileSystemHandler() = default;

    // File operations
    virtual bool addFile(const std::string& hostPath,
                        const std::string& targetName,
                        const FileMetadata& meta) = 0;
    virtual bool deleteFile(const std::string& targetName) = 0;
    virtual bool extractFile(const std::string& targetName,
                            const std::string& hostPath) = 0;
    virtual std::vector<FileEntry> listFiles() = 0;

    // Disk operations
    virtual size_t getFreeSpace() const = 0;
    virtual size_t getTotalSpace() const = 0;
    virtual bool format(const FormatOptions& options) = 0;
};

struct FileMetadata {
    std::string filename;       // Target filename
    FileType type;              // Platform-specific file type
    uint16_t loadAddress;       // Load address (for binary)
    uint16_t execAddress;       // Execution address (optional)
    bool readOnly;              // Read-only flag
    bool hidden;                // Hidden flag
    std::optional<time_t> timestamp;  // File timestamp
};
```

#### 12.5.2 Apple II Implementation

```cpp
class AppleDOS33Handler : public FileSystemHandler {
private:
    std::vector<uint8_t>& diskImage;

    // VTOC operations
    bool readVTOC(VTOC& vtoc);
    bool writeVTOC(const VTOC& vtoc);
    bool allocateSector(uint8_t& track, uint8_t& sector);
    bool freeSector(uint8_t track, uint8_t sector);

    // Catalog operations
    int findFreeCatalogEntry();
    bool writeCatalogEntry(int index, const CatalogEntry& entry);

    // T/S List operations
    bool buildTSList(const std::vector<uint8_t>& data,
                     std::vector<TSPair>& tsList);

public:
    bool addFile(const std::string& hostPath,
                const std::string& targetName,
                const FileMetadata& meta) override;
};

class AppleProDOSHandler : public FileSystemHandler {
private:
    std::vector<uint8_t>& diskImage;

    // Block operations
    int findFreeBlock();
    bool markBlockUsed(int block);
    bool markBlockFree(int block);

    // Directory operations
    int findFreeDirectoryEntry(int dirBlock);
    bool writeDirectoryEntry(int dirBlock, int index,
                            const ProDOSEntry& entry);

    // Index block operations
    bool buildIndexBlocks(const std::vector<uint8_t>& data,
                         int& keyBlock, int& blocksUsed);

public:
    bool addFile(const std::string& hostPath,
                const std::string& targetName,
                const FileMetadata& meta) override;
};
```

#### 12.5.3 MSX Implementation

```cpp
class MSXDOSHandler : public FileSystemHandler {
private:
    std::vector<uint8_t>& diskImage;
    BootSector bootSector;

    // FAT operations
    uint16_t readFAT12(uint16_t cluster);
    void writeFAT12(uint16_t cluster, uint16_t value);
    uint16_t findFreeCluster();
    uint16_t findNextFreeCluster(uint16_t current);
    void freeClusterChain(uint16_t start);

    // Directory operations
    int findFreeDirEntry(int dirSector);
    bool writeDirEntry(int sector, int index, const MSXDirEntry& entry);

    // Cluster/Sector conversion
    int clusterToSector(uint16_t cluster);
    uint16_t sectorToCluster(int sector);

public:
    bool addFile(const std::string& hostPath,
                const std::string& targetName,
                const FileMetadata& meta) override;
};
```

---

### 12.6 CLI Commands for File Merge

#### 12.6.1 Add File Command

```bash
# Add binary file to Apple II ProDOS disk
$ rdedisktool merge game.po myprogram.bin MYPROGRAM \
    --type=BIN --load=0x2000 --exec=0x2000
Adding: myprogram.bin -> MYPROGRAM (BIN, load=$2000)
Allocated 12 blocks, file size: 5632 bytes
Done.

# Add binary file to Apple II DOS 3.3 disk
$ rdedisktool merge game.dsk hello.bin HELLO \
    --type=B --load=0x0803
Adding: hello.bin -> HELLO (Binary)
Allocated 3 sectors (T:18,S:0 -> T:18,S:2)
Done.

# Add file to MSX disk
$ rdedisktool merge game.dsk program.com PROGRAM.COM
Adding: program.com -> PROGRAM.COM
Allocated clusters: 2-5 (4 clusters, 4096 bytes)
Done.

# Add text file
$ rdedisktool merge disk.dsk readme.txt README.TXT --type=TXT
Adding: readme.txt -> README.TXT (Text)
Done.
```

#### 12.6.2 Batch Merge Command

```bash
# Merge multiple files from manifest
$ rdedisktool merge-batch game.po manifest.txt

# manifest.txt format:
# <host_file> <target_name> [options]
# myprogram.bin MYPROGRAM --type=BIN --load=0x2000
# data.dat DATA --type=BIN --load=0x4000
# readme.txt README --type=TXT
```

#### 12.6.3 Directory Sync Command

```bash
# Sync host directory to MSX disk (like DirAsDSK)
$ rdedisktool sync ./build/ game.dsk --mode=full
Syncing directory: ./build/ -> game.dsk
  Added: GAME.COM (8192 bytes)
  Added: DATA.DAT (1024 bytes)
  Updated: CONFIG.TXT (256 bytes)
Sync complete: 3 files, 9472 bytes total

# One-way sync (host -> disk only)
$ rdedisktool sync ./build/ game.dsk --mode=push
```

#### 12.6.4 Complete Command Reference

```
rdedisktool merge <disk_image> <host_file> <target_name> [options]

Options:
  --type=<type>      File type (platform-specific)
                     Apple II DOS 3.3: T, I, A, B, S, R
                     Apple II ProDOS: TXT, BIN, BAS, SYS, etc.
                     MSX: (auto-detected from extension)
  --load=<addr>      Load address (hex, e.g., 0x2000)
  --exec=<addr>      Execution address (hex, optional)
  --readonly         Mark file as read-only
  --hidden           Mark file as hidden
  --force            Overwrite if file exists
  --timestamp=<ts>   Set file timestamp (ISO 8601 or 'now')

rdedisktool merge-batch <disk_image> <manifest_file>

rdedisktool sync <host_dir> <disk_image> [options]

Options:
  --mode=<mode>      Sync mode: push, pull, full (default: push)
  --delete           Delete files not in source
  --exclude=<pat>    Exclude files matching pattern
```

---

### 12.7 Implementation Details

#### 12.7.1 Apple II DOS 3.3 File Addition Algorithm

```
1. Read VTOC (Track 17, Sector 0)
2. Find free Catalog entry
   - Start from Track 17, Sector 15
   - Follow chain until empty slot found
3. Calculate required sectors
   - Data sectors: ceil(fileSize / 256)
   - T/S List sectors: ceil(dataSectors / 122)
4. Allocate sectors
   - Use VTOC bitmap to find free sectors
   - Mark allocated sectors in VTOC
   - Build T/S List chain
5. Write file data
   - Copy data to allocated sectors
   - For Binary: prepend 2-byte load address + 2-byte length
6. Create Catalog entry
   - Set filename, type, first T/S List T/S
   - Set sector count
7. Write VTOC back
```

#### 12.7.2 Apple II ProDOS File Addition Algorithm

```
1. Read Volume Header (Block 2)
2. Find free directory entry
   - Scan directory blocks for empty slot (storage_type=0)
3. Determine storage type
   - ≤512 bytes: Seedling (type 0x1)
   - 513-131,072 bytes: Sapling (type 0x2)
   - >131,072 bytes: Tree (type 0x3)
4. Allocate blocks
   - Scan bitmap for free blocks
   - Mark blocks as used
   - For Sapling: create index block
   - For Tree: create master index + sub-index blocks
5. Write file data
   - Copy data to allocated blocks
   - Update index blocks
6. Create directory entry
   - Set storage_type, filename, file_type
   - Set aux_type (load address for BIN)
   - Set key_block, blocks_used, EOF
   - Set timestamps
7. Update volume header
   - Increment file count
```

#### 12.7.3 MSX-DOS File Addition Algorithm

```
1. Read Boot Sector
   - Parse BPB for disk geometry
2. Find free directory entry
   - Scan root directory (or subdirectory)
   - Look for entry starting with 0x00 or 0xE5
3. Calculate required clusters
   - clusters = ceil(fileSize / (sectorsPerCluster * 512))
4. Build FAT chain
   - Find first free cluster
   - For each additional cluster:
     - Find next free cluster
     - Link previous cluster to current
   - Mark last cluster as EOF (0xFFF)
5. Write file data
   - For each cluster:
     - Calculate sector number
     - Write data to sectors
6. Create directory entry
   - Convert filename to 8.3 format (uppercase)
   - Set start cluster
   - Set file size
   - Set timestamp (current or specified)
   - Set attributes
7. Write FAT to both copies
```

---

### 12.8 Cross-Toolchain Integration

#### 12.8.1 cc65 (Apple II)

```bash
# Build with cc65
$ cl65 -t apple2 -o myprogram.bin myprogram.c

# Merge to ProDOS disk
$ rdedisktool merge mydisk.po myprogram.bin MYPROGRAM \
    --type=BIN --load=0x803

# Or with DOS 3.3
$ rdedisktool merge mydisk.dsk myprogram.bin MYPROGRAM \
    --type=B --load=0x803
```

#### 12.8.2 SDCC/z88dk (MSX)

```bash
# Build with SDCC for MSX
$ sdcc -mz80 --code-loc 0x100 --data-loc 0 -o myprogram.ihx myprogram.c
$ hex2bin myprogram.ihx

# Or with z88dk
$ zcc +msx -create-app -o myprogram.com myprogram.c

# Merge to MSX disk
$ rdedisktool merge mydisk.dsk myprogram.com MYPROGRAM.COM
```

#### 12.8.3 Makefile Integration

```makefile
# Example Makefile for Apple II project
DISK = game.po
CC65 = cl65
RDEDISKTOOL = rdedisktool

all: $(DISK)

%.bin: %.c
    $(CC65) -t apple2 -o $@ $<

$(DISK): game.bin data.bin
    $(RDEDISKTOOL) create $(DISK) --format=po --size=140k
    $(RDEDISKTOOL) merge $(DISK) game.bin GAME --type=BIN --load=0x2000
    $(RDEDISKTOOL) merge $(DISK) data.bin DATA --type=BIN --load=0x4000

clean:
    rm -f *.bin $(DISK)
```

---

### 12.9 Development Phases for File Merge Feature

#### Phase A: Core Infrastructure ✅ COMPLETE
- [x] FileSystemHandler abstract interface
- [x] FileMetadata and FileEntry structures
- [x] Platform detection from disk image
- [x] Filename conversion utilities (8.3, ProDOS format)

#### Phase B: Apple II DOS 3.3 Support ✅ COMPLETE
- [x] VTOC read/write
- [x] Sector allocation/deallocation
- [x] Catalog entry management
- [x] T/S List chain building
- [x] Binary file header handling (load address)
- [x] `add` command for DOS 3.3
- [x] Extended validation (VTOC, catalog chain, T/S lists, bitmap consistency)

#### Phase C: Apple II ProDOS Support ✅ COMPLETE
- [x] Volume header read/write
- [x] Block bitmap management
- [x] Directory entry management
- [x] Index block building (Seedling/Sapling/Tree)
- [x] Timestamp conversion
- [x] `add` command for ProDOS
- [x] Subdirectory support (mkdir/rmdir)

#### Phase D: MSX-DOS Support ✅ COMPLETE
- [x] Boot sector parsing
- [x] FAT12 read/write
- [x] Cluster allocation/deallocation
- [x] Directory entry management (root + subdirectories)
- [x] DOS timestamp conversion
- [x] `add` command for MSX-DOS
- [x] Subdirectory support (mkdir/rmdir)

#### Phase E: Advanced Features ✅ COMPLETE
- [x] File extraction (`extract` command)
- [x] Subdirectory support (ProDOS, MSX-DOS)

#### Phase F: Testing & Validation ✅ COMPLETE
- [x] Integration tests with emulators (AppleWin, openMSX)
- [x] Edge cases (full disk, file deletion, rename)
- [x] ProDOS file deletion bug fixed
- [x] DOS 3.3 sector count validation fixed

---

### 12.10 Source Code References

#### 12.10.1 AppleWin References

| Feature | File | Key Functions |
|---------|------|---------------|
| ProDOS structures | `ProDOS_FileSystem.h` | Struct definitions |
| ProDOS file add | `ProDOS_Utils.cpp` | `Util_ProDOS_AddFile()` |
| Block allocation | `ProDOS_Utils.cpp` | `ProDOS_BlockGetFirstFree()` |
| Block marking | `ProDOS_Utils.cpp` | `ProDOS_BlockSetUsed()` |
| Index blocks | `ProDOS_Utils.cpp` | `ProDOS_PutIndexBlock()` |
| File header | `ProDOS_Utils.cpp` | `ProDOS_PutFileHeader()` |

#### 12.10.2 openMSX References

| Feature | File | Key Functions |
|---------|------|---------------|
| Boot sector | `DiskImageUtils.hh` | `MSXBootSector` struct |
| Directory entry | `DiskImageUtils.hh` | `MSXDirEntry` struct |
| FAT read/write | `DirAsDSK.cc` | `readFATHelper()`, `writeFATHelper()` |
| Cluster allocation | `DirAsDSK.cc` | `findFirstFreeCluster()` |
| File import | `DirAsDSK.cc` | `importHostFile()` |
| MSXtar file add | `MSXtar.cc` | `addFileToDSK()` |
| MSXtar FAT ops | `MSXtar.cc` | `readFAT()`, `writeFAT()` |
| File data copy | `MSXtar.cc` | `alterFileInDSK()` |

---

### 12.11 File Merge Module Structure

```
src/
├── filesystem/
│   ├── FileSystemHandler.h       # Abstract interface
│   ├── FileSystemHandler.cpp
│   ├── FileMetadata.h            # File metadata structures
│   ├── apple/
│   │   ├── AppleDOS33Handler.h
│   │   ├── AppleDOS33Handler.cpp # DOS 3.3 implementation
│   │   ├── AppleProDOSHandler.h
│   │   ├── AppleProDOSHandler.cpp # ProDOS implementation
│   │   ├── AppleDOS33Defs.h      # DOS 3.3 constants & structures
│   │   └── ProDOSDefs.h          # ProDOS constants & structures
│   └── msx/
│       ├── MSXDOSHandler.h
│       ├── MSXDOSHandler.cpp     # MSX-DOS implementation
│       ├── MSXFATUtils.h         # FAT12/FAT16 utilities
│       ├── MSXFATUtils.cpp
│       └── MSXDOSDefs.h          # MSX-DOS constants & structures
├── cli/
│   ├── MergeCommand.h
│   ├── MergeCommand.cpp          # merge command implementation
│   ├── MergeBatchCommand.h
│   ├── MergeBatchCommand.cpp     # merge-batch command
│   ├── SyncCommand.h
│   └── SyncCommand.cpp           # sync command implementation
└── utils/
    ├── FilenameConverter.h       # 8.3, ProDOS name conversion
    ├── FilenameConverter.cpp
    ├── TimestampUtils.h          # DOS/ProDOS timestamp conversion
    └── TimestampUtils.cpp
```

---

## 13. License

To be determined based on project requirements. Consider:
- MIT License (permissive)
- GPL v2/v3 (copyleft, matching AppleWin)
- BSD 3-Clause

---

*Document Version: 1.3*
*Created: 2025-12-31*
*Updated: 2025-12-31 - Binary name specified as `rdedisktool`*
*Updated: 2025-12-31 - Added File Merge Feature (Section 12) for cross-platform binary injection*
*Updated: 2026-01-02 - Updated implementation status (v1.3)*
  - All phases COMPLETE
  - XSA compression: Full bidirectional support implemented
  - Extended validation: DOS 3.3, ProDOS, MSX-DOS all implemented
  - Bug fixes: ProDOS file deletion, DOS 3.3 sector count validation
*Based on source code analysis of AppleWin and openMSX*
