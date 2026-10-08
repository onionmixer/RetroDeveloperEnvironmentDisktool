# HANDOFF — rdedisktool Apple II 800K(3.5") 지원 작업 기준

> 작성 2026-10-08 · 출처: sa2 //c Plus 내장 3.5" 작업(`DKFS_retro/prototype_20_AppleII/PLAN_99_A2_SA2_IIC.md` §18)
> 상태: **완료(2026-10-08)** — 결과는 맨 아래 §7. §0–§6 은 조사 당시 기록(원문 유지). 남은 것: S7 MAME 수동 확인(§7-4).

## 0. 한 장 요약
| 항목 | 현재(2026-10-08 실측) | 목표 |
|---|---|---|
| 800K ProDOS `.po`(819,200 B) `info`/`list` | ❌ `Invalid file size for Apple II disk image` | 열기·목록·추출·추가·삭제 |
| `.2mg` | ❌ `Unable to detect disk format` · `-f 2mg` 없음 | 읽기·쓰기·변환(`.po` ⇄ `.2mg`) |
| 800K ProDOS 생성 | ❌ `create … -f po --fs prodos -g 80:2:10:512` → **204,800 B**(틀린 크기) · 부트 블록 0 | 1600 블록 볼륨 생성(+ 선택: 부팅 가능) |
| (선택) 3.5" GCR 비트스트림 | 없음 | WOZ2(3.5")/MOOF 등 — 별도 결정 |

## 1. 시험 이미지(원본 · 로컬 전용)
- **디렉터리**: `/mnt/USERS/onion/DATA_ORIGN/Workspace/05_RetroDeveloperEnvironmentProject/resource/AppleII/disk35/`
  (워크스페이스 상대 `resource/AppleII/disk35/` · 워크스페이스 `.gitignore` 의 `resource/AppleII/disk35/*` 로 **git 제외**)

| 파일 | 크기 | sha256 | 내용 |
|---|---|---|---|
| `A2DeskTop-1.5-en_800k.2mg` | 819,264 | `1f1ba5f37aa429761ac6114f1240fff34d044035308210383ab30886d14e9d3a` | 받은 원본 2MG |
| `A2DeskTop-1.5-en_800k.po` | 819,200 | `a6ae0a055e440fe59ff383e45b57c0e6f630695ec9436e78fb9668069b60ac98` | 위 2MG 의 데이터 부분(날 ProDOS 순서) |
| `A2DeskTop-1.5-en.zip` | 797,036 | `e6526029277106a1483992555d873924ac16907f1abf1d408986388c90cb29ab` | 원본 다운로드(32 MB `.hdv`·140K `.po` 6 장도 들어 있음) |
| `SOURCE.txt` · `A2DeskTop-1.5-README.txt` | — | — | 출처·형식 메모 · 릴리스 README |

- 출처: Apple II DeskTop 1.5 — https://github.com/a2stuff/a2d/releases/tag/v1.5 (2025-11-13). ⚠ 저장소에 LICENSE 파일이 없다 → **재배포 금지 · 저장소(tests/ fixture 포함)에 넣지 않는다**. 시험 fixture 가 필요하면 rdedisktool 이 직접 생성한 이미지만 쓴다.
- 볼륨(python 독립 해석): `/A2.DESKTOP` · 전체 1600 블록 · 부트 블록 0 비어 있지 않음(`01 38 B0 03 4C 1C 09 78 …`) · 루트 8 항목 `PRODOS`(SYS)·`CLOCK.SYSTEM`(SYS)·`READ.ME`(TXT)·`DESKTOP.SYSTEM`(SYS)·`MODULES/`·`EXTRAS/`·`APPLE.MENU/`·`SAMPLE.MEDIA/`.
- 2MG 헤더(실측): magic `2IMG` · creator `>BD<` · header size 64 · version 1 · image format **1(ProDOS 순서)** · flags 0 · blocks **1600** · data offset 64 · data length 819,200.
- ⚠ **원본을 직접 열어 쓰지 말 것** — 사본에서 작업. A2 DeskTop 은 부팅하면 설정을 써서 블록 2·6·1147-1150 이 바뀐다(MAME 실측). 쓰기 시험은 사본으로.

## 2. 재현(2026-10-08 · 스크래치 사본)
```bash
T=RetroDeveloperEnvironmentDisktool/build/rdedisktool
$T info A2DeskTop-1.5-en_800k.po   # Format: Apple II ProDOS Order → Error: Invalid file size for Apple II disk image
$T list A2DeskTop-1.5-en_800k.po   # Error: Failed to open disk image: Invalid disk format: Invalid file size …
$T info A2DeskTop-1.5-en_800k.2mg  # Format: Unknown / "Full format support not yet implemented"
$T list A2DeskTop-1.5-en_800k.2mg  # Error: Unable to detect disk format
$T create n800.po -f po --fs prodos -n NEW800 -g 80:2:10:512 --force   # 성공 표시지만 파일 204,800 B · info 오류
$T create n800.2mg -f 2mg --fs prodos -n NEW800 --force               # Error: Unknown disk format: 2mg
```

## 3. 원인(코드 위치 · 커밋 8f88d2f 기준)
1. `src/apple/ApplePOImage.cpp:38` — `fileSize != DISK_SIZE_140K` 면 거부(800K·32 MB 모두 실패). `include/rdedisktool/apple/AppleDiskImage.h:25` `DISK_SIZE_140K = 143360` 하나뿐.
2. `include/rdedisktool/apple/AppleConstants.h:72` `TOTAL_BLOCKS = 280` 고정 → `src/filesystem/apple/AppleProDOSHandler.cpp:60,67` 의 블록 읽기/쓰기가 280 이상을 거부 · `:128` 볼륨 헤더 total_blocks 가 0 이면 280 으로 대체. 볼륨 헤더의 `total_blocks`(오프셋 `$29`)를 기준으로 해야 한다.
3. `src/core/DiskImageFactory.cpp:99-109` — ProDOS 서명(블록 2 의 storage type `$F`) 검사는 크기 ≥143,360 이면 하지만, 로더가 140K 만 받는다(검출과 로더 불일치).
4. `src/cli/CLI.cpp:44` — 형식 목록에 `2mg` 없음 · `create -g` 의 기하가 ProDOS 블록 수로 이어지지 않음(800K 요청에 204,800 B).
5. `AppleProDOSHandler.cpp:1408-1410` — 포맷 시 부트 블록 0/1 을 0 으로 씀(부팅 불가 볼륨). 부팅 가능 볼륨이 필요하면 ProDOS 부트 로더 블록이 있어야 한다(출처·라이선스 결정 필요 — ProDOS 2.4.3 은 prodos8.com 배포).

## 4. 작업 제안(순서대로 · 각 단계 측정으로 확인)
1. **N 블록 ProDOS 순서 이미지**: `.po`/`.hdv`(크기 = 512 × N, N ≤ 65535) 열기 · 블록 수 = 파일 크기/512 와 볼륨 헤더 `total_blocks` 대조(불일치는 경고) · 핸들러의 280 고정 제거. 완료 기준: §1 `.po` 에서 `info`/`list`/`extract` 가 §1 의 루트 8 항목과 일치 · 32 MB `.hdv` 도 같은 내용.
2. **`.2mg` 컨테이너**: 64 B 헤더(magic `2IMG` · creator 4 B · header size u16 · version u16 · format u32(0 DOS 순서 · 1 ProDOS 순서 · 2 NIB) · flags u32(bit31 잠금 · bit8 DOS 볼륨 번호 유효 · 하위 8 비트 번호) · blocks u32 · data offset u32 · data length u32 · comment/creator-data offset·length u32 ×4) — 실측 예는 §1. 읽기·쓰기·`convert` (`.po` ⇄ `.2mg`) · 쓸 때 creator/주석 보존. 완료 기준: §1 `.2mg` ↔ `.po` 변환이 데이터 부분 바이트 동일(python `==`).
3. **800K ProDOS 생성**: `--fs prodos` + 1600 블록(예: `-g 80:2:10:512` 해석 수정 또는 `--blocks 1600`) · 볼륨 비트맵 블록 수 = ceil(1600/4096) = 1 · 루트 디렉터리 4 블록(2-5) · 사용 블록 표시. 완료 기준: 생성 → python ProDOS 해석기로 구조 확인 · `add`/`extract` 왕복 · (선택) MAME 에서 볼륨 인식.
4. (선택 · 별도 결정) **3.5" GCR 비트스트림**: Apple II 800K 는 Mac 800K 와 같은 Sony GCR(존 5 개: 12/11/10/9/8 섹터 · 394/429/472/525/590 rpm · 2:1 인터리브 · 주소 필드 `D5 AA 96` + 트랙/섹터/면·트랙상위/형식 `$22`/XOR · 데이터 필드 `D5 AA AD` + 섹터 + 태그 12 + 512 B 699 심벌 + 4 심벌 체크섬 · 트레일러 `DE AA`). rdedisktool 에는 이미 `src/macintosh/MacGcrEncoder.cpp`(MOOF) 가 있다 → 재사용 검토. 참고 구현: MAME `src/lib/formats/flopimg.cpp` `build_mac_track_gcr`/`extract_sectors_from_track_mac_gcr6`, `ap_dsk35.cpp`(로컬 `Emulator/x68000/mame/src/lib/formats/`) · sa2 `Emulator/AppleWin/source/IIc35Disk.{h,cpp}`(MAME 기반 C++ · 2026-10-08 · 미커밋).

## 5. 검증 방법(독립 기대값)
- **python ProDOS 해석기**(구현과 별개로 작성): 볼륨 헤더·루트 항목·파일 블록 체인 → rdedisktool `list`/`extract` 결과와 대조. 형식 해석 근거: ProDOS 8 Technical Reference(워크스페이스 `resource/AppleII/prodos/technical_reference_manual/`).
- **왕복**: `.po` → `.2mg` → `.po` 바이트 동일 · `add` 후 `extract` 바이트 동일 · 원본 사본 외 블록 무변경(python 블록 비교).
- **MAME 오라클**(시스템 MAME 0.288 `/usr/games/mame` · IIc+ `apple2cp` · 내장 3.5" = `-flop3`): 롬 세트 `resource/AppleII/rom/mame/apple2cp/`(MAME 이름 `341-0625-a.256` CRC32 0B996420 · `341-0265-a.chr` 2651014D · `341-0132-d.e12` C506EFB9 · git 제외 · 사용자 제공 롬). 헤드리스 부팅·화면 저장(워크스페이스 루트에서 · 2026-10-08 이 명령 그대로 실행 확인):
  ```bash
  cp resource/AppleII/disk35/A2DeskTop-1.5-en_800k.po /tmp/t800.po      # 사본(MAME 가 디스크에 쓴다)
  O=/tmp/mame_out; mkdir -p $O
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy /usr/games/mame apple2cp -noreadconfig -skip_gameinfo \
    -video none -sound none -nothrottle -str 60 -rompath resource/AppleII/rom/mame -flop3 /tmp/t800.po \
    -snapshot_directory $O -snapname t800 -cfg_directory $O/cfg -nvram_directory $O/nv -nonvram_save
  # → $O/t800.png = A2 DeskTop 바탕화면
  ```
  §1 `.po`·`.2mg` 둘 다 A2 DeskTop 바탕화면까지 부팅. 메모리 덤프는 `-autoboot_delay N -autoboot_script dump.lua`(Lua `manager.machine.devices[":maincpu"].spaces["program"]:read_u8(a)` · 끝에 `manager.machine:exit()`).
- 음성 대조: 블록 수 검사 제거 · 2MG 데이터 오프셋 무시 등 변이 → 시험이 해당 항목만 실패하는지.

## 6. 주의
- 부트 디스크 보호(`diskwork/bootdisk/`)·`--bootdisk-mode` 의미·기존 140K 경로는 그대로(회귀: `tests/test_bootdisk_guard_apple.sh` 등).
- 시험 이미지·롬은 **저장소에 넣지 않는다**(`.gitignore` 규칙 유지) · 커밋은 요청 시만 · 공유 인덱스라 `git commit --only`.
- 다른 세션이 같은 `build/rdedisktool` 을 재빌드할 수 있다(2026-10-08 한 번 겹침: 링크 중 실행 → "허가 거부").
- 계획 → codex 교차검토(코딩 전) → 전건 재검증 → 구현 → §5 순서. WOZ/NIB 결함은 별도 문서 `HANDOFF_APPLE_WOZ_NIB.md`.

## 7. 결과 (2026-10-08 · 후속 세션)

### 7-1. §0 목표별 결과
| 항목 | 결과 |
|---|---|
| 800K ProDOS `.po`(819,200 B) | ✅ 형식 `800po` — 열기·목록·추출·추가·삭제·이름 변경·mkdir·rmdir · 확장자 `.po` + 크기 819,200 B 로만 감지(Macintosh 800K 와 혼용 안 함: `.img`/`.dsk` 는 Apple 로 열지 않음) |
| `.2mg` | ✅ 형식 `800mg` — 읽기·쓰기(파일 전체 유지, 데이터 범위만 갱신) · 헤더 바이트 그대로 · 잠금 비트(bit 31) = 쓰기 보호 · 잘못된 헤더 거부 · `GMI2`(Bernie ][ The Rescue) 허용 · `.po` ⇄ `.2mg` 변환 |
| 800K ProDOS 생성 | ✅ `create x.po -f 800po --fs prodos -n NAME` / `-f 800mg` → 1600 블록 볼륨(빈 볼륨의 모든 바이트를 Technical Reference B.2.2 기준 python 이미지와 대조) · 부팅 블록은 쓰지 않음 |
| (선택) 3.5" GCR 비트스트림 | 하지 않음(사용자 결정: 800K 블록 이미지만) |

### 7-2. 함께 고친 것
- ProDOS 처리기의 280 블록 가정 제거 · 희소 파일 판독 · 디렉터리별 file_count 검증.
- 부트 보호: 800K 는 블록 0-1(512 B 섹터 0-1)만, 블록 2 는 쓰기 가능.
- ProDOS `mkdir`: 하위 디렉터리 머리 parent_pointer/parent_entry 결함 수정 · 머리·항목 바이트를 실 ProDOS 2.4.3 CREATE 와 같게(MAME 실측) · 볼륨 이름 검사.
- 변환 경계: `800po` 는 `*.po`, `800mg` 는 `*.2mg` 로만 · `convert -f` 대소문자 무시 · 모르는 값은 오류.
- 범위 밖이던 것: ProDOS validate 강화(모든 블록 계수·잃어버린 블록 경고) · ProDOS/DOS 3.3 체인 순환 방지 · ProDOS Total Space = 데이터 영역 · CLI 출력 파일 규칙(입력 = 출력 거부, 다른 형식 확장자 거부, 덮어쓰기 경고).

### 7-3. 커밋·시험
- 커밋: `aee1809`(800K S0-S6 등) · `abb5b2a`(mkdir = ProDOS CREATE, 순환 방지, validate, 출력 규칙, GMI2) · 이후 변경은 미커밋.
- 시험: `test_apple_800k.sh` · `test_apple_800k_2mg.sh` · `test_apple_800k_create.sh` · `test_apple_800k_convert_names.sh` · `test_bootdisk_guard_prodos_800k.sh` · `test_apple_prodos_sparse.sh` · `test_apple_prodos_mkdir_parent.sh` · `test_apple_prodos_validate_loops.sh` · `test_cli_output_files.sh` — 독립 판독기 `tests/tools/a2_prodos_ref.py` · A2 DeskTop 실디스크 경로는 선택(`A2_REAL_800K_DIR`, 없으면 "not judged").
- 사용법: `README.md`(800po/800mg 절) · 상세 기록: 로컬 `PLAN_APPLE_800K.md`(git 제외).

### 7-4. 남은 것
- S7 MAME 수동 확인(사용자 실행): rdedisktool 로 파일을 더한 A2 DeskTop 사본이 바탕화면까지 부팅하는지 · `-flop3` 에 rdedisktool 이 만든 800K 볼륨을 넣고 ProDOS 에서 `CAT`(명령은 §5 MAME 오라클).
