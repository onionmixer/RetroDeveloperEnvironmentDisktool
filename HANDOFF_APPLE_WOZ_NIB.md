# HANDOFF — rdedisktool Apple II WOZ(및 NIB) 지원 개선

> 작성 2026-10-07 · 출처: sa2(AppleWin) IIc 지원 작업 중 발견(`DKFS_retro/prototype_20_AppleII/PLAN_99_A2_SA2_IIC.md` §11-9 참고)
> 상태: **조사 완료 · 코드 무변경** · 이 문서만 보고 다음 세션이 이어서 작업할 수 있게 쓴다.

## 0. 한 장 요약

| 증상 | 재현 | 판정 |
|---|---|---|
| `convert dos33.dsk x.woz -f woz` 결과가 **IIe 에서 부팅 안 됨**("Apple //e" 화면에서 정지) | §2 | ❌ |
| 같은 원본으로 만든 **NIB 도 부팅 안 됨** | §2 | ❌ |
| rdedisktool 이 **자기가 만든 WOZ/NIB 를 `list` 못 함**(`Error: Sector not found: Track 207, Sector 195`) | §2 | ❌ |
| **DSK→WOZ→DSK 왕복이 원본과 다름**: 비어 있지 않은 섹터 **298/560 전부** 손상(0 섹터 262 개만 우연히 일치) · DSK→NIB→DSK 도 동일하게 실패 | §2 | ❌ |
| 정상 WOZ(sa2 에서 DOS `INIT` 으로 만든 디스크 · DOS 는 CATALOG 에 HELLO 표시)를 `list` 하면 **"DISK VOLUME 0 · 파일 없음"** | §2 | ❌ |

⇒ 원인은 **두 층**이다.
1. **공용 6-and-2 인코딩/디코딩 결함**(`NibbleEncoder`) — NIB 와 WOZ 둘 다 깨진다(왕복 바이트가 완전히 다른 값 · 하위 2 비트만 다른 경우는 66,651 개 중 791 개뿐 → "2 비트 부분만 틀림"이 아님).
2. **WOZ 비트스트림 생성 결함**(`AppleWozImage`) — NIB 바이트 배열을 그대로 비트로 저장해 **자기동기(10 비트 `11111111 00`)가 없다**. 그 밖에 TMAP·INFO 필드가 사양·관례와 다르다(§3-2).

## 1. 관련 파일
- `src/apple/NibbleEncoder.cpp`(`buildTrack` 243-300 · `encodeSector`/`decodeSector`/`encodeAddressField`·`decode44`) · `include/rdedisktool/apple/NibbleEncoder.h`(`TRACK_NIBBLE_SIZE = 6656`)
- `src/apple/AppleWozImage.cpp`(743 줄): `create`(442-) · `writeSector`(540-) · `readTrack`/`writeTrack`(565-600) · `buildInfoChunk`(325-) · `buildTmapChunk`(354-) · `parseTmapChunk`(160-)
- `src/apple/AppleNibImage.cpp`
- 참고 구현(같은 저장소): `src/macintosh/MacGcrEncoder.cpp:20` `kAutoSync = {FF 3F CF F3 FC FF}` — **10 비트 자기동기를 바이트로 묶어 비트스트림에 넣는 선례**(Mac GCR). Apple II 에도 같은 생각을 쓸 수 있다.
- 시험: `tests/` 에 Apple II NIB/WOZ 시험이 **없다**(Mac 쪽 `test_mac_moof_roundtrip.sh` 가 형식 참고).
- 빌드·실행·스모크 시험 절차: 저장소 스킬 `.claude/skills/run-rdedisktool`(빌드 = `HOWTO_COMPILE.md`).

## 2. 재현(2026-10-07 측정 · 스크래치 `/tmp/claude-1000/.../scratchpad/woztest/`)
```bash
T=RetroDeveloperEnvironmentDisktool/build/rdedisktool
$T convert dos33.dsk x.woz -f woz        # Sectors: 560 copied
$T list x.woz                            # Error: Sector not found: Track 207, Sector 195
$T convert x.woz rt.dsk -f do            # Sectors: 560 copied
python3 -c "print(open('dos33.dsk','rb').read()==open('rt.dsk','rb').read())"   # False
$T convert dos33.dsk x.nib -f nib ; $T convert x.nib rtn.dsk -f do               # NIB 도 False · list 같은 오류
```
- 원본 `dos33.dsk` = DOS 3.3 System Master 사본(143,360 B · sa2 IIe 정상 부팅).
- 왕복 T0S0 앞 16 B: 원본 `01 a5 27 c9 09 d0 18 a5 2b 4a 4a 4a 4a 09 c0 85` → 왕복 `ec 6e 80 40 99 51 ec 62 03 03 03 03 40 89 cc 76`.
- sa2 IIe Enhanced 부팅: `dos33.dsk` ✅ DOS 배너 · `x.woz` ❌ · `x.nib` ❌ ("Apple //e" 에서 정지).

## 3. 분석
### 3-1. WOZ 구조 비교(python 파서 · 같은 측정)
| 항목 | rdedisktool 출력 | Applesauce v2.02(Drelbs · woz-a-day · 정상 부팅) | sa2 DOS INIT 로 쓴 디스크(정상) | 사양/관례 |
|---|---|---|---|---|
| 트랙 0 의 10 비트 동기(`1111111100`) 수 | **87**(우연 패턴) | 452 | 718 | 갭마다 10 비트 동기 |
| 트랙 비트 수 | 모든 트랙 **53,248**(= 6656 B × 8 · 블록 패딩 그대로) | 50,018-50,033 | 53,248(이 파일 재사용) | 약 50,000-51,200(300 rpm · 4 µs) |
| TMAP 쿼터트랙 0..8 | `0 FF FF FF 1 FF FF FF 2` | `0 0 FF 1 1 1 FF 2 2` | (rdedisktool 형) | 인접 쿼터트랙(±1)도 같은 트랙으로(사양 권고) |
| INFO creator | `rdedisktool` + **NUL** 채움 | 공백 채움 | — | UTF-8 · **공백(0x20) 채움** |
| INFO 호환 하드웨어 | **0xFFFF** | 0x003E | — | 비트 0-8 만 정의 |
| INFO 동기화(sync) | 0 | 1 | 0 | — |
| CRC32 | 정상 | 정상 | 정상 | — |
- 주소/데이터 머리(D5 AA 96 / D5 AA AD)는 니블 단위로 보면 트랙 0 에 11 개씩 있다 — 즉 **배치는 있으나 인코딩이 틀리고 비트 정렬 수단(자기동기)이 없다**.

### 3-2. 원인 후보(코드)
1. `NibbleEncoder::buildTrack`/`encodeSector` 의 6-and-2 인코딩(또는 `decodeSector`)이 표준(DOS 3.3 RWTS · Beneath Apple DOS 3.3)과 다르다 — 왕복이 깨지는 것은 인코더·디코더가 **서로도** 맞지 않는다는 뜻. 섹터 인터리브 문제는 아님(왕복 결과가 다른 위치의 원본 섹터와 일치하는 경우 0 건).
2. WOZ: `AppleWozImage::create`/`writeSector` 가 `buildTrack` 의 **니블 바이트 배열을 그대로 비트 배열로** 저장(`bitCount = bytes × 8`). 동기 바이트 `$FF` 가 8 비트로만 들어가 **10 비트 자기동기 없음** → 실제 LSS(와 sa2 의 비트 단위 WOZ 판독)가 바이트 정렬을 잡지 못한다.
3. WOZ 읽기: 비트스트림 → 섹터 디코드가 실제 디스크(10 비트 동기·임의 정렬)를 처리하지 못한다(정상 WOZ 를 "VOLUME 0 · 파일 없음" 으로 읽음).
4. TMAP 인접 쿼터트랙 미매핑 · INFO 필드(creator 채움 · 호환 비트) — 부팅 실패의 주원인은 아닐 가능성이 크나 사양 정합성 문제.

## 4. 작업 제안(순서대로 · 각 단계 측정으로 확인)
1. **6-and-2 정정**(NIB·WOZ 공용): 표준 표로 `encodeSector`/`decodeSector` 를 재검증 · 단위 시험 = 무작위 256 B 섹터 왕복 + **알려진 정답**(DOS 3.3 실제 트랙 니블 덤프 — 예: sa2 가 DOS INIT 로 쓴 WOZ 에서 섹터를 비트 수준으로 디코드한 값을 원본 데이터와 대조). 완료 기준: DSK→NIB→DSK 바이트 동일 · sa2 IIe 에서 NIB 부팅.
2. **WOZ 비트스트림 생성**: 니블 → 비트 변환에서 갭의 `$FF` 를 **10 비트**(`1111111100`)로 · 트랙 비트 수는 실제 바이트 길이로(블록 패딩과 분리) · 표준 갭 길이(gap1 ≈ 40-48 · gap2 ≈ 5-10 · gap3 ≈ 14-24 자기동기) · `MacGcrEncoder.cpp:20` 방식 참고.
3. **WOZ 판독**: 비트스트림에서 1 비트씩 밀며 니블을 조립(LSS 흉내: 선두 1 이 올 때까지 0 버림) → 주소장/데이터장 탐색 · 실제 이미지(Applesauce · sa2 기록본)로 시험.
4. **사양 정합**: TMAP 인접 쿼터트랙(±1) 매핑 · creator 공백 채움 · 호환 비트(0xFFFF 금지 · 0 또는 실제 비트) · (선택) `META` 청크.
5. **시험 추가**(`tests/`): `test_apple_nib_roundtrip.sh` · `test_apple_woz_roundtrip.sh` · `test_apple_woz_read_real.sh`(fixture: 자작 디스크만 · 상용 이미지는 저장소에 넣지 않는다).

## 5. 검증 방법(독립 기대값)
- **왕복**: DSK→NIB/WOZ→DSK 바이트 동일(python `==`).
- **구조 검사기**(python · 스크래치에 있던 파서를 tests 로 옮겨도 됨): CRC · INFO(creator 공백 · 호환 비트) · TMAP 인접 매핑 · 트랙별 10 비트 동기 수 > 0 · 주소장/데이터장 16 개씩 · 비트 수 범위.
- **에뮬레이터 부팅**(독립 판독기): sa2(AppleWin) IIe Enhanced 에 `--d1 x.woz` → 화면에 `DOS VERSION 3.3` · `CATALOG` 결과가 DSK 부팅과 같음. 전용 Xvfb(`-displayfd`)에서 돌리고 사용자 화면 `:1` 은 건드리지 않는다 · 설정은 `-c` 사본(`-r` 은 yaml 에 영구 저장되므로 사용자 설정에 쓰지 않는다) · 디버그 서버 텍스트 판독은 **256 B 단위**로(긴 요청은 조용히 잘림).
- **대조군**: sa2 에서 DOS `INIT` 으로 만든 WOZ(10 비트 동기 정상)를 rdedisktool 이 `list` 했을 때 `HELLO` 가 보여야 한다.
- **음성 대조**: 10 비트 동기를 다시 8 비트로 되돌린 변이 → sa2 부팅 실패(시험이 동기 처리에 귀속되는지).

## 6. 주의
- 부트 디스크 보호(`diskwork/bootdisk/`) 정책 · `--bootdisk-mode` 의미는 그대로 둔다(기존 회귀 시험 `test_bootdisk_guard_apple.sh`).
- 상용 디스크 이미지(Drelbs 등)는 **저장소에 넣지 않는다**(사용자 제공 · 스크래치 전용).
- 커밋은 요청이 있을 때만 · 공유 인덱스라 `git commit --only`.
- 계획 → codex 교차검토(코딩 전) → 전건 재검증 → 구현 → 위 검증 표 순서.
