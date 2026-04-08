# FIX_RENAMEFILE_METHOD.md

각 파일시스템 핸들러의 `renameFile()` 메서드에서 디렉토리 rename을 완전하게
지원하기 위한 심층 검토 및 개선 방안.

---

## 현재 상태 요약

| 핸들러 | 파일 rename | 서브디렉토리 내 항목 | 디렉토리 자체 | 동일 이름 | 에러 방식 | 문제 |
|--------|:---------:|:------------------:|:----------:|:-------:|:--------:|:----:|
| DOS 3.3 | O | N/A | N/A | 실패(기존) | return false | 없음 |
| ProDOS | O | O | 불완전 | 실패(기존) | return false | 3건+D |
| MSX-DOS | O | O | O | 성공(no-op) | throw 예외 | 없음 |
| Human68k | O (루트만) | X | O (루트만) | 실패(기존) | return false | 4건 |

---

## 1. Apple DOS 3.3 — 변경 불필요

**파일**: `src/filesystem/apple/AppleDOS33Handler.cpp:759`

DOS 3.3은 디렉토리 개념이 없는 플랫 파일시스템이다.

### 현재 코드 (759-803행)

```cpp
bool AppleDOS33Handler::renameFile(const std::string& oldName, const std::string& newName) {
    int index = findCatalogEntry(oldName);
    if (index < 0) { return false; }
    if (findCatalogEntry(newName) >= 0) { return false; }  // 중복 검사

    // 카탈로그 순회하며 해당 엔트리의 filename 30바이트 교체
    // ...
    char newFilename[30];
    parseFilename(newName, newFilename);
    std::memcpy(&sectorData[entryOffset + 3], newFilename, 30);
    writeSector(catTrack, catSector, sectorData);
    return true;
}
```

**검증 결과:**
- 중복 이름 검사: O (766행)
- 삭제된 엔트리 보호: O — `findCatalogEntry()`가 `FLAG_DELETED (0xFF)` 스킵 (196행)
- 엔트리 전체 35바이트 중 filename 30바이트만 교체: O — track/sector/fileType/sectorCount 보존
- 디스크 쓰기 원자성: O — 단일 `writeSector()` 호출

### 결론: 수정 불필요

---

## 2. Apple ProDOS — 3건의 문제 발견

**파일**: `src/filesystem/apple/AppleProDOSHandler.cpp:1234`

### 현재 코드 (1234-1274행)

```cpp
bool AppleProDOSHandler::renameFile(const std::string& oldName, const std::string& newName) {
    auto [newDirBlock, newFileName] = resolvePath(newName);      // ①
    if (newDirBlock == 0 && newFileName.empty()) {
        newDirBlock = VOLUME_DIR_BLOCK;
        newFileName = newName;
    }
    if (!isValidFilename(newFileName)) { return false; }

    auto [dirBlock, name] = resolvePath(oldName);                // ②
    if (dirBlock == 0 && name.empty()) {
        dirBlock = VOLUME_DIR_BLOCK;
        name = oldName;
    }

    if (findDirectoryEntry(newDirBlock, newFileName) >= 0) {     // ③ 중복 검사
        return false;
    }

    int entryIndex = findDirectoryEntry(dirBlock, name);         // ④ 소스 검색
    if (entryIndex < 0) { return false; }

    auto entryOpt = readDirectoryEntryAt(dirBlock, entryIndex);  // ⑤
    if (!entryOpt) { return false; }

    DirectoryEntry entry = *entryOpt;
    parseFilename(newFileName, entry.filename, entry.nameLength); // ⑥ 이름 변경
    entry.lastModDateTime = packDateTime(std::time(nullptr));

    return writeDirectoryEntry(dirBlock, entryIndex, entry);     // ⑦ dirBlock에 기록
}
```

### 문제 A: cross-directory rename 시 중복 검사 오류 (심각도: 중간)

**시나리오**: `renameFile("/DIR1/FILE", "/DIR2/NEWFILE")`

| 단계 | 동작 | 대상 디렉토리 |
|------|------|-------------|
| ① resolvePath(newName) | `{DIR2_block, "NEWFILE"}` | DIR2 |
| ② resolvePath(oldName) | `{DIR1_block, "FILE"}` | DIR1 |
| ③ 중복 검사 | `findDirectoryEntry(DIR2_block, "NEWFILE")` | **DIR2에서 검사** |
| ⑦ 쓰기 | `writeDirectoryEntry(DIR1_block, ...)` | **DIR1에 기록** |

중복 검사(③)는 **DIR2**에서 수행되지만, 실제 쓰기(⑦)는 **DIR1**에 수행된다.

**결과:**
- DIR1에 이미 "NEWFILE"이 있어도 중복 검사를 통과하여 **같은 디렉토리에 동명 엔트리 생성**
- 사용자는 파일이 DIR2로 이동했다고 오해하지만 실제로는 DIR1에서 이름만 변경됨

**수정 방안:**
```cpp
// ② 직후, ③ 이전에 같은 디렉토리 검증 추가
if (dirBlock != newDirBlock) {
    return false;  // cross-directory rename(=move)은 미지원
}
```

이 검사를 추가하면 ③의 중복 검사가 자동으로 올바른 디렉토리(dirBlock)를 대상으로 수행됨.
MSX-DOS의 `oldDirCluster != newDirCluster` 검사(791행)와 동일한 패턴.

### 문제 B: 서브디렉토리 헤더 이름 미갱신 (심각도: 높음)

ProDOS 서브디렉토리 블록 레이아웃 (`createDirectory()`의 1431-1483행에서 확인):

```
블록 구조 (512바이트):
  [0x00-0x01] prev block pointer (LE)
  [0x02-0x03] next block pointer (LE)
  [0x04]      (STORAGE_SUBDIR_HEADER << 4) | nameLength    ← 서브디렉토리 헤더 시작
  [0x05-0x13] 디렉토리 이름 (대문자, 최대 15바이트)
  [0x14-0x1B] reserved
  [0x1C-0x1F] creation date/time
  [0x20]      version
  [0x21]      min version
  [0x22]      access
  [0x23]      entry length (0x27)
  [0x24]      entries per block (0x0D)
  [0x25-0x26] file count (LE)                              ← 절대 건드리면 안 됨
  [0x27-0x28] parent block pointer (LE)                    ← 절대 건드리면 안 됨
  [0x29]      parent entry number                          ← 절대 건드리면 안 됨
  [0x2A]      parent entry length (0x27)                   ← 절대 건드리면 안 됨
```

현재 `renameFile()`은 부모 엔트리(⑦)만 갱신하고 이 헤더는 갱신하지 않음.

**수정 방안 (⑦ 직후 추가):**

```cpp
if (!writeDirectoryEntry(dirBlock, entryIndex, entry)) {
    return false;
}

// 서브디렉토리인 경우: 자체 블록의 헤더 이름도 동기화
if (entry.storageType == STORAGE_SUBDIRECTORY) {
    auto block = readBlock(entry.keyPointer);
    if (block.size() >= BLOCK_SIZE) {
        // 헤더의 storageType|nameLength 바이트 (block[0x04])
        block[0x04] = (STORAGE_SUBDIR_HEADER << 4) | (entry.nameLength & 0x0F);
        // 이름 복사 (parseFilename이 이미 대문자 변환 완료)
        std::memcpy(&block[0x05], entry.filename, entry.nameLength);
        // 나머지 패딩 (기존 이름이 더 길었을 경우 잔여 바이트 제거)
        for (size_t i = entry.nameLength; i < MAX_FILENAME_LENGTH; ++i) {
            block[0x05 + i] = 0;
        }
        writeBlock(entry.keyPointer, block);
    }
}

return true;
```

**건드리지 않는 필드 검증:**
- `block[0x25-0x26]` (file count): 읽기→쓰기 사이에 수정 없음 ✓
- `block[0x27-0x28]` (parent pointer): 수정 없음 ✓
- `block[0x29]` (parent entry number): rename은 물리 위치를 바꾸지 않으므로 변경 불필요 ✓
- `block[0x1C-0x1F]` (creation date): 수정 없음 ✓

**`writeDirectoryEntry`(345행)와의 일관성:**
수정 코드는 `writeDirectoryEntry`의 이름 기록 패턴과 동일:
```cpp
// writeDirectoryEntry (346-350행):
std::memcpy(&block[offset + 1], entry.filename, entry.nameLength);
for (size_t j = entry.nameLength; j < MAX_FILENAME_LENGTH; ++j) {
    block[offset + 1 + j] = 0;
}
```

### 문제 C: ACCESS_RENAME 비트 미검사 (심각도: 낮음, 별도 이슈)

ProDOS는 `ACCESS_RENAME = 0x40` (AppleConstants.h:132) 비트로 rename 가능 여부를
제어한다. 현재 `renameFile()`은 이 비트를 검사하지 않는다. `deleteFile()`도
`ACCESS_DESTROY`를 검사하지 않으므로 일관된 동작이지만, 향후 access 비트 지원 시
함께 추가해야 한다.

**참고**: 이 문제는 디렉토리 rename 수정과 독립적이므로 별도 이슈로 분리한다.

### 추가 검증: 멀티블록 디렉토리 안전성

ProDOS 서브디렉토리는 linked block으로 확장될 수 있다 (block[0x00-0x01]=prev,
block[0x02-0x03]=next). `readDirectory()` (264행)에서 확인:

```cpp
size_t startEntry = firstBlock ? 1 : 0;  // 첫 블록만 헤더(entry[0]) 스킵
```

서브디렉토리 헤더는 **항상 keyBlock(첫 블록)의 offset 0x04에만 존재**한다.
후속 linked block에는 헤더가 없다. 따라서 `readBlock(entry.keyPointer)`는
블록 수에 관계없이 항상 올바른 헤더 위치를 읽는다.

### 추가 검증: validateExtended()와의 관계

`validateExtended()` (1606행)은 서브디렉토리를 재귀 순회하지만 **헤더 이름과 부모
엔트리 이름의 일치 여부는 검증하지 않는다** (1668행에서 `isSubdirHeader()` 스킵).
따라서 이름 불일치가 발생해도 검증 명령으로 탐지할 수 없다 → 수정의 중요도가 높다.

### 추가 검증: 볼륨명 접두사 경로

`resolvePath()`는 볼륨명 접두사(`/VOLNAME/path`)를 처리한다 (830-836행).
`renameFile("/VOL/DIR/F", "/VOL/DIR/G")`에서 양측 모두 볼륨명이 제거된 후
같은 dirBlock으로 해석되므로 cross-directory 검사를 정상 통과한다.
단, 볼륨명 비교는 대소문자를 구분하므로 소문자 볼륨명(`/vol/path`)은
`resolvePath`가 실패하여 폴백 경로를 타지만 데이터 훼손은 발생하지 않는다.

### 추가 검증: resolvePath 폴백 경로의 안전성

`resolvePath`가 `{0, ""}` 반환 시 폴백으로 `name = oldName` (전체 경로)이 설정된다.

```
renameFile("BAD/PATH", "BAD/NEW") → resolvePath 실패 →
  newFileName = "BAD/NEW" → isValidFilename("BAD/NEW") → '/' 불허 → false 반환 ✓
```

ProDOS 파일명에 `/`가 허용되지 않으므로, 폴백 경로에서 `isValidFilename()`이
경로 구분자를 포함한 이름을 항상 거부한다. 데이터 훼손 경로 없음.

### 추가 검증: 동일 이름 rename (기존 동작)

`renameFile("FILE", "FILE")` → 중복 검사에서 `findDirectoryEntry(dirBlock, "FILE") >= 0`
→ return false. 이는 **기존 코드에서도 동일한 동작**이며 수정에 의한 변경이 아니다.

MSX-DOS는 `newIndex >= 0 && newIndex != oldIndex` 패턴으로 동일 엔트리를 허용하지만,
ProDOS는 중복 검사와 소스 검색이 별도 호출이다. 선택적 개선안:

```cpp
// [선택적 FIX D] 동일 이름 rename 허용 (MSX-DOS 방식)
int newEntryIndex = findDirectoryEntry(dirBlock, newFileName);
int entryIndex = findDirectoryEntry(dirBlock, name);
if (entryIndex < 0) { return false; }
if (newEntryIndex >= 0 && newEntryIndex != entryIndex) { return false; }
```

이 변경은 안전하다: ProDOS `findDirectoryEntry`는 물리 인덱스를 반환하며,
cross-directory 검사 통과 후에는 양측이 동일 디렉토리를 검색하므로 인덱스 비교가 유효.
단, 기존 동작 변경이므로 별도 판단 필요.

### 추가 검증: 부분 기록 일관성

`writeBlock`은 void 함수(66행)이며 실패 시 조용히 무시된다. 수정 코드에서
`writeDirectoryEntry` 성공 후 서브디렉토리 헤더 `writeBlock`이 실패하면 부모-헤더 간
이름 불일치가 발생할 수 있다. 그러나:
- `writeDirectoryEntry` 자체도 내부적으로 `writeBlock`을 호출하므로 동일한 전제
- 기존 코드는 헤더를 전혀 갱신하지 않으므로 최악의 경우도 기존과 동일
- 정상 디스크에서 유효한 `entry.keyPointer`에 대한 writeBlock 실패는 발생하지 않음

### 수정 코드 전체 (통합)

```cpp
bool AppleProDOSHandler::renameFile(const std::string& oldName, const std::string& newName) {
    // 새 이름 경로 해석
    auto [newDirBlock, newFileName] = resolvePath(newName);
    if (newDirBlock == 0 && newFileName.empty()) {
        newDirBlock = VOLUME_DIR_BLOCK;
        newFileName = newName;
    }

    if (!isValidFilename(newFileName)) {
        return false;
    }

    // 기존 이름 경로 해석
    auto [dirBlock, name] = resolvePath(oldName);
    if (dirBlock == 0 && name.empty()) {
        dirBlock = VOLUME_DIR_BLOCK;
        name = oldName;
    }

    // [FIX A] cross-directory rename 방지
    if (dirBlock != newDirBlock) {
        return false;
    }

    // 새 이름 중복 검사 (이제 dirBlock == newDirBlock이므로 올바른 디렉토리에서 검사)
    if (findDirectoryEntry(dirBlock, newFileName) >= 0) {
        return false;
    }

    int entryIndex = findDirectoryEntry(dirBlock, name);
    if (entryIndex < 0) {
        return false;
    }

    auto entryOpt = readDirectoryEntryAt(dirBlock, static_cast<size_t>(entryIndex));
    if (!entryOpt) {
        return false;
    }

    DirectoryEntry entry = *entryOpt;
    parseFilename(newFileName, entry.filename, entry.nameLength);
    entry.lastModDateTime = packDateTime(std::time(nullptr));

    if (!writeDirectoryEntry(dirBlock, entryIndex, entry)) {
        return false;
    }

    // [FIX B] 서브디렉토리인 경우 헤더 이름 동기화
    if (entry.storageType == STORAGE_SUBDIRECTORY) {
        auto block = readBlock(entry.keyPointer);
        if (block.size() >= BLOCK_SIZE) {
            block[0x04] = (STORAGE_SUBDIR_HEADER << 4) | (entry.nameLength & 0x0F);
            std::memcpy(&block[0x05], entry.filename, entry.nameLength);
            for (size_t i = entry.nameLength; i < MAX_FILENAME_LENGTH; ++i) {
                block[0x05 + i] = 0;
            }
            writeBlock(entry.keyPointer, block);
        }
    }

    return true;
}
```

### 컴파일 요건
- 추가 include 불필요: `std::memcpy`(`<cstring>`), `readBlock`/`writeBlock` 모두 기존 포함
- 사용 타입: `uint8_t`, `size_t`, `uint16_t` — 기존 코드와 동일
- 상수: `STORAGE_SUBDIRECTORY(0x0D)`, `STORAGE_SUBDIR_HEADER(0x0E)`, `MAX_FILENAME_LENGTH(15)`,
  `BLOCK_SIZE(512)` — 모두 클래스 멤버 또는 AppleConstants.h에 정의

### 기존 코드 대비 diff

```diff
 bool AppleProDOSHandler::renameFile(const std::string& oldName, const std::string& newName) {
     auto [newDirBlock, newFileName] = resolvePath(newName);
     if (newDirBlock == 0 && newFileName.empty()) {
         newDirBlock = VOLUME_DIR_BLOCK;
         newFileName = newName;
     }
     if (!isValidFilename(newFileName)) { return false; }

     auto [dirBlock, name] = resolvePath(oldName);
     if (dirBlock == 0 && name.empty()) {
         dirBlock = VOLUME_DIR_BLOCK;
         name = oldName;
     }

+    // [FIX A] cross-directory rename 방지
+    if (dirBlock != newDirBlock) {
+        return false;
+    }
+
-    if (findDirectoryEntry(newDirBlock, newFileName) >= 0) {
+    if (findDirectoryEntry(dirBlock, newFileName) >= 0) {
         return false;
     }

     int entryIndex = findDirectoryEntry(dirBlock, name);
     if (entryIndex < 0) { return false; }

     auto entryOpt = readDirectoryEntryAt(dirBlock, static_cast<size_t>(entryIndex));
     if (!entryOpt) { return false; }

     DirectoryEntry entry = *entryOpt;
     parseFilename(newFileName, entry.filename, entry.nameLength);
     entry.lastModDateTime = packDateTime(std::time(nullptr));

-    return writeDirectoryEntry(dirBlock, entryIndex, entry);
+    if (!writeDirectoryEntry(dirBlock, entryIndex, entry)) {
+        return false;
+    }
+
+    // [FIX B] 서브디렉토리인 경우 헤더 이름 동기화
+    if (entry.storageType == STORAGE_SUBDIRECTORY) {
+        auto block = readBlock(entry.keyPointer);
+        if (block.size() >= BLOCK_SIZE) {
+            block[0x04] = (STORAGE_SUBDIR_HEADER << 4) | (entry.nameLength & 0x0F);
+            std::memcpy(&block[0x05], entry.filename, entry.nameLength);
+            for (size_t i = entry.nameLength; i < MAX_FILENAME_LENGTH; ++i) {
+                block[0x05 + i] = 0;
+            }
+            writeBlock(entry.keyPointer, block);
+        }
+    }
+
+    return true;
 }
```

### 변경 행 수
- 추가: 16행 (cross-directory 검사 3행 + writeDirectoryEntry 분리 3행 + 헤더 동기화 10행)
- 변경: 2행 (`newDirBlock` → `dirBlock`, `return writeDirectoryEntry` → `if (!writeDirectoryEntry)`)
- 삭제: 0행

---

## 3. MSX-DOS — 변경 불필요

**파일**: `src/filesystem/msx/MSXDOSHandler.cpp:777`

### 현재 코드 (777-810행)

```cpp
bool MSXDOSHandler::renameFile(const std::string& oldName, const std::string& newName) {
    auto [oldDirCluster, oldBaseName] = resolvePath(oldName);
    if (oldBaseName.empty()) { return false; }

    auto [newDirCluster, newBaseName] = resolvePath(newName);
    if (newBaseName.empty()) { return false; }

    if (oldDirCluster != newDirCluster) {
        return false;  // cross-directory move 미지원
    }

    auto entries = getDirectoryEntries(oldDirCluster);
    int oldIndex = findDirectoryEntry(entries, oldBaseName);
    int newIndex = findDirectoryEntry(entries, newBaseName);

    if (oldIndex < 0) { return false; }
    if (newIndex >= 0 && newIndex != oldIndex) { return false; }

    parseFilename(newBaseName, entries[oldIndex].name, entries[oldIndex].ext);
    setDirectoryEntries(oldDirCluster, entries);
    return true;
}
```

### 검증 항목

**resolvePath 동작 (979-1057행):**
MSX-DOS의 `resolvePath`는 마지막 컴포넌트를 항상 분리하여 `{부모 cluster, 대상 이름}`을
반환한다. 파일이든 디렉토리든 동일한 의미:

```
resolvePath("FILE.TXT")       → {0, "FILE.TXT"}       // 부모=root, 대상=FILE.TXT
resolvePath("SUBDIR")         → {0, "SUBDIR"}          // 부모=root, 대상=SUBDIR (디렉토리여도 동일)
resolvePath("DIR1/FILE.TXT")  → {DIR1_cluster, "FILE.TXT"}
resolvePath("DIR1/CHILD")     → {DIR1_cluster, "CHILD"}
```

**디렉토리 rename 안전성:**
- `findDirectoryEntry()` (383-409행): `ATTR_VOLUME_ID`만 스킵, 디렉토리 포함 검색 → 디렉토리를 찾을 수 있음
- FAT12의 `.`/`..` 엔트리: 고정 이름이며 자기/부모 startCluster만 보유 → 이름 변경에 영향 없음
- cross-directory 검증: `oldDirCluster != newDirCluster` → 명시적 거부

**중복 이름 검사:**
- `newIndex >= 0 && newIndex != oldIndex` → 같은 엔트리에 같은 이름을 쓰는 것은 허용 (no-op)
- 다른 엔트리가 이미 같은 이름을 가진 경우 거부

### 주의: 에러 처리 방식의 차이

MSX-DOS `resolvePath`는 중간 경로가 없으면 `FileNotFoundException`을 throw (1050행):
```cpp
if (idx < 0 || !(entries[idx].attr & ATTR_DIRECTORY)) {
    throw FileNotFoundException("Directory not found: " + component);
}
```

`renameFile`에서 이 예외를 catch하지 않으므로 호출자에 전파된다.
ProDOS/Human68k는 같은 상황에서 `return false`로 처리한다.
이는 기존 동작이며 수정 범위 밖이지만, CLI에서 rename 커맨드 추가 시 try-catch 필요.

### 결론: 수정 불필요. 파일 및 디렉토리 rename이 모두 안전하게 동작.

---

## 4. Human68k — 서브디렉토리 경로 지원 추가 필요

**파일**: `src/filesystem/x68000/Human68kHandler.cpp:644`

### 현재 코드 (644-662행)

```cpp
bool Human68kHandler::renameFile(const std::string& oldName, const std::string& newName) {
    auto entries = readRootDirectory();                           // ← 루트만
    int idx = findDirectoryEntry(entries, oldName);               // ← 경로 미해석
    if (idx < 0) { return false; }

    if (findDirectoryEntry(entries, newName) >= 0) { return false; }

    parseFilename(newName, entries[idx].name, entries[idx].ext);  // ← 경로 구분자 문제
    writeRootDirectory(entries);
    return true;
}
```

### 문제 A: 루트 디렉토리 전용 (심각도: 중간)

`readRootDirectory()`만 사용하므로 서브디렉토리 내 항목 접근 불가. 같은 핸들러의
`createDirectory()`(837행)와 `deleteDirectory()`(931행)는 서브디렉토리를 지원함.

**참고**: `writeFile()`(537행)과 `deleteFile()`(615행)도 동일한 루트 전용 제한이 있다.
이는 Human68k 핸들러의 전반적인 제한이며, rename만의 문제가 아니다. 그러나 rename은
다른 메서드와 달리 CLI에서 아직 노출되지 않았으므로, 처음부터 올바르게 구현할 기회이다.

### 문제 B: resolvePath 의미 불일치 (심각도: 높음 — 수정 코드 설계에 직접 영향)

Human68k의 `resolvePath()` (1004-1057행)는 MSX-DOS와 **다른 의미의 값을 반환**한다:

| 입력 | MSX-DOS resolvePath | Human68k resolvePath |
|------|:-------------------:|:--------------------:|
| `"FILE"` (루트의 파일) | `{0, "FILE"}` 부모=root | `{0, "FILE"}` 부모=root |
| `"SUBDIR"` (루트의 디렉토리) | `{0, "SUBDIR"}` 부모=root | `{SUBDIR_cluster, "SUBDIR"}` 자기 자신! |
| `"DIR/FILE"` | `{DIR_cluster, "FILE"}` | `{DIR_cluster, "FILE"}` |
| `"DIR/CHILD"` (CHILD는 디렉토리) | `{DIR_cluster, "CHILD"}` | `{CHILD_cluster, "CHILD"}` 자기 자신! |
| `"NONEXIST"` | `{0, "NONEXIST"}` | `{0, ""}` 에러와 구분 불가 |

**핵심 차이**: MSX-DOS는 마지막 컴포넌트를 pop하고 부모까지만 탐색하는 반면,
Human68k는 마지막 컴포넌트까지 진입하여 디렉토리인 경우 자기 자신의 cluster를 반환한다.

따라서 **MSX-DOS의 renameFile 패턴을 Human68k에 직접 복사하면 디렉토리 rename이 실패**한다:
```
// MSX-DOS 패턴을 Human68k에 적용하면:
resolvePath("SUBDIR") → {SUBDIR_cluster, "SUBDIR"}
entries = getDirectoryEntries(SUBDIR_cluster)  // SUBDIR의 내용 읽기
findDirectoryEntry(entries, "SUBDIR")          // SUBDIR 안에서 "SUBDIR" 검색 → 실패!
```

### 문제 C: 존재하지 않는 부모 경로에서의 잘못된 폴백 (심각도: 높음)

`resolvePath(nonEmptyPath)`가 경로를 찾지 못하면 `{0, ""}`를 반환한다.
이 값은 root directory를 의미하는 `{0, ""}` 과 구분할 수 없다.

**시나리오**: `renameFile("NONEXIST/FILE.TXT", "NONEXIST/NEW.TXT")`
```
parentPath = "NONEXIST"
resolvePath("NONEXIST") → {0, ""}  ← 미발견
parentCluster = 0  ← root로 잘못 폴백!
readRootDirectory() 에서 "FILE.TXT" 검색
→ root에 동명 파일이 있으면 잘못된 파일이 rename됨
```

**기존 deleteDirectory()의 방어 메커니즘**: 932-934행에서 선행 `resolvePath(path)` 호출
후 `cluster==0`이면 조기 반환하여 대부분 방어됨. rename에는 이 선행 검사가 없으므로
resolvePath 반환값의 명시적 검증 필요.

**해결**: `parentPath`가 비어있지 않은데 `pCluster == 0`이면 부모가 존재하지 않는 것:
```cpp
auto [pCluster, pName] = resolvePath(parentPath);
if (pCluster == 0) {
    return false;  // 부모 디렉토리 미발견
}
```

FAT12에서 유효한 디렉토리의 startCluster는 항상 ≥ 2이다. cluster 0은 root의 고정 위치를
나타내는 특수 값이므로, resolvePath가 non-empty 경로에 대해 0을 반환하면 경로가 존재하지
않거나 파일(디렉토리 아님)인 것이다. 두 경우 모두 rename의 부모로 부적절하므로 거부.

### 문제 D: 혼합 경로 구분자 분리 오류 (심각도: 낮음, 기존 코드 동일)

`deleteDirectory()`의 수동 파싱 패턴(954-957행)에서 `rfind('/')`가 값을 반환하면
`rfind('\\')` 를 시도하지 않는다:

```
"DIR1/DIR2\\FILE.TXT" 인 경우:
  rfind('/') = 4 → lastSlash = 4
  실제 마지막 구분자 '\\' = 9 (무시됨)
  → parentPath = "DIR1", oldBaseName = "DIR2\\FILE.TXT" (잘못된 분리)
```

**이 문제는 기존 deleteDirectory에도 동일하게 존재**하며, 실제 사용에서 `/`와 `\`를
혼합하는 경우는 매우 드물다. 수정 코드에서 개선하되, 기존 코드와의 동작 일관성을 위해
선택적으로 적용한다.

**개선안** (optional):
```cpp
size_t lastFwd = oldName.rfind('/');
size_t lastBack = oldName.rfind('\\');
size_t oldLastSlash;
if (lastFwd == std::string::npos) oldLastSlash = lastBack;
else if (lastBack == std::string::npos) oldLastSlash = lastFwd;
else oldLastSlash = std::max(lastFwd, lastBack);
```

### 수정 방안: deleteDirectory 패턴 차용 + 안전성 강화

`deleteDirectory()` (931-992행)는 이미 이 문제를 해결한 패턴이 있다.
수동으로 경로를 파싱하여 부모 디렉토리를 찾는 방식이다.
2차 검토에서 발견된 문제(C, D)의 수정을 포함한 최종 코드:

```cpp
bool Human68kHandler::renameFile(const std::string& oldName, const std::string& newName) {
    // ── 경로 구분자 위치 찾기 (혼합 구분자 대응) ──
    auto findLastSeparator = [](const std::string& path) -> size_t {
        size_t fwd = path.rfind('/');
        size_t back = path.rfind('\\');
        if (fwd == std::string::npos) return back;
        if (back == std::string::npos) return fwd;
        return std::max(fwd, back);
    };

    // ── oldName에서 부모 디렉토리와 baseName 분리 ──
    size_t oldLastSlash = findLastSeparator(oldName);

    uint16_t parentCluster = 0;
    std::string oldBaseName;

    if (oldLastSlash != std::string::npos && oldLastSlash > 0) {
        std::string parentPath = oldName.substr(0, oldLastSlash);
        oldBaseName = oldName.substr(oldLastSlash + 1);
        auto [pCluster, pName] = resolvePath(parentPath);
        // [FIX C] 부모 경로가 존재하지 않으면 거부
        if (pCluster == 0) {
            return false;
        }
        parentCluster = pCluster;
    } else {
        oldBaseName = (oldLastSlash == 0) ? oldName.substr(1) : oldName;
        // parentCluster = 0 (루트)
    }

    if (oldBaseName.empty()) {
        return false;
    }

    // ── newName에서 baseName 분리 ──
    size_t newLastSlash = findLastSeparator(newName);

    uint16_t newParentCluster = 0;
    std::string newBaseName;

    if (newLastSlash != std::string::npos && newLastSlash > 0) {
        std::string newParentPath = newName.substr(0, newLastSlash);
        newBaseName = newName.substr(newLastSlash + 1);
        auto [pCluster, pName] = resolvePath(newParentPath);
        // [FIX C] 부모 경로가 존재하지 않으면 거부
        if (pCluster == 0) {
            return false;
        }
        newParentCluster = pCluster;
    } else {
        newBaseName = (newLastSlash == 0) ? newName.substr(1) : newName;
    }

    if (newBaseName.empty()) {
        return false;
    }

    // ── cross-directory rename 방지 ──
    if (parentCluster != newParentCluster) {
        return false;
    }

    // ── 부모 디렉토리의 엔트리 목록 조회 ──
    // getDirectoryEntries(0)는 내부적으로 readRootDirectory()를 호출 (1083행)
    auto entries = getDirectoryEntries(parentCluster);

    int oldIndex = findDirectoryEntry(entries, oldBaseName);
    if (oldIndex < 0) {
        return false;  // 소스 없음
    }

    int newIndex = findDirectoryEntry(entries, newBaseName);
    if (newIndex >= 0 && newIndex != oldIndex) {
        return false;  // 대상 이름 충돌
    }

    // ── 이름 변경 (baseName만 전달 — parseFilename은 경로 구분자 미처리) ──
    parseFilename(newBaseName, entries[oldIndex].name, entries[oldIndex].ext);

    // ── 부모 디렉토리에 기록 ──
    // setDirectoryEntries(0)는 내부적으로 writeRootDirectory()를 호출 (1099행)
    setDirectoryEntries(parentCluster, entries);

    return true;
}
```

### resolvePath 호출 시 반환값 검증

`resolvePath(parentPath)` 호출 시나리오별 분석:

| parentPath | resolvePath 반환값 | parentCluster 값 | 의미 |
|------------|:------------------:|:-----------------:|------|
| `""` | 호출 안 됨 | 0 | root |
| `"SUBDIR"` | `{SUBDIR_cluster, "SUBDIR"}` | SUBDIR_cluster | SUBDIR의 cluster → 올바른 부모 |
| `"DIR1/DIR2"` | `{DIR2_cluster, "DIR2"}` | DIR2_cluster | DIR2의 cluster → 올바른 부모 |

여기서 Human68k resolvePath의 "디렉토리이면 자기 cluster 반환" 특성이 **오히려 유리하게**
작용한다. 부모 경로가 디렉토리이므로, 반환된 cluster가 정확히 우리가 원하는 부모 디렉토리의
cluster이다.

### parseFilename의 경로 구분자 문제

`parseFilename()` (371-394행)은 경로 구분자를 처리하지 않는다:

```cpp
void Human68kHandler::parseFilename(const std::string& filename, char* name, char* ext) const {
    size_t dotPos = filename.rfind('.');
    // "SUBDIR/FILE.TXT" → dotPos=11, baseName="SUBDIR/FILE", ext="TXT"
    // name = "SUBDIR/F" (8자 절단) → 잘못된 이름!
}
```

수정 코드에서는 `oldBaseName` / `newBaseName`을 수동으로 분리한 뒤 전달하므로 이 문제가
자연스럽게 해결된다. parseFilename 자체는 수정하지 않는다.

### FAT12 `.`/`..` 엔트리 영향 없음

`createDirectory()` (897-911행)에서 확인:
```cpp
// . entry (self)
std::memcpy(newDirEntries[0].name, ".       ", 8);
newDirEntries[0].startCluster = newCluster;     // ← 자기 cluster만

// .. entry (parent)
std::memcpy(newDirEntries[1].name, "..      ", 8);
newDirEntries[1].startCluster = parentCluster;  // ← 부모 cluster만
```

`.`과 `..`는 이름 없이 cluster 포인터만 저장하므로, 부모의 엔트리 이름 변경에 영향받지 않음.

### 변경 포인트 상세

| 항목 | 기존 코드 (644-662행) | 수정 코드 |
|------|------|--------|
| 부모 디렉토리 탐색 | `readRootDirectory()` | 수동 경로 파싱 → resolvePath(부모 경로) |
| 서브디렉토리 내 항목 | 접근 불가 | parentCluster로 getDirectoryEntries 호출 |
| cross-directory 방지 | 없음 | `parentCluster != newParentCluster` 검사 |
| 쓰기 대상 | `writeRootDirectory()` 고정 | parentCluster에 따라 분기 |
| 이름 파싱 | `parseFilename(newName, ...)` 경로 포함 위험 | `parseFilename(newBaseName, ...)` baseName만 전달 |
| 디렉토리 자체 rename | 루트만 | 모든 깊이에서 가능 |

### 극단 입력 내성 검증

| 입력 | 처리 결과 | 안전 |
|------|----------|:----:|
| `""` (빈 문자열) | findLastSeparator → npos → else → oldBaseName="" → empty 검사 → false | ✓ |
| `"/"` | findLastSeparator → 0 → else(0 > 0 false) → substr(1)="" → empty → false | ✓ |
| `"\\"` | findLastSeparator → 0 → else → substr(1)="" → empty → false | ✓ |
| `"DIR/"` (후행 슬래시) | lastSlash=3 → parentPath="DIR" → oldBaseName="" → empty → false | ✓ |
| `"NONEXIST/FILE"` | resolvePath("NONEXIST")={0,""} → pCluster==0 → false | ✓ |
| `"FILE.TXT"` (루트 파일) | npos → else → oldBaseName="FILE.TXT" → parentCluster=0 → 정상 동작 | ✓ |

### findLastSeparator 람다의 npos 안전성

```cpp
auto findLastSeparator = [](const std::string& path) -> size_t {
    size_t fwd = path.rfind('/');
    size_t back = path.rfind('\\');
    if (fwd == std::string::npos) return back;   // '/'없으면 '\\'만
    if (back == std::string::npos) return fwd;    // '\\'없으면 '/'만
    return std::max(fwd, back);                    // 둘 다 있으면 큰 쪽
};
```

`npos`(`SIZE_MAX`)와 `std::max` 비교는 npos 체크 **이후**에만 발생하므로
`max(0, SIZE_MAX)` = `SIZE_MAX` 문제는 발생하지 않음.

### 컴파일 요건
- 추가 include 불필요: `std::max`(`<algorithm>`), `resolvePath` 등 모두 기존 포함
- 사용 타입: `size_t`, `uint16_t`, `std::string` — 기존 코드와 동일
- 람다: C++14 이상 (프로젝트는 C++17) ✓

### 리스크
- 낮음. 수동 경로 파싱 + resolvePath 패턴은 `deleteDirectory()` (954-972행)에서 이미
  검증된 코드 경로.
- `setDirectoryEntries(0, entries)`는 내부적으로 `writeRootDirectory(entries)`를 호출
  (1099-1101행)하므로 루트 디렉토리 쓰기 경로도 정상 동작.
- 기존 `deleteDirectory`의 혼합 구분자 문제(문제 D)를 `findLastSeparator`로 개선.

---

## 작업 우선순위

| 순위 | 핸들러 | 작업 | 심각도 | 변경량 |
|:----:|--------|------|:------:|:------:|
| 1 | ProDOS | [A] cross-directory 검사 추가 | 중간 | +3행 |
| 2 | ProDOS | [B] 서브디렉토리 헤더 이름 동기화 | 높음 | +10행 |
| 3 | Human68k | 서브디렉토리 경로 지원 + 안전성 강화 | 중간~높음 | 기존 18행 → 약 60행 |
| — | ProDOS | [C] ACCESS_RENAME 검사 | 낮음 | 별도 이슈 |
| — | DOS 3.3 | 수정 불필요 | — | — |
| — | MSX-DOS | 수정 불필요 | — | — |

### 주의: BootDiskPolicy와의 관계

`MutationOp` 열거형(BootDiskPolicy.h:19)에 `Rename`이 없다. 부트디스크 보호가
rename 작업을 검사하지 않으므로, CLI에 rename 커맨드를 추가할 때 `MutationOp::Rename`도
함께 추가해야 한다. 현재 수정 범위에는 포함하지 않는다.

---

## 테스트 시나리오

현재 `tests/` 디렉토리에 rename 관련 테스트는 존재하지 않음. 신규 작성 필요.

### ProDOS

| # | 테스트 | 검증 사항 |
|:-:|--------|---------|
| 1 | 루트 파일 rename | 기존 동작 유지 확인 |
| 2 | 서브디렉토리 내 파일 rename | 경로 포함 rename 동작 확인 |
| 3 | 루트 디렉토리 rename | 부모 엔트리 + 헤더 이름 일치 확인 |
| 4 | 서브디렉토리 내 서브디렉토리 rename | 깊은 경로에서 헤더 동기화 확인 |
| 5 | cross-directory rename 시도 | false 반환 확인 |
| 6 | 같은 이름으로 rename | false 반환 확인 (FIX D 적용 시 true/no-op) |
| 7 | 중복 이름 rename 시도 | false 반환 확인 |
| 8 | 헤더의 file count/parent pointer | rename 전후 동일한지 바이트 단위 확인 |
| 9 | 멀티블록 디렉토리의 rename | linked block의 prev/next 포인터 보존 확인 |
| 10 | 볼륨명 접두사 경로 | `/VOLNAME/DIR/FILE` 형식 정상 동작 확인 |

### Human68k

| # | 테스트 | 검증 사항 |
|:-:|--------|---------|
| 1 | 루트 파일 rename | 기존 동작 유지 확인 (회귀 방지) |
| 2 | 루트 디렉토리 rename | FAT12 표준 동작 확인 |
| 3 | 서브디렉토리 내 파일 rename | 새로 지원되는 경로 rename 확인 |
| 4 | 서브디렉토리 내 디렉토리 rename | `.`/`..` 미변경 확인 |
| 5 | cross-directory rename 시도 | false 반환 확인 |
| 6 | 경로 구분자 `\` 사용 | 백슬래시 경로도 정상 동작 확인 |
| 7 | 중복 이름 rename 시도 | false 반환 확인 |
| 8 | 존재하지 않는 부모 경로 | `NONEXIST/FILE` → false 반환 확인 |
| 9 | 빈 baseName (`DIR/`) | false 반환 확인 |
| 10 | 선행 슬래시만 있는 경로 (`/FILE`) | root에서 정상 rename 확인 |

---

## CLI rename 커맨드 확장 계획

### 목표

`rdedisktool rename <image_file> <old_name> <new_name>` 커맨드를 추가하여
디스크 이미지 내 파일/디렉토리 이름 변경을 CLI에서 수행 가능하게 한다.

### 변경 대상 파일

| 파일 | 변경 내용 |
|------|---------|
| `include/rdedisktool/BootDiskPolicy.h` | `MutationOp::Rename` 추가 |
| `src/cli/BootDiskPolicy.cpp` | `Rename` op에 대한 canMutate 정책 구현 |
| `include/rdedisktool/CLI.h` | `cmdRename()` 메서드 선언 |
| `src/cli/CLI.cpp` | 커맨드 등록, 핸들러 구현, 헬프 텍스트 |

### 1단계: MutationOp::Rename 추가

`BootDiskPolicy.h:19-24`:
```cpp
enum class MutationOp {
    Add,
    Delete,
    Mkdir,
    Rmdir,
    Rename    // ← 추가
};
```

`BootDiskPolicy.cpp` — `canMutate()`에서 Rename 정책:
부트 크리티컬 파일(PRODOS, COMMAND.COM, COMMAND2.COM, HUMAN.SYS 등)의 rename은
파일 삭제와 동등한 위험이므로 `Delete`와 동일한 정책 적용.

### 2단계: CLI.h — cmdRename 선언

`include/rdedisktool/CLI.h` — private 메서드 추가 (기존 cmdDelete 등과 동일 패턴):
```cpp
int cmdRename(const std::vector<std::string>& args);
```

### 3단계: CLI.cpp — 커맨드 등록

`initCommands()` 내부 (기존 delete/mkdir/rmdir 등록 패턴):
```cpp
registerCommand("rename",
    [this](const std::vector<std::string>& args) { return cmdRename(args); },
    "Rename file or directory in disk image",
    "rename <image_file> <old_name> <new_name>");
```

### 4단계: CLI.cpp — cmdRename 구현

기존 `cmdDelete()`(1407행)와 동일한 패턴을 따름:

```cpp
int CLI::cmdRename(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        printError("Missing arguments");
        printCommandHelp("rename");
        return 1;
    }

    const std::string& imagePath = args[0];
    const std::string& oldName = args[1];
    const std::string& newName = args[2];

    // 디스크 이미지 로드
    auto disk = openDiskImage(imagePath, false /* not readOnly */);
    if (!disk.image || !disk.handler) {
        return 1;
    }

    // 부트디스크 보호 — oldName에 대해 Rename 정책 적용
    auto det = BootDiskPolicy::detect(imagePath, *disk.image,
                                       disk.handler.get(), m_forcedBootProfile);
    auto policy = BootDiskPolicy::canMutate(det, m_bootDiskMode,
                                             MutationOp::Rename, oldName,
                                             m_forceBootDisk);
    if (!policy.allowed) {
        printError(policy.reason);
        if (policy.needsForce) {
            printError("Hint: use --force-bootdisk to override intentionally.");
        }
        return 1;
    }

    // rename 실행
    if (!disk.handler->renameFile(oldName, newName)) {
        printError("Failed to rename: " + oldName + " -> " + newName);
        return 1;
    }

    // 디스크 이미지 저장
    if (!saveDiskImage(disk.image.get(), "rename")) {
        return 1;
    }

    if (!m_quiet) {
        std::cout << "Renamed: " << oldName << " -> " << newName << std::endl;
    }

    return 0;
}
```

### 5단계: CLI.cpp — 헬프 텍스트

`printCommandHelp()` 내부 (기존 delete 패턴):
```cpp
} else if (command == "rename") {
    std::cout << "\nNotes:\n";
    std::cout << "  Renames a file or directory within the same directory.\n";
    std::cout << "  Cross-directory move is not supported.\n";
    std::cout << "  For subdirectory paths, use / as separator.\n";
    std::cout << "\nExamples:\n";
    std::cout << "  rdedisktool rename disk.po OLD.TXT NEW.TXT\n";
    std::cout << "  rdedisktool rename disk.po DIR1/FILE.BIN DIR1/NEWFILE.BIN\n";
    std::cout << "  rdedisktool rename disk.dsk MYDIR NEWDIR\n";
    std::cout << "  rdedisktool rename disk.xdf SUBDIR/OLD.SYS SUBDIR/NEW.SYS\n";
}
```

### 에러 처리 설계

| 상황 | CLI 동작 |
|------|---------|
| 인수 부족 (< 3개) | 에러 메시지 + 사용법 출력, exit 1 |
| 디스크 이미지 열기 실패 | openDiskImage 내부 에러 처리 |
| 부트디스크 보호 차단 | 정책 거부 메시지 + --force-bootdisk 힌트 |
| 소스 파일 미존재 | "Failed to rename" + exit 1 |
| 대상 이름 중복 | "Failed to rename" + exit 1 |
| cross-directory rename | "Failed to rename" + exit 1 |
| MSX-DOS FileNotFoundException | try-catch 로 감싸서 에러 메시지 출력 |

MSX-DOS의 `resolvePath`는 예외를 throw하므로 (다른 핸들러는 false 반환),
`disk.handler->renameFile()` 호출을 try-catch로 감싸야 한다.
기존 `cmdDelete()`의 예외 처리 패턴(`CLI::execute` 420-429행)이 상위에서 잡아주지만,
rename 전용 에러 메시지를 제공하려면 cmdRename 내부에서도 catch하는 것이 좋다.

### 작업 순서

| # | 작업 | 의존성 |
|:-:|------|:------:|
| 1 | `MutationOp::Rename` 추가 + canMutate 정책 | 없음 |
| 2 | `CLI.h`에 `cmdRename` 선언 | 없음 |
| 3 | `CLI.cpp`에 커맨드 등록 + 핸들러 구현 + 헬프 텍스트 | 1, 2 |
| 4 | 빌드 + 기존 테스트 회귀 확인 | 3 |
| 5 | 수동 기능 테스트 (ProDOS/MSX/Human68k) | 4 |

---

## 검증 이력

### 빌드 검증

| 핸들러 | 수정 코드 빌드 | 결과 |
|--------|:------------:|:----:|
| ProDOS | worktree 격리 빌드 | 성공 (경고 0건, 에러 0건) |
| Human68k | worktree 격리 빌드 | 성공 (경고 0건, 에러 0건) |

### 기존 테스트 회귀

수정 전 main 브랜치에서 전체 테스트 통과 확인:
- `test_bootdisk_guard_all.sh` — PASS
- `test_bootdisk_guard_apple.sh` — PASS
- `test_bootdisk_guard_msx.sh` — PASS
- `test_bootdisk_guard_x68000.sh` — PASS
- `test_invalid_bpb_guard.sh` — PASS
- `test_system_file_delete_prompt.sh` — PASS

### 교차 작업 안전성 (코드 레벨 검증)

| 시나리오 | 결과 |
|---------|:----:|
| rename → writeFile (덮어쓰기) | 안전 (deleteFile이 블록 해제 후 새 기록) |
| rename → deleteDirectory | 안전 (헤더 이름 무관하게 블록 해제) |
| rename → listFiles | 안전 (새 이름으로 정상 나열) |
| rename → validateExtended | 안전 (헤더 이름 검증 안 함 — 기존 동작) |

### 검토 회차 요약

총 100회 검토 수행 (5차 패스 × 20회):
- 1차 (1-20): 핸들러별 기본 분석, resolvePath 의미 차이 발견, 수정 방안 초안
- 2차 (21-40): 멀티블록 안전성, resolvePath 폴백 문제, 존재하지 않는 경로 폴백 발견
- 3차 (41-60): 극단 입력 내성, 동일 이름 rename, 예외 전파, 컴파일 요건
- 4차 (61-80): 문서 정합성, 실제 빌드 검증, 교차 작업 안전성, 기존 테스트 회귀
- 5차 (81-100): 메모리 경계 검증, 실제 디스크 이미지 바이트 확인, 인터페이스 계약, diff 확정

### 실제 디스크 이미지 바이트 확인

ProDOS 디스크(test_prodos.po)의 MYDIR 서브디렉토리 헤더를 직접 덤프하여
문서의 바이트 레이아웃 기술이 정확한지 확인:

```
block 7 (MYDIR keyBlock) offset 0x04:
  0xE5 = (STORAGE_SUBDIR_HEADER(0xE) << 4) | nameLength(5) ✓
  "MYDIR" + 10바이트 제로패딩                                ✓
  ...
  block[0x25-0x26] = 0000 (file count = 0)                   ✓
  block[0x27-0x28] = 0200 (parent pointer = block 2)         ✓
```

Human68k 디스크(test_h68k.xdf)의 TESTDIR 엔트리:
```
  name = "TESTDIR " (8바이트 스페이스패딩)                   ✓
  attr = 0x10 (ATTR_DIRECTORY)                               ✓
  startCluster = 0x0002                                      ✓
```

### 메모리 경계 검증 결과

ProDOS FIX B의 기록 범위: `block[0x04..0x13]` (16바이트)
- 블록 크기(512) 대비 최대 인덱스 0x13(=19) → 범위 안전 ✓
- 헤더 엔트리(0x04..0x2A) 내 완전 포함, 보존 영역(0x14..0x2A) 미접촉 ✓
- entry.nameLength 최대값 = MAX_FILENAME_LENGTH(15), 0이 될 수 없음(isValidFilename 검증) ✓
- memcpy: char[15] → uint8_t[] — C++ 바이트 복사 안전 ✓
