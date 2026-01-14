# X68000 DIM/XDF 포맷 지원 구현 계획

## 개요

현재 RDE Disktool은 Apple II와 MSX 플랫폼을 지원합니다. 본 계획은 X68000 에뮬레이터에서 사용되는 DIM 및 XDF 디스크 이미지 포맷 지원을 추가하기 위한 구현 계획입니다.

**참조 소스**: `/home/onion/Workspace/x68000/px68k-onionmixer/x68k/` (px68k 에뮬레이터)

---

## 1. 포맷 분석

### 1.1 XDF (X68000 Disk Format)

**특징**: 헤더 없는 순수 섹터 덤프

| 항목 | 값 |
|------|-----|
| 파일 크기 | 1,261,568 바이트 (고정) |
| 섹터 크기 | 1024 바이트 (고정) |
| 트랙 당 섹터 | 8개 (고정) |
| 총 트랙 수 | 154개 (77 실린더 × 2 헤드) |
| 헤더 | 없음 |

**섹터 위치 계산**:
```cpp
size_t getXDFSectorOffset(size_t track, size_t sector) {
    return ((track * 8) + (sector - 1)) * 1024;
}
```

### 1.2 DIM (Disk Image Format)

**특징**: 256바이트 헤더 + 가변 크기 트랙 데이터

**헤더 구조** (256 바이트):
```cpp
#pragma pack(push, 1)
struct DIMHeader {
    uint8_t  type;           // 디스크 타입 (0-9)
    uint8_t  trkflag[170];   // 트랙 존재 플래그
    uint8_t  headerinfo[15]; // 헤더 정보
    uint8_t  date[4];        // 날짜
    uint8_t  time[4];        // 시간
    char     comment[61];    // 주석
    uint8_t  overtrack;      // 오버트랙 플래그
};
#pragma pack(pop)
```

**지원 디스크 타입**:

| 타입 | 이름 | 섹터 크기 | 섹터/트랙 | 트랙 크기 |
|------|------|----------|----------|----------|
| 0 | DIM_2HD | 1024 | 8 | 8192 |
| 1 | DIM_2HS | 1024 | 9 | 9216 |
| 2 | DIM_2HC | 512 | 15 | 7680 |
| 3 | DIM_2HDE | 1024 | 9 | 9216 |
| 9 | DIM_2HQ | 512 | 18 | 9216 |

**섹터 위치 계산**:
```cpp
size_t getDIMSectorOffset(DIMType type, size_t track, size_t sector) {
    static const size_t trackSizes[] = {
        1024*8, 1024*9, 512*15, 1024*9, 0, 0, 0, 0, 0, 512*18
    };
    size_t sectorSize = (type == DIM_2HC || type == DIM_2HQ) ? 512 : 1024;
    return 256 + (track * trackSizes[type]) + ((sector - 1) * sectorSize);
}
```

---

## 2. 구현 단계

### 2.1 Phase 1: 타입 시스템 확장

**파일**: `include/rdedisktool/Types.h`

```cpp
// Platform enum 추가
enum class Platform {
    Unknown,
    Apple2,
    MSX,
    X68000   // 추가
};

// DiskFormat enum 추가
enum class DiskFormat {
    // ... 기존 포맷들 ...
    X68000XDF,    // 추가: XDF 포맷
    X68000DIM,    // 추가: DIM 포맷
};

// X68000 DIM 타입 enum 추가
enum class X68000DIMType {
    DIM_2HD  = 0,   // 1024 bytes/sector, 8 sectors/track
    DIM_2HS  = 1,   // 1024 bytes/sector, 9 sectors/track
    DIM_2HC  = 2,   // 512 bytes/sector, 15 sectors/track
    DIM_2HDE = 3,   // 1024 bytes/sector, 9 sectors/track
    DIM_2HQ  = 9,   // 512 bytes/sector, 18 sectors/track
};
```

### 2.2 Phase 2: X68000 베이스 클래스

**새 파일**: `include/rdedisktool/x68000/X68000DiskImage.h`

```cpp
#ifndef RDEDISKTOOL_X68000DISKIMAGE_H
#define RDEDISKTOOL_X68000DISKIMAGE_H

#include "rdedisktool/DiskImage.h"

namespace rde {

class X68000DiskImage : public DiskImage {
public:
    Platform getPlatform() const override { return Platform::X68000; }

    // X68000 표준: 1024바이트 섹터를 2개의 512바이트 블록으로 매핑
    SectorBuffer readBlock(size_t blockNumber) override;
    void writeBlock(size_t blockNumber, const SectorBuffer& data) override;

protected:
    // CHRN (Cylinder, Head, Record, N) 섹터 어드레싱
    struct FDCID {
        uint8_t c;  // Cylinder (0-99)
        uint8_t h;  // Head (0-1)
        uint8_t r;  // Record/Sector (1-indexed)
        uint8_t n;  // Size code: 2=512, 3=1024
    };

    // 섹터 크기 계산
    static size_t sectorSizeFromN(uint8_t n) {
        return 128 << n;  // n=2: 512, n=3: 1024
    }

    std::vector<uint8_t> m_data;
    DiskGeometry m_geometry;
};

} // namespace rde
#endif
```

### 2.3 Phase 3: XDF 포맷 구현

**새 파일**: `include/rdedisktool/x68000/X68000XDFImage.h`

```cpp
#ifndef RDEDISKTOOL_X68000XDFIMAGE_H
#define RDEDISKTOOL_X68000XDFIMAGE_H

#include "rdedisktool/x68000/X68000DiskImage.h"

namespace rde {

class X68000XDFImage : public X68000DiskImage {
public:
    static constexpr size_t XDF_FILE_SIZE = 1261568;
    static constexpr size_t XDF_SECTOR_SIZE = 1024;
    static constexpr size_t XDF_SECTORS_PER_TRACK = 8;
    static constexpr size_t XDF_TOTAL_TRACKS = 154;
    static constexpr size_t XDF_CYLINDERS = 77;

    X68000XDFImage();
    ~X68000XDFImage() override = default;

    // DiskImage interface
    void load(const std::filesystem::path& path) override;
    void save(const std::filesystem::path& path = {}) override;
    void create(const DiskGeometry& geometry) override;

    DiskFormat getFormat() const override { return DiskFormat::X68000XDF; }
    FileSystemType getFileSystemType() const override;
    DiskGeometry getGeometry() const override;
    bool isWriteProtected() const override;

    SectorBuffer readSector(size_t track, size_t side, size_t sector) override;
    void writeSector(size_t track, size_t side, size_t sector,
                    const SectorBuffer& data) override;

    TrackBuffer readTrack(size_t track, size_t side) override;
    void writeTrack(size_t track, size_t side, const TrackBuffer& data) override;

    bool canConvertTo(DiskFormat format) const override;
    std::unique_ptr<DiskImage> convertTo(DiskFormat format) const override;

    bool validate() const override;
    std::string getDiagnostics() const override;

private:
    size_t calculateOffset(size_t track, size_t sector) const;
    void validateParameters(size_t track, size_t sector) const;
};

} // namespace rde
#endif
```

**새 파일**: `src/x68000/X68000XDFImage.cpp`

```cpp
#include "rdedisktool/x68000/X68000XDFImage.h"
#include "rdedisktool/DiskImageFactory.h"
#include "rdedisktool/Exceptions.h"
#include <fstream>

namespace rde {

// 자동 등록
namespace {
    struct XDFRegistrar {
        XDFRegistrar() {
            DiskImageFactory::registerFormat(DiskFormat::X68000XDF,
                []() -> std::unique_ptr<DiskImage> {
                    return std::make_unique<X68000XDFImage>();
                });
        }
    };
    static XDFRegistrar registrar;
}

X68000XDFImage::X68000XDFImage() {
    m_geometry = {
        .tracks = XDF_TOTAL_TRACKS,
        .sides = 2,
        .sectorsPerTrack = XDF_SECTORS_PER_TRACK,
        .bytesPerSector = XDF_SECTOR_SIZE
    };
}

void X68000XDFImage::load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw FileNotFoundException(path.string());
    }

    m_data.resize(XDF_FILE_SIZE, 0xE5);
    file.read(reinterpret_cast<char*>(m_data.data()), XDF_FILE_SIZE);

    m_path = path;
    m_modified = false;
}

void X68000XDFImage::save(const std::filesystem::path& path) {
    auto savePath = path.empty() ? m_path : path;

    std::ofstream file(savePath, std::ios::binary);
    if (!file) {
        throw WriteException("Cannot open file for writing: " + savePath.string());
    }

    file.write(reinterpret_cast<const char*>(m_data.data()), m_data.size());
    m_path = savePath;
    m_modified = false;
}

size_t X68000XDFImage::calculateOffset(size_t track, size_t sector) const {
    return ((track * XDF_SECTORS_PER_TRACK) + (sector - 1)) * XDF_SECTOR_SIZE;
}

SectorBuffer X68000XDFImage::readSector(size_t track, size_t side, size_t sector) {
    // XDF는 linear track 사용: track = cylinder*2 + head
    size_t linearTrack = track;
    if (side != 0) {
        // side가 별도로 주어진 경우 처리
        linearTrack = track * 2 + side;
    }

    validateParameters(linearTrack, sector);

    size_t offset = calculateOffset(linearTrack, sector);
    SectorBuffer buffer(XDF_SECTOR_SIZE);
    std::copy(m_data.begin() + offset,
              m_data.begin() + offset + XDF_SECTOR_SIZE,
              buffer.begin());
    return buffer;
}

// ... 나머지 구현
} // namespace rde
```

### 2.4 Phase 4: DIM 포맷 구현

**새 파일**: `include/rdedisktool/x68000/X68000DIMImage.h`

```cpp
#ifndef RDEDISKTOOL_X68000DIMIMAGE_H
#define RDEDISKTOOL_X68000DIMIMAGE_H

#include "rdedisktool/x68000/X68000DiskImage.h"

namespace rde {

class X68000DIMImage : public X68000DiskImage {
public:
    static constexpr size_t DIM_HEADER_SIZE = 256;
    static constexpr size_t DIM_MAX_TRACKS = 170;

    X68000DIMImage();
    ~X68000DIMImage() override = default;

    // DiskImage interface
    void load(const std::filesystem::path& path) override;
    void save(const std::filesystem::path& path = {}) override;
    void create(const DiskGeometry& geometry) override;

    DiskFormat getFormat() const override { return DiskFormat::X68000DIM; }
    FileSystemType getFileSystemType() const override;
    DiskGeometry getGeometry() const override;
    bool isWriteProtected() const override;

    SectorBuffer readSector(size_t track, size_t side, size_t sector) override;
    void writeSector(size_t track, size_t side, size_t sector,
                    const SectorBuffer& data) override;

    TrackBuffer readTrack(size_t track, size_t side) override;
    void writeTrack(size_t track, size_t side, const TrackBuffer& data) override;

    bool canConvertTo(DiskFormat format) const override;
    std::unique_ptr<DiskImage> convertTo(DiskFormat format) const override;

    bool validate() const override;
    std::string getDiagnostics() const override;

    // DIM-specific
    X68000DIMType getDIMType() const { return m_dimType; }
    void setDIMType(X68000DIMType type);
    std::string getComment() const;
    void setComment(const std::string& comment);
    bool isTrackPresent(size_t track) const;

private:
    // DIM 헤더 구조
    #pragma pack(push, 1)
    struct DIMHeader {
        uint8_t  type;
        uint8_t  trkflag[170];
        uint8_t  headerinfo[15];
        uint8_t  date[4];
        uint8_t  time[4];
        char     comment[61];
        uint8_t  overtrack;
    };
    #pragma pack(pop)

    static_assert(sizeof(DIMHeader) == 256, "DIMHeader must be 256 bytes");

    DIMHeader m_header;
    X68000DIMType m_dimType;

    size_t getTrackSize() const;
    size_t getSectorSize() const;
    size_t getSectorsPerTrack() const;
    size_t calculateOffset(size_t track, size_t sector) const;
    void parseHeader();
    void buildHeader();

    static constexpr size_t TRACK_SIZES[10] = {
        1024*8,  // DIM_2HD
        1024*9,  // DIM_2HS
        512*15,  // DIM_2HC
        1024*9,  // DIM_2HDE
        0, 0, 0, 0, 0,
        512*18   // DIM_2HQ
    };
};

} // namespace rde
#endif
```

### 2.5 Phase 5: 포맷 감지 업데이트

**수정 파일**: `src/core/FormatDetector.cpp`

```cpp
DiskFormat FormatDetector::detectByExtension(const std::string& ext, size_t fileSize) {
    std::string lowerExt = toLower(ext);

    // X68000 formats
    if (lowerExt == ".xdf") {
        if (fileSize == 1261568) {
            return DiskFormat::X68000XDF;
        }
    }
    if (lowerExt == ".dim") {
        return DiskFormat::X68000DIM;
    }

    // ... 기존 코드 ...
}

DiskFormat FormatDetector::detectByContent(const std::vector<uint8_t>& data) {
    // XDF: 헤더 없음, 고정 크기로 판별
    if (data.size() == 1261568) {
        // X68000 IPL 시그니처 확인 (옵션)
        // 첫 섹터가 X68000 부트 섹터인지 확인
        if (data.size() >= 4 && data[0] == 0x60) {  // JMP 명령
            return DiskFormat::X68000XDF;
        }
    }

    // DIM: 256바이트 헤더의 type 필드 확인
    if (data.size() > 256) {
        uint8_t dimType = data[0];
        if (dimType <= 9 && (dimType <= 3 || dimType == 9)) {
            // 트랙 플래그 유효성 검사
            bool validFlags = true;
            for (int i = 0; i < 170 && validFlags; i++) {
                if (data[1 + i] > 1) validFlags = false;
            }
            if (validFlags) {
                return DiskFormat::X68000DIM;
            }
        }
    }

    // ... 기존 코드 ...
}
```

### 2.6 Phase 6: CMake 업데이트

**수정 파일**: `CMakeLists.txt`

```cmake
# X68000 소스 파일 추가
set(X68000_SOURCES
    src/x68000/X68000DiskImage.cpp
    src/x68000/X68000XDFImage.cpp
    src/x68000/X68000DIMImage.cpp
)

# 라이브러리에 추가
add_library(rdedisktool_lib STATIC
    ${CORE_SOURCES}
    ${APPLE_SOURCES}
    ${MSX_SOURCES}
    ${X68000_SOURCES}    # 추가
    ${FILESYSTEM_SOURCES}
    ${UTILS_SOURCES}
)

# 헤더 경로 추가
target_include_directories(rdedisktool_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
```

### 2.7 Phase 7: CLI 지원 추가

**수정 파일**: `src/cli/CLI.cpp`

```cpp
DiskFormat formatFromString(const std::string& s) {
    std::string lower = toLower(s);

    // X68000 formats
    if (lower == "xdf" || lower == "x68000xdf") return DiskFormat::X68000XDF;
    if (lower == "dim" || lower == "x68000dim") return DiskFormat::X68000DIM;

    // ... 기존 코드 ...
}

void printSupportedFormats() {
    std::cout << "Supported formats:\n";
    // ...
    std::cout << "  X68000: XDF, DIM\n";
}
```

---

## 3. 파일 구조

```
include/rdedisktool/
├── x68000/                          # 새 디렉토리
│   ├── X68000DiskImage.h            # X68000 베이스 클래스
│   ├── X68000XDFImage.h             # XDF 포맷
│   ├── X68000DIMImage.h             # DIM 포맷
│   └── X68000Constants.h            # X68000 상수 정의

src/
├── x68000/                          # 새 디렉토리
│   ├── X68000DiskImage.cpp
│   ├── X68000XDFImage.cpp
│   └── X68000DIMImage.cpp
```

---

## 4. 변환 지원 매트릭스

| From \ To | XDF | DIM |
|-----------|-----|-----|
| **XDF** | - | ✓ (2HD 타입으로) |
| **DIM** | ✓ (2HD 타입만) | - |

**변환 조건**:
- XDF → DIM: XDF 데이터를 DIM_2HD 형식으로 변환 (동일한 섹터 레이아웃)
- DIM → XDF: DIM_2HD 타입만 변환 가능 (다른 타입은 섹터 수 불일치)

---

## 5. 테스트 계획

### 5.1 단위 테스트

```cpp
// XDF 테스트
TEST(X68000XDFImage, LoadValidFile) {
    X68000XDFImage img;
    EXPECT_NO_THROW(img.load("test.xdf"));
    EXPECT_EQ(img.getGeometry().tracks, 154);
}

TEST(X68000XDFImage, ReadWriteSector) {
    X68000XDFImage img;
    img.create({});

    SectorBuffer data(1024, 0xAA);
    img.writeSector(0, 0, 1, data);

    auto read = img.readSector(0, 0, 1);
    EXPECT_EQ(read, data);
}

// DIM 테스트
TEST(X68000DIMImage, ParseHeader) {
    X68000DIMImage img;
    img.load("test.dim");
    EXPECT_EQ(img.getDIMType(), X68000DIMType::DIM_2HD);
}

TEST(X68000DIMImage, TrackFlags) {
    X68000DIMImage img;
    img.load("sparse.dim");
    EXPECT_TRUE(img.isTrackPresent(0));
    EXPECT_FALSE(img.isTrackPresent(100));
}
```

### 5.2 통합 테스트

- 실제 X68000 디스크 이미지 로드/저장 테스트
- px68k 에뮬레이터에서 생성된 이미지 호환성 테스트
- 포맷 변환 테스트 (XDF ↔ DIM)

---

## 6. 구현 우선순위

1. **Phase 1-2**: 타입 시스템 및 베이스 클래스 (기반 작업)
2. **Phase 3**: XDF 포맷 (단순한 구조, 먼저 구현)
3. **Phase 4**: DIM 포맷 (복잡한 헤더 처리)
4. **Phase 5**: 포맷 감지 (자동 인식)
5. **Phase 6-7**: 빌드 시스템 및 CLI (통합)

---

## 7. 참고 자료

### 소스 코드 참조
- DIM 구현: `/home/onion/Workspace/x68000/px68k-onionmixer/x68k/disk_dim.c`
- XDF 구현: `/home/onion/Workspace/x68000/px68k-onionmixer/x68k/disk_xdf.c`
- FDD 드라이버: `/home/onion/Workspace/x68000/px68k-onionmixer/x68k/fdd.c`

### 주요 상수
```cpp
// XDF
constexpr size_t XDF_FILE_SIZE = 1261568;
constexpr size_t XDF_SECTOR_SIZE = 1024;
constexpr size_t XDF_SECTORS_PER_TRACK = 8;
constexpr size_t XDF_TOTAL_TRACKS = 154;

// DIM
constexpr size_t DIM_HEADER_SIZE = 256;
constexpr size_t DIM_MAX_TRACKS = 170;
```

---

## 8. 향후 확장 가능성

1. **D88 포맷 지원**: X68000에서 사용되는 또 다른 포맷
2. **Human68k 파일시스템 핸들러**: X68000 OS 파일시스템 지원
3. **FDI/HDM 포맷**: 다른 X68000 에뮬레이터 포맷 지원

---

*작성일: 2026-01-15*
*참조 에뮬레이터: px68k-onionmixer*
