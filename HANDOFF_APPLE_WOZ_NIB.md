# HANDOFF — rdedisktool Apple II WOZ(및 NIB) 지원 개선

> 작성 2026-10-07 · 출처: sa2(AppleWin) IIc 지원 작업 중 발견(`DKFS_retro/prototype_20_AppleII/PLAN_99_A2_SA2_IIC.md` §11-9 참고)
> 상태: **완료(2026-10-07) · 후속 완료(2026-10-08)** — 결과·원인 정정·동작 변경은 맨 아래 §7(§7-6 이 최신). 아래 §0–§6 은 조사 당시 기록(원문 유지).

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

## 7. 결과 (2026-10-07 · 같은 날 후속 세션)

> 상세 계획·측정·codex 판정표는 로컬 작업 문서 `PLAN_APPLE_WOZ_NIB.md` · `PLAN_APPLE_DOS33_FILES.md`(`.gitignore` 의 `PLAN_*` — 저장소 밖).

### 7-1. 실제 원인(§0 의 가설 정정)
| §0 가설 | 결과 |
|---|---|
| 6-and-2 인코딩/디코딩 결함 | **인코더만** 틀림 — XOR 체인 역방향(체크섬이 항상 통과해 조용히 쓰레기). 디코더는 정상(올바른 NIB 560/560 판독) |
| (가설에 없음) | **주소장 섹터 번호** = DOS 논리 번호를 기록(정답 = 물리 0..15 순차 — 실디스크 2 종 측정) · NIB/WOZ `readSector` 번호 의미 불일치 |
| 10 비트 자기동기 없음 → 부팅 실패 | **sa2 부팅 실패의 원인이 아님** — 8 비트 동기 WOZ(회전 포함)도 sa2 에서 정상 부팅. 인코더 결함만 되살린 WOZ = "Apple //e" 정지, 주소 번호 결함만 = 부팅 실패 ⇒ 두 결함이 원인. 10 비트 동기는 사양·실기 정합으로 반영 |
| 정상 WOZ 가 "VOLUME 0 · 파일 없음" | 파일시스템 감지가 원시 파일 바이트를 읽던 것(+ 비트 단위 판독 없음) |

### 7-2. 변경 요약
- NIB/WOZ: RWTS 정본 6-and-2 인코더 · 물리 순서 트랙(갭 85/6/11 동기 · WOZ 50,034 비트) · 비트 단위(LSS) 판독 · 판독 실패 = 예외(0 채움 금지) · 16 섹터 모두 읽혀야 트랙 재구성 · INFO(creator 공백·호환 0·최대 트랙 계산)·TMAP 인접·WOZ1 레코드·FLUX 감지·TRKS 경계 검사.
- 섹터 번호 의미: `.do/.dsk/.nib/.woz` = DOS 3.3 논리 · `.po` = ProDOS 논리. `convert` 는 물리 섹터 기준으로 옮김(기존 DO↔PO 원시 복사 결함 해소) · DOS 3.3 처리기는 `.po` 에서 번호 변환 · ProDOS 부트 보호는 블록 0/1 의 실제 섹터를 봄.
- DOS 3.3 파일: 호스트 파일 = 본문(B 는 주소·길이 헤더 자동, A/I 는 길이) · `--raw` · 두 번째 이후 T/S 목록 +5..6 상대 섹터(수정 전 실제 DOS `BLOAD` 가 31,228 B 에서 어긋남) · 희소 파일 구멍 보존 · 형식 이름은 파일시스템별로 해석.

### 7-3. 동작 변경(사용자 영향)
- `convert` 가 Apple 형식 간 섹터를 재배열한다 — 예전 rdedisktool 이 DO→PO 를 원시 복사해 만든 'DOS 3.3 PO' 는 이제 다르게(바르게) 읽힌다.
- Apple 대상 `convert` 에서 읽지 못한 섹터가 있으면 경고 목록 + **종료 코드 2**.
- DOS 3.3 `extract` 는 B/A/I 헤더를 뺀 본문 · T 는 첫 `$00` 까지 · 그 밖 형식은 데이터 섹터 전부(`--raw` = 디스크의 파일 바이트 그대로). `add` 에 `--type` 이 없으면 B · 주소 없으면 `$2000` + 경고.
- `--type` 16 진수는 대상 파일시스템의 코드 그대로(ProDOS `$04` = TXT · 예전엔 BIN) · ProDOS 에서 `S` 는 오류.
- `-f woz`/`woz1` 출력은 항상 WOZ2(`woz1` 은 경고).

### 7-4. 검증
- 독립 python 판독기 `tests/tools/a2_nibref.py` — 실디스크(Applesauce 캡처 · sa2 DOS `INIT` 기록본) 니블을 바이트 단위로 재현(1102/1104 · 나머지 2 는 판독에 안 쓰이는 비트).
- 새 시험: `test_apple_nib_woz_roundtrip.sh` · `test_apple_woz_read.sh` · `test_apple_convert_order.sh` · `test_apple_dos33_files.sh` · `test_bootdisk_guard_prodos_order.sh` · 기존 `test_bootdisk_guard_apple.sh` 는 add 결과를 판정하도록 수정(이전엔 `|| true` + 가득 찬 이미지라 ProDOS 검증이 실행되지 않았음). 결함을 하나씩 되살린 변이 빌드 26 종 전부 검출.
- sa2 IIe Enhanced(네트워크 네임스페이스로 격리 실행): DOS 3.3 WOZ·NIB 부팅 → CATALOG 화면이 DSK 와 동일 · ProDOS 2.4.3 PO→WOZ·NIB 부팅 동일 · sa2 에서 DOS 가 쓴 WOZ·NIB 560 섹터 = 같은 조작의 DSK · 실제 DOS 가 쓴 파일 extract 일치 · rdedisktool 이 넣은 파일을 실제 DOS 가 `BLOAD`/`LOAD`+`LIST`/`READ` 로 정상 판독.

### 7-4a. 후속(같은 날)
- `.nb2`(트랙 6,384 니블) 지원 — 생성자 등록 · `create`/`convert`/파일시스템 처리기. sa2 에서 NB2 부팅 CATALOG = DSK · sa2 가 쓴 NB2 560 섹터 = DSK.
- 판독 규칙을 실제 DOS 3.3 RWTS 와 일치시킴(sa2 실측): 주소·데이터 에필로그 `DE AA` 필수(어긋나면 그 섹터만 판독 불가) · **4-and-4 고정 비트는 판독 조건이 아님**(값·체크섬이 맞으면 실제 DOS 가 읽음 — 앞서 넣었던 고정 비트 검사는 실기보다 엄격해 제거) · 대신 고정 비트가 어긋난 섹터는 `convert`·`info` 에서 **경고**(종료 코드 불변).
- 시험 고정본 `a2_nibref.py make-nib dataepi/addrepi/fixedbit/nb2` · 변이 7 종 추가 검출(누계 33).

- DOS VTOC 검증: 0/쓰레기 VTOC 를 DOS 3.3 으로 받아들이던 폴백을 막음(조건은 감지보다 느슨 — 볼륨 0 등 변형은 계속 읽음) · Apple 디스크에 파일시스템이 없으면 명확한 오류 · ProDOS 로 감지됐는데 못 읽으면 "섹터 순서(.po)" 안내. 그 과정에서 작업공간 예제 `Tutorial_apple_01.do` 가 PO 순서 이미지임이 드러나 올바른 DO 순서로 재생성 · `tests/baselines` 해당 2 개 갱신.

- 에뮬레이터 검증 도구 보존: `tests/emu/`(README 참조) — `emu_apple_boot_check.sh`(수동 · DOS 3.3 부팅 CATALOG·SAVE 쓰기·ProDOS 부팅을 WOZ/NIB/NB2 로 대조 · 9 판정 · 옛 인코더 빌드면 9/9 실패) · `a2run.sh`(네트워크 네임스페이스 격리 sa2 · 격리 확인 실패 시 sa2 를 띄우지 않음 · 종료/신호 시 전부 정리).

### 7-4b. 13 섹터(DOS 3.2) 읽기 전용(2026-10-08 · 이후 쓰기·생성·변환 지원 → §7-6)
- NIB/NB2/WOZ 의 트랙 0 이 `D5 AA B5` 주소장이면 13 섹터로 판정(35×13 · 물리 = DOS 3.2 논리) · 5-and-3 디코더(실디스크 nib↔d13 비트 상관으로 도출) · 에필로그 `DE AA` 필수. 새 형식 `.d13`(`AppleD13Image`).
- 파일시스템 "DOS 3.2"(DOS 3.3 처리기의 읽기 전용 모드): `info`/`list`/`extract`/`validate` · 쓰기 명령은 이미지를 바꾸지 않고 거부 · 13↔16 섹터 형식 간 변환 거부(13 섹터는 `.d13` 로만).
- 빈 섹터 비트맵: 16 비트 워드(바이트 0 상위)의 bit (섹터+3) — 실제 DOS 3.2 마스터 2 장의 빈 섹터(T14 S0~2)로 추정 · 빈 공간 표시에만 쓰임.
- 검증: 새 시험 `test_apple_d13_read.sh`(103 판정 · 실디스크 선택 경로 `A2_REAL_D13_DIR`) · C++ 디코더 = 참조 표 2048/2048(단일 비트 섹터 전수) · 실디스크 nib→d13 = 동봉 d13 바이트 동일 · 변이 12 종 중 11 검출(나머지 1 = 실질 등가) · 기존 시험 30/30.

### 7-4c. DOS 3.3 빈 섹터 비트맵 비트 순서(2026-10-08)
- 결함(HEAD 부터): 바이트 안 비트를 뒤집어 다룸(섹터 s ↔ 표준 섹터 s^7). 부분 사용 트랙에서 사용 중 섹터 할당 · 실제 DOS 가 rdedisktool 파일을 덮어씀(격리 sa2 실측: `BSAVE` 가 NEW 의 T18 S8~10 덮음 — 수정 후 0) · `validate` 가 실제 DOS 마스터에 오류.
- 수정: 표준 배치(바이트 0 bit k = 섹터 8+k · 바이트 1 bit k = 섹터 k — 실제 DOS 디스크 47 장 일치) + add 전 카탈로그·파일이 쓰는 섹터가 비트맵상 비어 있으면 사용 중으로 보정·경고(예전 rdedisktool 디스크 보호 · 사용자 선택).
- 예제 `Examples/Tutorial_apple_dos33_01/Tutorial_apple_dos33_01.do` 재생성(옛 도구 비트맵 · 실제 DOS SAVE 시 HELLO 3 섹터 손상 위험) — HOWTO 명령 그대로 · VTOC 1 섹터만 다름.
- 시험 `test_apple_dos33_bitmap.sh`(31 판정 · HEAD 빌드는 실패) · 변이 5/5 · `tests/emu` 4 번 항목(실제 DOS BSAVE 대조) · 기존 31/31.

### 7-5. 남은 것(2026-10-07 기록 → 2026-10-08 처리 결과)
- ~~비표준(복제 방지) 트랙 부분 갱신~~ → §7-6 ✅ · 약한 비트 무작위 재현 → **하지 않음**(읽을 수 없는 섹터로 둠) · ~~13 섹터 쓰기·생성~~ → §7-6 ✅.
- DOS 3.3 부트 보호는 트랙 0 만(트랙 0~2 확장은 rdedisktool 포맷 디스크에서 오탐 — 실측 후 취소) — 그대로.

### 7-6. 후속(2026-10-08)
- **NIB/WOZ 섹터 쓰기 = 데이터 필드만 교체**(RWTS 방식): 주소 필드·갭·다른 섹터(읽을 수 없는 것 포함)·복제 방지 니블·약한 비트 유지 · 대상 섹터만 읽히면 됨 · 쓴 뒤 재해독 확인, 실패 시 원상 복구 · 타이밍 비트가 있는 필드는 8 비트 니블로 다시 써서 트랙이 짧아짐(WOZ1 splice point 이동) · WRIT 청크 폐기. 시험 `test_apple_nibwoz_partial_write.sh`.
- **DOS 3.2 쓰기**: `.d13`·13 섹터 NIB/NB2/WOZ 에 add/delete/rename(5-and-3 인코더 · 비트맵 bit s+3 은 Apple 마스터 5 장으로 확정) · 실제 DOS 3.2 `INIT` 는 주소 필드만 쓰므로 아직 쓴 적 없는 섹터에는 주소 필드 뒤에 데이터 필드를 새로 씀(동기 14 + D5 AA AD..DE AA EB) · 공간이 없으면 거부·불변. 시험 `test_apple_d13_write.sh` · `test_apple_d13_nibwoz_write.sh`.
- **DOS 3.2 생성·변환**: `create x.d13 -f d13 --fs dos32` = 실제 INIT 의 VTOC·카탈로그와 바이트 동일(DOS 본체 제외) · `.d13` → 13 섹터 nib/nb2/woz(실기 캡처 배치: 9 비트 동기, 물리 순서 0,10,7,4,1,11,8,5,2,12,9,6,3 · System Master 캡처 왕복이 비트 동일). 시험 `test_apple_d13_create_convert.sh`.
- **WOZ 2.1 FLUX 판독**(읽기 전용): 간격 / optimal bit timing 을 반올림해 비트열로 · 쓰기와 FLUX 가 있는 WOZ 저장은 거부 · Applesauce 시험 이미지(짝수 트랙 FLUX)가 전부 읽히고 `.po` 로 변환하면 부팅. 시험 `test_apple_woz_read.sh` §4.
- **VTOC**: 쓸 때 rdedisktool 이 모르는 바이트 유지(바이트 0 등) · 할당 방향 0 처리 · `repair` 명령(예전 비트 순서로 비어 보이는 사용 중 섹터를 사용 표시, Human68k 과대 BPB). 시험 `test_repair_older_writes.sh`.
- 에뮬레이터 점검 `tests/emu/emu_apple_boot_check.sh` 5~8 추가(DOS 3.2 · FLUX) — 22/22.
- 하지 않기로 확정: 실제 DOS 와 같은 할당 순서 · FLUX 트랙 쓰기 · 트랙 0 이 빈 13 섹터 이미지의 검출.
- 실디스크(커밋 안 함): `resource/AppleII/dos32`(Asimov DOS 3.1/3.2 마스터) · `resource/AppleII/woz_flux`(Applesauce FLUX 샘플) — 각 `SOURCE.txt` 에 출처·sha256.
