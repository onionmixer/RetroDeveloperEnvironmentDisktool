# FIX_DELETE_FILE_LOGIC.md

파일/디렉토리 삭제 로직의 잔여 데이터(residual data) 문제 분석 보고서.

기준 커밋: `59ace7a` — ProDOS deleteFile에서 디렉토리 엔트리 전체를 0으로 초기화하도록 수정.

---

## 1. 원래 버그 (ProDOS deleteFile — 수정 완료)

### 증상
ProDOS 부트 디스크에서 파일을 삭제한 뒤 "Unable to load ATInit file" 부팅 오류 발생.

### 원인
`deleteFile()`에서 `DirectoryEntry deletedEntry = entry;` 로 기존 엔트리를 복사한 뒤
`storageType = STORAGE_DELETED (0)` 만 설정했다. 그러나 `nameLength`는 원본 값이 그대로
남아 있었기 때문에 디스크에 기록되는 첫 바이트가:

```
block[offset] = (storageType << 4) | (nameLength & 0x0F)
              = (0 << 4) | (n)
              = 0x0n   ← 0x00이 아님!
```

ProDOS 2.4.3의 startup 파일 검색 루틴이 `0x0n`을 유효한 엔트리로 오인하여 오류 발생.

### 수정 내용 (59ace7a)
```cpp
DirectoryEntry deletedEntry;
std::memset(&deletedEntry, 0, sizeof(deletedEntry));
deletedEntry.storageType = STORAGE_DELETED;
deletedEntry.nameLength = 0;
```

엔트리 전체를 0으로 초기화 → byte[0] = `0x00` 보장.

---

## 2. ProDOS deleteDirectory — 동일 버그 존재 (미수정)

**파일**: `src/filesystem/apple/AppleProDOSHandler.cpp:1565`

```cpp
// Mark directory entry as deleted
DirectoryEntry deletedEntry = entry;           // ← 기존 엔트리 복사 (nameLength 잔존)
deletedEntry.storageType = STORAGE_DELETED;    // ← nameLength 미초기화

if (!writeDirectoryEntry(parentBlock, entryIndex, deletedEntry)) {
```

### 문제
`deleteFile`과 동일한 패턴. 삭제된 서브디렉토리 엔트리의 byte[0]이 `0x0n`이 되어
ProDOS가 유효한 엔트리로 오인할 수 있다.

### 수정 방안
`deleteFile`과 동일하게 memset 적용:

```cpp
DirectoryEntry deletedEntry;
std::memset(&deletedEntry, 0, sizeof(deletedEntry));
deletedEntry.storageType = STORAGE_DELETED;
deletedEntry.nameLength = 0;

if (!writeDirectoryEntry(parentBlock, entryIndex, deletedEntry)) {
```

### 심각도: **높음**
부트 디스크에서 서브디렉토리를 삭제하면 deleteFile과 동일한 부팅 오류가 재현될 수 있다.

---

## 3. Apple DOS 3.3 deleteFile — 문제 없음

**파일**: `src/filesystem/apple/AppleDOS33Handler.cpp:730-739`

```cpp
sectorData[offset + 3] = entry.trackSectorListTrack;  // 원본 T/S track 보존 (복구용)
sectorData[offset] = FLAG_DELETED;   // 0xFF로 설정
sectorData[offset + 1] = 0;         // T/S list sector 초기화
```

### 분석
DOS 3.3의 표준 삭제 프로토콜을 정확히 따르고 있다:
- `trackSectorListTrack = 0xFF`가 삭제 마커 (명확하고 유일한 판정 기준)
- filename[0]에 원본 T/S track 저장 → UNDELETE 지원
- fileType, filename(1-29), sectorCount는 의도적으로 보존

DOS 3.3은 `trackSectorListTrack == 0xFF`만으로 삭제 판정하므로, 잔여 데이터가 OS를
혼란시킬 가능성 없음. ProDOS와 달리 복합 필드(storageType|nameLength)를 사용하지 않아
비트 마스킹 오류 패턴이 구조적으로 발생할 수 없다.

### 상태: **안전**

---

## 4. MSX-DOS deleteFile — 문제 없음 (FAT12 표준 준수)

**파일**: `src/filesystem/msx/MSXDOSHandler.cpp:770-773`

```cpp
entry.name[0] = static_cast<char>(DIR_FREE);   // 0xE5
setDirectoryEntries(dirCluster, entries);
```

### 분석
FAT12 표준 삭제 방식: `name[0] = 0xE5`만 설정, 나머지 필드 보존.

보존되는 필드:
| 필드 | 값 | 위험도 |
|------|------|--------|
| name[1-7], ext[3] | 원본 파일명 | 없음 — 0xE5 마커가 우선 |
| attr | 원본 속성 | 없음 |
| time, date | 원본 타임스탬프 | 없음 |
| startCluster | 해제된 클러스터 번호 | 없음 (UNDELETE용) |
| fileSize | 원본 파일 크기 | 없음 (UNDELETE용) |

MSX-DOS, MSX-DOS2, MS-DOS 모두 `name[0] == 0xE5`를 삭제 마커로 인식하며, 이 값은 단일
바이트 비교로 판정된다(ProDOS의 복합 니블 필드와 다름). 잔여 데이터로 인한 OS 혼란
가능성 없음.

새 파일 기록 시 `writeRootDirectory()` / `setDirectoryEntries()`가 전체 32바이트를
덮어쓰므로 잔여 데이터가 누출되지 않는다.

### 상태: **안전**

---

## 5. MSX-DOS deleteDirectory — 문제 없음

**파일**: `src/filesystem/msx/MSXDOSHandler.cpp:1333-1334`

```cpp
entry.name[0] = static_cast<char>(DIR_FREE);   // 0xE5
```

deleteFile과 동일한 FAT12 표준 삭제 방식. 동일 이유로 안전.

### 상태: **안전**

---

## 6. Human68k deleteFile — 문제 없음 (FAT12 표준 준수)

**파일**: `src/filesystem/x68000/Human68kHandler.cpp:636`

```cpp
entry.name[0] = static_cast<char>(DIR_FREE);   // 0xE5
```

Human68k는 FAT12 기반이며 MSX-DOS와 동일한 디렉토리 엔트리 구조(32바이트)를 사용한다.
삭제 마커도 동일하게 `0xE5`. `writeRootDirectory()`는 `memcpy`로 구조체 전체를 기록하므로
잔여 데이터 문제 없음.

### 상태: **안전**

---

## 7. Human68k deleteDirectory — 문제 없음

**파일**: `src/filesystem/x68000/Human68kHandler.cpp:982`

```cpp
parentEntries[idx].name[0] = static_cast<char>(DIR_FREE);
```

deleteFile과 동일한 패턴. 동일 이유로 안전.

### 상태: **안전**

---

## 요약

| 핸들러 | 함수 | 상태 | 비고 |
|--------|------|------|------|
| ProDOS | deleteFile | **수정 완료** (59ace7a) | memset 전체 초기화 |
| ProDOS | deleteDirectory | **버그 — 수정 필요** | deleteFile과 동일한 nameLength 잔존 문제 |
| DOS 3.3 | deleteFile | 안전 | 0xFF 마커는 단일 바이트 판정 |
| MSX-DOS | deleteFile | 안전 | 0xE5 마커는 FAT12 표준 |
| MSX-DOS | deleteDirectory | 안전 | 동일 |
| Human68k | deleteFile | 안전 | 0xE5 마커는 FAT12 표준 |
| Human68k | deleteDirectory | 안전 | 동일 |

### 핵심 원인 분석

ProDOS에서만 문제가 발생하는 근본 이유는 디렉토리 엔트리 byte[0]의 인코딩 방식 차이:

- **ProDOS**: byte[0] = `(storageType << 4) | nameLength` — 두 필드가 하나의 바이트에 패킹됨.
  storageType을 0으로 설정해도 nameLength가 남아있으면 byte[0] ≠ 0x00.
- **FAT12 (MSX-DOS, Human68k)**: byte[0] = `name[0]` — 단일 값 `0xE5`로 덮어쓰기.
  다른 필드와 독립적이므로 부분 초기화 문제가 구조적으로 불가능.
- **DOS 3.3**: byte[0] = `trackSectorListTrack` — 단일 값 `0xFF`로 덮어쓰기.
  마찬가지로 다른 필드와 독립적.

따라서 ProDOS의 `deleteDirectory`만 긴급 수정이 필요하다.
