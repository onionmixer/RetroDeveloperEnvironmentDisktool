# UPDATE_PUTRAW_DIRECTBOOT.md — rdedisktool `putraw` 업그레이드 (Apple II direct-boot 지원)

> **목적**: `DKFS_retro/prototype_20_AppleII` 의 **direct-boot(ProDOS 없음, raw-sector)** 디스크를
> 조립하기 위해, rdedisktool 에 **고정 track/sector 에 raw 바이트를 기록**하는 `putraw` 명령을
> 추가한다. (배경: PLAN_APPLEII_IMPL.md §3.4/§3.5)
>
> ★★ **절대 원칙: 새 버그 0**. 기존 명령/포맷/파일시스템 동작을 **일절 변경하지 않는다**. 순수
> additive. codex↔Claude 다중 검토 + 전체 회귀 테스트 통과 후에만 머지.
>
> 작성: 2026-06-04. 상태: **구현 완료 (2026-06-05)** — `src/cli/CLI.cpp` 에 `cmdPutRaw`/`cmdGetRaw`
> + 익명 namespace 헬퍼(`parseUSizeStrict`/`hasDkfsMarker`/`lowerExt`), `CLI.h` 선언 2개,
> `initCommands()` 의 registerCommand 2개 (순수 additive). 회귀 게이트: 기존 23 + 신규
> `tests/test_putraw_getraw.sh` = **24/24 통과**. codex 리뷰 1차에서 getraw 의 ofstream
> 지연 flush/close 미검사(High) 발견 → 명시적 flush+close+`!out` 가드로 수정 → codex 재검토 clean.
> 합의: putraw/getraw=`.do`+AppleDO+35/1/16/256 전용, hasImage()(operator bool 금지), 빌드마커
> (DKFS20RAW@T0S15)로 실 DOS33/ProDOS 거부(override=전역 `m_forceBootDisk`), preflight bounds +
> all-or-nothing write(부분변경 0), FS handler 미변경, additive-only. ★ **map/overlap/dup-id·
> `sector_map.h` 생성은 rdedisktool 밖 = Python 빌드스크립트가 메모리에서 처리**(§3.6 — rdedisktool 에
> JSON/직렬화 코드 0 → 새 버그 surface 최소). 구현 시 §7.

---

## 1. 배경 / 필요성

- prototype_20_AppleII 는 ProDOS 를 쓰지 않고 **custom boot sector(T0S0) → stage2 → resident** 로
  부팅하고, 런타임에 **자체 RWTS(ProRWTS2)로 고정 T/S 에서 overlay/asset 을 읽는다**.
- 따라서 빌드 시 디스크의 **임의 track/sector 에 raw 바이트(boot0, stage2, resident image, overlay,
  player, songs)를 기록**해야 한다. 그리고 각 자산이 어느 T/S 에 있는지 **sector-map** 을 산출해
  resident 에 baked 해야 한다.
- **현 rdedisktool 에는 그 기능이 없다** (§2). → **`putraw` 신설**.

## 2. 현 rdedisktool 분석 (변경 대상/재사용 대상 식별)

| 항목 | 위치 | 판정 |
|---|---|---|
| `DiskImage::readSector / writeSector` (public, pure virtual) | `include/rdedisktool/DiskImage.h:112,123` | ★ **재사용**(이미 존재, 검증됨) |
| `AppleDOImage::writeSector` (256B pad/truncate, modified mark) | `src/apple/AppleDOImage.cpp:128` | ★ **재사용** |
| `CLI::loadDiskImageOnly()` (image open; FS handler 는 **optional 생성** — `CLI.cpp:865`) | `src/cli/CLI.cpp:840` | ★ **재사용**. ★ putraw 는 handler **미사용/미변경**(sector 레벨만) |
| `CLI::saveDiskImage(image, op)` | `src/cli/CLI.cpp:869` | ★ **재사용** |
| `CLI::cmdDump(args)` (T/S read, human-readable 출력) | `src/cli/CLI.cpp:2358` | **미러링 참고** (write 버전 작성) |
| dispatch = **`m_commands` map lookup** (`CLI.cpp:456`) — if-chain 아님 | `src/cli/CLI.cpp:456` | `registerCommand` 가 map 등록 → **dispatch 코드 무수정** |
| `CLI::registerCommand(...)` + `initCommands()` | `src/cli/CLI.cpp:404,333` | **추가만**(기존 등록 무수정) |
| CLI 명령 집합 (info/list/extract/add/delete/mkdir/rmdir/rename/create/convert/dump/validate/list-formats) | `src/cli/CLI.cpp:334-401` | **무수정** — 등록 줄만 추가 (★ `format` 명령은 없음) |
| 생성 Apple 디스크 = 비부팅(boot code 미기록) | `README.md:458` | putraw 로 T0S0 기록해 부팅화 |

→ ★ **필요한 저수준 API 가 이미 전부 존재**. 업그레이드 = 그것을 **새 CLI 명령으로 노출**할 뿐.
기존 코드 경로(파일시스템 add/delete/format, 포맷 변환, 다른 플랫폼)는 **건드리지 않는다**.

## 3. 신설 명령 `putraw` / `getraw` (spec)

★★ **범위 최소화 (새 버그 0 핵심)**: **rdedisktool putraw/getraw 는 raw sector I/O + arg 검증 +
디스크 안전가드만**(최소 C++). ★ **sector-map(extent 테이블)·overlap·dup-id 검증·`sector_map.h`
생성은 전부 prototype_20 빌드 스크립트(Python)** 가 소유 — Python 이 extent 를 **메모리에서 계산·
검증**하고 **`sector_map.h`(C 테이블)를 직접 생성**(★ 중간 파일/직렬화 없음 — rdedisktool 에
schema·파싱 코드 일절 없음 → 신규 버그 surface 최소). rdedisktool 은 **JSON 등 어떤 직렬화 포맷도
다루지 않는다.**

```
rdedisktool putraw <image> <hostfile> --track T --sector S [--max-sectors N] [--format do] [--force-bootdisk]
rdedisktool getraw <image> -o FILE --track T --sector S --count N [--force]
```
★ **`--map-out`/`--id`/`--load-addr`/`--lc-bank` 는 putraw 에 두지 않음** — sector-map 메타는 빌드
스크립트가 메모리에서 관리(§3.6).
- ★★ **포맷 제한(blocker fix) — `.do` 확장자 + AppleDO + 35×1×16×256 전용**:
  - **확장자 `.do` 필수**(container 정책). ★ `--format do` 단독으로 비-`.do`(예: `.po`) 파일을
    AppleDO 로 강제 오픈하면, `AppleDOImage::load` 가 **크기만 검사**(140K/DOS3.2; `AppleDOImage.cpp:
    40-48`)라 `.po` 를 DO-order 로 **오해**할 수 있음 → 초기엔 **`.do` 확장자 아닌 파일 거부**.
  - detect 포맷이 `DiskFormat::AppleDO` 아니면 거부(`loadDiskImageOnly` 항상 auto-detect; `.do`→
    AppleDO unambiguous `FormatDetector.cpp:162-166`).
  - ★ **geometry 권위 = `image->getGeometry()`**(`DiskImage.h:77`). AppleDO 라도 13-sector DOS3.2
    (`AppleDOImage.cpp:40-45`) / `create -g` 커스텀(`CLI.cpp:2034-2038,2097-2105`) 가능 → **정확히
    35trk·1side·16sec·256B 아니면 거부**(blank `create -f do` 기본 = 35/1/16/256, `DiskImageFactory.
    cpp:209-219`). direct-boot 디스크는 표준 16-sector 5.25".
  - ★ **항상 autodetect 로 open**(forced open 금지): `--format do` 를 *loader override* 로 쓰면
    `AppleDOImage::load` 가 크기만 봐서 `.po` 등을 DO 로 오해(`AppleDOImage.cpp:40-48`). → putraw 는
    **forced open 안 함**; `--format do` 는 (남길 경우) *검증 힌트*일 뿐 loader override 아님.
  - `.po/.nib/.woz/MSX/Mac` 은 **불허**(§8 R2).
- 동작(순서 고정): ① `loadDiskImageOnly`(autodetect) 후 **`hasImage()` 확인**(★ `operator bool` 금지 —
  image+handler 둘 다 요구 `CLI.h:31`. **handler 존재는 불확실**: blank `.do` 도 DOS33 fallback handler
  부착 가능(`FileSystemHandler.cpp:50-53` + `AppleDOS33Handler::parseVTOC` zero→35/16 default→true `:59-77`),
  타 이미지는 null 가능 → **handler 비의존 `hasImage()` 사용**), ② **`.do` 확장자
  + format==AppleDO + getGeometry()==35/1/16/256 검사**, ③ **raw-write 가드 + 빌드마커**(§4-4: 마커
  전체일치 허용, else Unknown 허용, else 거부 unless `m_forceBootDisk`), ④ **preflight bounds**: `hostfile`
  크기→sector 수 + 시작(T,S) 로 **마지막 (T,S) 를 실제 geometry 로 계산해 범위 내인지 검증**(밖이면
  *write 前* 에러 — 부분 변경 방지), ⑤ 256B sector 분할(마지막 zero-pad) → `(T,S)` 부터 연속
  `writeSector(track, **0**, sector, …)`(single-side), ⑥ `saveDiskImage(image,"putraw")`. ★ **map 없음
  — overlap/dup-id 등은 빌드 스크립트가 putraw 호출 *前* 에 이미 검증**(§3.6).
- 옵션:
  - `--track T --sector S` (필수): 시작 위치.
  - `--format do` (선택, 검증 힌트만 — ★ loader override 아님, 항상 autodetect).
  - `--max-sectors N` (선택): 안전 상한(초과 시 에러).
  - `--force-bootdisk` (선택, 전역): getFileSystemType()!=Unknown 인 이미지에 강제 raw write(§4-4).
- sector 진행: track 내 sector 소진 시 다음 track. ★ `DiskImage.cpp:5` 의 산술은 **512B-block 기반**
  (`sectorsPerBlock=512/bytesPerSector`)이라 putraw 가 직접 못 씀 — putraw 는 **256B sector loop 을
  자체 구현**하되 **`geom.bytesPerSector`(256)/`geom.sectorsPerTrack`(16)을 getGeometry() 에서 읽어**
  사용(하드코딩 금지). ★ **geometry 밖 = 에러**.
- 출력(★ 선택적 audit, JSON 아님 — **단순 TSV 1줄** stdout): `PUTRAW\t<track>\t<sector>\t
  <sectors>\t<bytes>`. (string escaping/JSON emit 회피 → 깨질 여지 0. id/load-addr 는 빌드스크립트가 부여.)
  ★ **성공 계약 = exit code 0**(에러는 stderr+nonzero). TSV 1줄은 **선택적 audit/log** — 빌드스크립트는
  자기 extent 를 이미 알고 있어 **의존하지 않음**(원하면 putraw 가 기록한 값 대조용으로만 사용).

★ **`getraw <image> -o FILE --track T --sector S --count N [--force]` (필수)**: binary 추출. 기존
`dump` 은 human-readable 만(`CLI.cpp:2358-2475`), `extract` 는 FS-레벨이라 **raw byte 비교 불가** →
round-trip 회귀에 getraw 필수. putraw 와 **동일 규칙**(`.do`+AppleDO+35/1/16/256, `hasImage()`,
side=0, read-only 라 raw-write 가드 불요). ★ 출력 `-o` 안전 = **N11**, span 검증 = N1/N2/N3.
- read loop: `linearStart = track*spt + sector`; `i=0..count-1` 마다 `linear=linearStart+i` →
  `(linear/spt, linear%spt)` side 0 `readSector` → 이어붙임. 출력 = **정확히 `count*256` B**(원 asset
  길이로 trim 안 함). putraw 와 동일하게 track 경계 넘김.

## 3.6 ★ 빌드 스크립트(Python) 책임 — sector-map (rdedisktool 밖, ★ JSON 없음)

★ map/schema/검증·`sector_map.h` 생성은 rdedisktool 에 안 넣는다. **prototype_20 빌드 스크립트
(Python `build_disk.py`)** 가 **전부 메모리에서** 처리(중간 직렬화 파일 없음):
1. blank `.do` (`create -f do`) 준비.
2. 모든 asset 의 extent `(track,sector,sectors,bytes,id,load_addr,lc_bank)` 를 **메모리 list 로 계산**.
3. ★ **putraw 호출 *前* 에 Python 이 검증**: geometry 일치, **dup-id, extent overlap(예약 T0S0/
   T0S1-14/T0S15 포함), max-sectors, reserved 침범**. → rdedisktool 은 이미 검증된 안전한 write 만 수행.
4. marker.bin(256B) 생성 → marker putraw 먼저.
5. 각 extent 마다 `putraw`(검증 통과분만) → exit code 0 확인(선택: TSV 로 기록값 대조).
6. **메모리 extent list → `sector_map.h`(C `RawExtent`/`RawAsset` 테이블) 직접 emit**. (★ JSON 중간
   파일 없음 — stale-map/`build_id`/직렬화 파싱 문제 자체가 소멸. 디버그 manifest 필요 시 plain-text.)
7. `set -e` — 어느 putraw 든 nonzero 시 **image 폐기 후 처음(`create`)부터 재빌드**.

→ rdedisktool 신규 C++ = **raw sector write/read + arg 검증 + 마커 가드**로 한정(가장 작은 버그
surface). **JSON/직렬화 코드 0.**

★★ **명시(소유 경계)**: putraw 의 마커 가드는 **외부(foreign) DOS33/ProDOS 디스크 보호용**일 뿐,
**DKFS-마킹된 우리 디스크의 자기-레이아웃(예약 sector boot0/marker/stage2)은 보호하지 않는다**
(마커 있으면 어떤 T/S 든 write 허용 — raw primitive 의 본질). **예약 sector/overlap 보호는 오로지
`build_disk.py` 가 putraw 호출 前 검증으로 강제**한다. 마킹된 이미지에 **수동 putraw 는 그 디스크를
손상시킬 수 있으며 지원 빌드 경로가 아님.** (rdedisktool 에 하드코딩 reserved-sector 가드를 넣지
않음 — primitive 복잡화 + marker/boot0/stage2 write 에 예외 필요해져 역효과.)

### (참고) `build_disk.py` 가 직접 생성하는 `sector_map.h` 예 — rdedisktool 산출 아님
```c
/* auto-generated by build_disk.py (메모리 extent → 직접 emit, JSON 없음) */
static const RawExtent g_ovl_login[] = { {6, 0, 11, 0x8000, 2752} };  /* track,sector,sectors,load,bytes */
static const RawAsset  g_assets[] = {
    { ID_BOOT0,     1, g_boot0  },
    { ID_RESIDENT,  1, g_res    },
    { ID_OVL_LOGIN, 1, g_ovl_login },
};
```

## 3.5 ★ 입력 검증 / 에러 처리 규칙 (구현 필수 — 새 버그 방지)

★ 아래는 spec 의 일부. 누락 시 구현자가 버그를 짜기 쉬움(전부 신규 테스트로 강제).

- **N1 엄격 숫자 파싱**: `--track/--sector/--max-sectors`, getraw `--count` 는 ★ **cmdDump 의
  `std::stoi`(catch-all, pos 미검사 — "1junk"→1, 음수→cast 후 거대값; `CLI.cpp:2382-2410`) 복붙
  금지**. **full-string 검증**(공백/`+`/`-`/trailing junk/빈문자 거부), **non-negative**, `size_t`
  범위 초과 거부. 파싱은 **디스크 변경 前**.
- **N2 start (T,S) 독립 검증**: span 계산 *前* 에 `track < geom.tracks && sector < geom.sectorsPerTrack`
  먼저 확인(아니면 에러). 그 후에야 `linearStart = track*sectorsPerTrack + sector`.
- **N3 overflow-safe span**: `byteLen>0` 확인 후 `sectorCount = (byteLen + bps-1)/bps`(bps=256). 거대
  파일은 `sectorCount > totalSectors - linearStart` 로 **pre-write 에러**(200KB→140KB 디스크 = loop
  실패 아닌 preflight 거부). `--max-sectors` 있으면 `sectorCount <= max-sectors`.
- **N4 0-byte hostfile 거부**: `ceil(0/256)=0 sector` → 무의미 no-op/빈 extent 방지 → **에러**. getraw
  `--count 0` 도 거부.
- **N5 hostfile read 순서**: ① **stat/seek 로 `byteLen` 만 먼저** 취득 → ② preflight(N3/N4/max-sectors)
  → ③ **그 후 정확히 `byteLen` 만 `vector<uint8_t>` 로 read** → ④ write loop. (★ read *前* preflight 로
  1GB 악성 입력을 통째 메모리에 안 올림.) open/read 실패 시 **0 변경** 에러. ★ sector-by-sector 스트리밍
  금지(중간 read 실패가 일부 writeSector 後 발생 → 부분변경).
- **N6 write loop all-or-nothing**: ★ `writeSector` 는 **throw 가능**(`WriteProtectedException` /
  `SectorNotFoundException`; `AppleDOImage.cpp:130-143`). write loop 을 try 로 감싸 **하나라도 throw 시
  saveDiskImage 미실행 + nonzero 반환**. write 前 `image->isWriteProtected()` 노출 시 선검사.
  (preflight 가 bounds 는 이미 거름 → 잔여 throw = fatal no-save.)
- **N7 save 실패 처리**: `saveDiskImage` 가 **false 반환 시**(rename/디스크full 등; 원본은 보존
  `CLI.cpp:910-915`) → cmdPutRaw **nonzero 반환**(빌드스크립트 `set -e` 가 중단·재빌드).
- **N11 getraw 출력(`-o FILE`) 안전**: ★ **`-o` == 입력 image 경로(canonical/absolute 정규화) 거부**
  (원본 클로버 방지), 기존 출력 파일은 **`--force` 없으면 거부**, **parent dir 자동생성 안 함**,
  `FILE.tmp.rdedisktool` 에 쓰고 rename, 실패 시 temp 제거 + nonzero. getraw 도 putraw 와 동일 start/
  overflow-safe span 검증(`track<tracks`, `sector<spt`, `count>0`, `count<=totalSectors-linearStart`).
- **N14 테스트 fixture 보호**: `--force-bootdisk` 강제 write 테스트는 **source fixture 직접 변경 금지**
  — `$WORK` 로 복사 후 사용(기존 bootdisk 테스트 패턴, `test_bootdisk_guard_apple.sh` copy-to-temp).

> ★ **map 관련 규칙(구 N9 load-addr·N10 id·N12 stale/identity·N13 inconsistent)은 rdedisktool 밖
> = 빌드 스크립트 책임(§3.6)**. rdedisktool putraw 에는 `--map-out`/`--id`/`--load-addr` 가 없음.

## 4. ★ 회귀 안전 전략 (새 버그 0)

1. **순수 additive**: `initCommands()` 에 **`registerCommand("putraw", …)` + `registerCommand("getraw",
   …)` 두 줄 추가**(`CLI.cpp:404`) + `cmdPutRaw()`/`cmdGetRaw()` **신규 함수** + help. (★ 미등록 시
   dispatch `m_commands`(`:456`)에서 dead → 둘 다 등록 필수.) dispatch 는 `m_commands` map(`CLI.cpp:456`)이라 분기
   추가 불요. **기존 명령 핸들러/등록 무수정.** (단 `help`/명령 목록 출력은 `m_commands` 순회라
   putraw·getraw 가 *의도적으로* 표시됨 — help 회귀 테스트로 커버.)
2. **백엔드 로직 신작 0**: 디스크 I/O 는 검증된 `loadDiskImageOnly`/`writeSector`/`saveDiskImage`
   재사용(add/create 와 동일 경로). ★ 단 **CLI 레벨 신규 로직**(host-file chunking, zero-pad,
   sector wrap, preflight bounds, TSV success 출력)은 신작 → **신규 테스트로 커버**(§6).
3. **FS mutation 경로 미진입**: `loadDiskImageOnly` 가 handler 를 optional *생성*(파싱)하나(`CLI.cpp:
   863-865`) putraw 는 **handler 미사용**(sector write 만) → FS 핸들러의 **write/mutate 경로 미진입**.
   ★ ① `hasImage()` 로 판정(★ `operator bool` 금지 — image+handler 둘 다 요구 `CLI.h:31`; handler
   존재 불확실: blank `.do` 도 DOS33 fallback handler 가능(`FileSystemHandler.cpp:50`, `parseVTOC`
   zero→default `:59-77`), 타 이미지 null 가능 → handler 비의존 `hasImage()`), ② **format==AppleDO
   아니면 거부**(§3).
4. **★ raw-write 가드 (putraw 자체 구현 — BootDiskPolicy 비의존) + 빌드마커**: 검증결과
   `BootDiskPolicy` 는 FS 존재가 아니라 `isBootDisk = hasSystemFiles || pathHint` 로만 차단
   (`src/core/BootDiskPolicy.cpp:196`, `canMutate`=`!isBootDisk` 허용 `:233-234`), `MutationOp` 에 raw op 없음
   (`include/rdedisktool/BootDiskPolicy.h:19-25`) → **재사용 불가**. putraw 자체 가드:
   - **override 플래그 = 전역 `m_forceBootDisk`** (★ `--force-bootdisk` 는 dispatch 前 전역 파싱
     `CLI.cpp:485-486`, `CLI.h:138` 저장 — **local args 에서 안 보임**. cmdPutRaw 는 `m_forceBootDisk`
     멤버를 읽는다).
   - **★ 빌드마커(blocker fix)**: multi-call putraw 중 asset 바이트가 우연히 DOS33 VTOC(T17S0,
     `AppleDiskImage.cpp:isDOS33`) / ProDOS block2 처럼 보이면 `getFileSystemType()!=Unknown` → *자기
     디스크* 2번째 putraw 가 거부될 수 있음. → **고정 T0S15 에 전체 마커 sector**를 둔다.
   - **마커 sector 포맷(정확 256B = 1 sector)**: offset0 magic `"DKFS20RAW"`(9B) + version(1B) +
     **inverse magic**(magic XOR 0xFF, 9B, 부분일치/우연충돌 방지) + reserved 0. putraw 는 **모든
     필드 일치 시에만** 마커 인정(부분일치 거부). ★ **marker.bin 은 정확히 256B**(>256=`--max-sectors 1`
     로 T1S0 침범 차단, <256=zero-pad 되지만 빌드 마커는 **정확 256B 강제**=빌드 스크립트가 생성).
     진짜 DOS33/ProDOS
     디스크가 T0S15 에 우연히 이 전체 패턴을 가질 확률 ≈ 무시(1/256^18). (T0S15 = PLAN §3.4 의
     build-id/marker sector — boot0=T0S0, stage2/RWTS=T0S1-T0S14 라 비충돌.) ★ **marker.bin(256B)은
     prototype_20 `compile.sh` 가 생성**(rdedisktool 무관 — putraw 는 받은 256B 를 그대로 기록).
   - **★ ordering 규칙(필수)**: `create -f do`(blank=Unknown) **직후 첫 명령 = 마커 putraw**
     (`putraw img marker.bin --track 0 --sector 15`). blank 라 Unknown→허용으로 통과하며 마커를 심음.
     **이후 모든 putraw 는 마커로 통과**(asset 이 false-positive 만들어도). → 마커가 어떤 FS
     false-positive 보다 *먼저* 존재 = chicken-and-egg 없음.
   - **가드 판정 순서**: ① 마커 sector(T0S15) read → 전체 일치면 **허용**. ② else `getFileSystemType()
     ==Unknown`(blank/초기) → **허용**. ③ else(인식 FS + 마커 없음 = 사용자 진짜 DOS33/ProDOS) →
     **거부** unless 전역 `m_forceBootDisk`.
   - ★ `--bootdisk-mode off` 는 putraw 가드에 **영향 없음**(BootDiskPolicy 비의존) — override 는
     `--force-bootdisk` 전역 플래그만.
5. **preflight 경계 검증(부분변경 방지)**: ★ `writeSector` 는 sector 마다 in-memory 변경+modified
   마크(`AppleDOImage.cpp:128-156`), `saveDiskImage` 가 전체 persist(`CLI.cpp:869`). → **첫 write 前**
   에 마지막 (T,S) 가 geometry 내인지 계산·검증(밖이면 *어떤 sector 도 안 쓰고* 에러) → 부분 변경
   이미지 잔존 0.
6. **다른 포맷/플랫폼 불변**: putraw = **AppleDO(`.do`) 전용**(§3). MSX/X68000/Mac/ProDOS/.nib/.woz
   = **거부**(코드 미진입). `.dsk/.po/.woz/.nib` 지원은 후속(별도 테스트, §8 R2).
7. **빌드/ABI 무영향**: 새 .cpp 심볼 + 헤더에 `cmdPutRaw`/`cmdGetRaw` 선언 추가만. 기존 ABI/링크 변경 0.
8. **map/overlap/dup-id = rdedisktool 밖(§3.6)**: ★ putraw 에 `--map-out` 없음. extent overlap·dup-id·
   예약 sector(T0S0/T0S1-14/T0S15) 보호 검증은 **빌드 스크립트(Python)가 putraw 호출 前에 메모리에서
   수행** + `sector_map.h` 직접 생성(중간 직렬화 파일 없음 → stale/identity 문제 소멸). → rdedisktool 은
   이미 검증된 안전한 write 만 + **JSON/직렬화 코드 0** → 새 코드/버그 최소.

## 5. 구현 위치 (최소 변경 set)

| 파일 | 변경 |
|---|---|
| `include/rdedisktool/CLI.h` | `int cmdPutRaw(...)` + `int cmdGetRaw(...)`(**필수** — byte-exact 회귀에 필요) **선언 추가** |
| `src/cli/CLI.cpp` `initCommands()` | **`putraw` + `getraw` 각각** `registerCommand(name, handler, description, usage)` **2줄 추가**(★ **4-arg** 시그니처 `CLI.h:71-74`, info/dump 와 동일). dispatch 는 `m_commands` map(`CLI.cpp:456`)이라 분기 추가 불요. **기존 줄 무수정** |
| `cmdPutRaw` arg 파싱 | ★ **cmdDump 와 동일 `CommandOptions`**(`CLI.cpp:2360-2364`): `addValue("track",{"-t","--track"})`, `addValue("sector",{"-s","--sector"})` 등 — 기존 CLI 규약 일치 |
| `src/cli/CLI.cpp` | `cmdPutRaw()` **신규 정의**(map 없음). 흐름: arg 파싱(N1 엄격) → `loadDiskImageOnly`(autodetect) → **`hasImage()`**(operator bool 금지) → **`.do`+AppleDO+getGeometry()==35/1/16/256** → **raw-write 가드**(① 마커 전체일치→허용 ② Unknown→허용 ③ else 거부 unless `m_forceBootDisk`) → **preflight bounds**(N2/N3, 밖이면 write 0건) → host 통째 read(N5) → **try{ write loop(side=0) }**(N6 all-or-nothing) → `saveDiskImage`(false→nonzero N7) → exit 0(성공 계약; 선택 audit TSV) |
| `src/cli/CLI.cpp` | `cmdGetRaw()` **신규 정의**: `.do`+AppleDO+geometry → span 검증(N1-N3) → readSector loop → `-o` 안전(N11) temp+rename |
| `tests/` | `test_putraw_roundtrip.sh` 등 **신규 테스트** |
| `README.md` | putraw/getraw 명령 문서 **추가** (help/명령목록 출력 변화 동반 — 의도적) |

★ **수정 0 인 것**: 모든 FileSystemHandler, DiskImage 서브클래스(`writeSector` 는 호출만),
**BootDiskPolicy 코어(읽지도 않음 — putraw 는 §4-4 자체 가드만 사용)**, 다른 CLI 명령, FormatDetector.
★ **의도적 변화(회귀 아님)**: `help`/명령목록(`m_commands` 순회, `CLI.cpp:542`)에 putraw·getraw 추가.

## 6. 테스트 계획 (회귀 0 입증)

### 6.1 신규 putraw/getraw 테스트
- **round-trip**: `create -f do` → `putraw`(여러 자산) → **`getraw` 로 byte-exact 일치** 확인.
- **★ getraw 동작**: track 경계 걸치는 `--count`(예: T0S14 부터 count 4 → T1 로 넘어감) → 출력 **정확히
  `count*256` B**, putraw 한 바이트와 일치. `count > totalSectors-linearStart` → 거부(N11). `getraw`
  미등록이면 dead 임을 방지하는 등록 확인(help 에 getraw 표시).
- **연속 sector / track 넘김**: 256B 초과 파일이 다음 sector·track 으로 분산.
- **zero-pad**: 비배수 길이 마지막 sector zero-pad.
- **preflight 경계**: geometry 밖 T/S = **write 0건으로 에러**(부분변경 없음을 getraw 로 검증),
  `--max-sectors` 초과 = 에러.
- **★ 포맷/geometry 거부**: 비-`.do` 파일·비-AppleDO·13-sector/커스텀 geometry → 거부.
- **★ blank 디스크 수용**: 갓 `create -f do`(Unknown) 에 첫 putraw 수용 + 마커 기록.
- **★ multi-call 누적**: 같은 custom 디스크에 2·3번째 putraw 수용(마커로 통과, FS false-positive
  내도 거부 안 됨) — DOS33 VTOC-유사 payload 를 일부러 써도 마커 디스크는 계속 수용.
- **★ 실 FS 거부**: 진짜 DOS33 `.do`(+가능 시 DO-order ProDOS, `AppleDiskImage.cpp:101-120`)에
  putraw → **거부**, `--force-bootdisk`(전역) 시에만 허용. `--bootdisk-mode off` 로는 우회 안 됨.
- **★ 부분-마커 거부**: T0S15 에 magic 만 있고 inverse/version 불일치인 DOS33 디스크 → 마커 불인정
  → 여전히 거부(전체 필드 일치만 인정).
- **★ oversized 마커 거부**: marker.bin 이 257B+ 면 `--max-sectors 1` preflight 로 **write 0건 에러**
  (T1S0 침범 방지).
- ★ **하네스**: `--force-bootdisk` 등 전역 플래그는 dispatch 前 파싱(`CLI.cpp:417-429,485`)이므로
  테스트는 `CLI::run()` 경로로 호출(직접 `execute()` 는 전역 미파싱).
- (★ overlap/dup-id/map 테스트는 **빌드 스크립트(Python) 테스트** — rdedisktool putraw 엔 map 없음.)
- **★ 입력검증(N1-N7,N11)**: `--track`/`--sector`/`--max-sectors`/`--count` 에 음수·`1junk`·빈문자·
  거대값(overflow) → 거부(N1). start (T,S) 범위 밖 → 거부(N2). 200KB→140KB = preflight 거부(N3).
  **0-byte hostfile → 거부**(N4). 존재안함/read 실패 hostfile → 0 변경 에러(N5). write-protected
  이미지 → save 미실행 에러(N6). getraw `-o`==입력 image / 기존파일(no --force) → 거부(N11). **각
  에러 후 디스크 byte-exact 불변**(getraw 검증).
- **★ save 실패**: (가능 시) save 실패 주입 → nonzero(N7).
- **부팅**: putraw 로 조립한 custom-boot `.do` 가 AppleWin 부팅(스모크, xvfb 또는 수동).

### 6.2 ★ 전체 회귀 (기존 동작 불변 입증 — 필수)
★ **현재 단일 all-tests 러너 없음**: `tests/test_bootdisk_guard_all.sh` 는 *선택분*만 실행하고,
`tests/run_all.sh` / CTest(`CMakeLists.txt:156` `BUILD_TESTS=OFF`, `tests/CMakeLists.txt` 미존재)는
없음. → **본 업그레이드에서 `tests/run_all.sh` 신설**(모든 `tests/test_*.sh` 순차 실행, 1개라도
fail 시 비0 exit) 권장, 또는 아래를 **개별 실행**:
```
tests/test_bootdisk_guard_all.sh  (apple/msx/x68000/macintosh)
tests/test_bootdisk_guard_{apple,msx,x68000,macintosh}.sh
tests/test_bootdisk_mode_semantics.sh
tests/test_extract_rsrc_warn.sh
tests/test_format_registrar.sh
tests/test_invalid_bpb_guard.sh
tests/test_system_file_delete_prompt.sh
tests/test_mac_*.sh  (convert/checksum/hfs/mfs/moof 전체)
+ CLAUDE.md 의 MSX Hi-Tech 파이프라인 테스트(tools/msx/*) — 디스크 조립 부분
```
**1개라도 회귀 시 머지 금지.** baselines/ 비교도 통과.

## 7. 검토 절차 (사용자 요구: 새 버그 0, 아주 자세히 검토·재검토)

1. **codex 1차**: 본 spec + 실제 구현 PR 을 회귀/경계/가드/메모리 관점 검토.
2. **Claude 재검토**: codex 결과를 소스로 재검증(특히 §4 회귀안전 7항·boot-guard 상호작용·geometry
   경계). 다중 라운드 수렴까지.
3. **전체 회귀 테스트 green** + putraw/getraw 신규 테스트 green.
4. (선택) AppleWin 부팅 스모크.
→ 위 전부 통과 후에만 rdedisktool 머지. PLAN_APPLEII P0b 의 선행 의존.

## 8. 리스크 / Open

- **R1 boot-guard 상호작용**: putraw 가 기존 보호 디스크를 우회 못 하게 하는 정책(§4-4) 설계가
  핵심 — 잘못하면 기존 보안 후퇴. → codex/Claude 집중 검토 + 전용 테스트.
- **R2 geometry 다양성**: Apple 5.25"(35trk×16sec) 외 포맷(.po order, .nib/.woz)별 sector 의미 차이.
  초기 = `.do`(DOS order) 한정. .po/.nib/.woz 는 후속(필요 시).
- **R3 sector skew/order**: `.do` 는 DOS 논리순서 저장(`AppleDOImage.cpp:105`). RWTS(런타임)와
  rdedisktool(빌드)의 sector 번호 의미가 일치해야 함 — putraw 의 (T,S) 는 **논리 sector** 기준,
  RWTS 도 동일 기준으로 읽도록 빌드/런타임 계약 명시(prototype_20_AppleII P0b 검증).
- **R4 ProRWTS2 라이선스**(rdedisktool 무관, prototype_20 측 결정항목).
- **선택 후속**: `.woz` 출력(실기 충실도) — 현 `AppleWozImage` 에 sector write 경로 존재
  (`src/apple/AppleWozImage.cpp`), putraw → woz convert 로 커버 가능(별도 검증).

---

## 부록. 관련 문서
- 소비자 계획: `DKFS_retro/prototype_20_AppleII/PLAN_APPLEII_IMPL.md` §3.4(direct-boot)/§3.5(본 업그레이드 참조)/§9 P0b/§11 #12.
- rdedisktool 기존 설계: `Disktool_Development_Plan.md`, `README.md`.
