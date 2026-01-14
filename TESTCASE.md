# TESTCASE

README.md를 기준으로 `rdedisktool`이 제공하는 기능 중 read-only 이미지(XSA) 관련 흐름을 제외한 모든 기능을 검증하는 수동 테스트 시나리오다. Apple II, MSX, X68000 플랫폼을 모두 아우르며, 각 시나리오는 서로 독립적으로 실행할 수 있고 공통 준비 과정을 한 번만 수행하면 된다. 모든 단계는 단순 명령 실행에 그치지 않고, `list`/`info`/`validate`/`cmp` 등을 통해 결과 상태를 검증해야 한다.

## 공통 준비
1. `rdedisktool` 바이너리가 `PATH`에 있거나 절대 경로로 호출 가능해야 한다.
2. 테스트용 호스트 파일은 `tests/fixtures/` (또는 임시 작업 디렉터리) 아래에 저장한다.
   - `HELLO.BAS`, `NEWPROG.BIN`, `README.TXT`, `CHAPTER1.TXT`, `DRAGON.COM`, `SHOOTER.COM`, `PATCH.BIN`, `PLAYER.BIN`, `GAME.X`, `DATA.DAT` 등은 1KB 이하의 더미 데이터를 `xxd -r -p` 또는 `printf`로 생성해도 무방하다.
3. 모든 명령은 실패 시 종료 코드를 확인하고, 성공 시에도 `rdedisktool info/list/validate` 또는 `cmp` 등 도구를 사용해 사후 상태 검증을 수행한다. 에러 시 재현 데이터(명령, 로그)를 기록한다.

## 시나리오 1. Apple DOS 3.3 이미지의 기본 파일 작업
- **목적**: Apple DOS 3.3 이미지에서 `info`, `list`, `add`, `extract`, `delete`가 정상 동작하고 각 작업 이후 파일 시스템 메타데이터가 일관성 있게 유지되는지 확인한다.
- **커버리지**: Apple II DOS 포맷 지원, 파일 나열·추출·추가·삭제, `--verbose` 출력, 여유 공간 계산 검증.
- **준비**: 빈 DO 이미지를 만들어 사용할 디렉터리를 준비한다.

| 단계 | 절차 | 기대 결과 |
|------|------|------------|
|1|`rdedisktool create apple33.do -f do --fs dos33`|140KB 크기의 DOS 3.3 이미지가 생성되고 성공 메시지가 출력된다.|
|2|`rdedisktool info apple33.do -v`|트랙/섹터 수, 여유 공간이 전형적인 빈 DOS 3.3 값(약 140KB)으로 표기된다.|
|3|`rdedisktool add apple33.do tests/fixtures/HELLO.BAS HELLO.BAS`|파일이 루트에 추가되며 여유 공간 감소량이 파일 크기와 일치한다.|
|4|`rdedisktool list apple33.do`|루트 목록에 HELLO.BAS가 보이고 속성/크기/타임스탬프가 기대와 일치한다.|
|5|`rdedisktool info apple33.do`|파일 수, 여유 블록 수가 `list` 결과와 일치한다.|
|6|`rdedisktool extract apple33.do HELLO.BAS ./HELLO_out.BAS && cmp tests/fixtures/HELLO.BAS ./HELLO_out.BAS`|호스트에 복사되고 내용이 완전히 동일하다.|
|7|`rdedisktool delete apple33.do HELLO.BAS`|루트에서 파일이 제거되고 `list` 시 항목이 사라진다.|
|8|`rdedisktool validate apple33.do`|삭제 후에도 디스크 무결성이 유지됨을 확인한다. 오류가 보고되면 카탈로그/VTOC 상태를 점검한다.|

## 시나리오 2. Apple ProDOS 이미지에서 서브디렉터리 작업
- **목적**: ProDOS 이미지 생성 후 디렉터리 생성/삭제, 파일 추가/삭제, 추출, `rmdir` 제약을 검증하고, 디렉터리 상태를 `list`/`info`/`validate`로 확인한다.
- **커버리지**: `create`(`-f po --fs prodos -n`), `mkdir`, `add`, `list`, `extract`, `delete`, `rmdir`, ProDOS 서브디렉터리, 디렉터리 비우기 전 `rmdir` 실패 검증.
- **준비**: `rdedisktool create prodos.po -f po --fs prodos -n TESTDISK` 실행.

| 단계 | 절차 | 기대 결과 |
|------|------|------------|
|1|`rdedisktool mkdir prodos.po DOCS`|루트에 DOCS 디렉터리가 생성되고 `list prodos.po`에서 DIR로 표기된다.|
|2|`rdedisktool mkdir prodos.po DOCS/MANUAL`|중첩 디렉터리가 생성되고 `rdedisktool list prodos.po DOCS` 결과에 MANUAL 표시.|
|3|`rdedisktool add prodos.po tests/fixtures/README.TXT DOCS/README.TXT`|파일이 DOCS 하위에 생성되고 `rdedisktool info prodos.po`의 사용 블록 수가 증가한다.|
|4|`rdedisktool add prodos.po tests/fixtures/CHAPTER1.TXT DOCS/MANUAL/CH1.TXT`|MANUAL 하위에 파일 추가 후 `rdedisktool list prodos.po DOCS/MANUAL -v`에서 블록/파일 속성 확인.|
|5|`rdedisktool rmdir prodos.po DOCS`|디렉터리에 MANUAL과 README가 존재하므로 실패하며, 에러 메시지가 "directory not empty" 류로 출력되는지 확인.|
|6|`rdedisktool extract prodos.po DOCS/MANUAL/CH1.TXT ./CH1_out.TXT && cmp tests/fixtures/CHAPTER1.TXT ./CH1_out.TXT`|추출 결과가 동일하다.|
|7|`rdedisktool delete prodos.po DOCS/MANUAL/CH1.TXT`|파일 삭제 후 `list` 결과 비어 있음.|
|8|`rdedisktool rmdir prodos.po DOCS/MANUAL`|비어 있는 디렉터리가 정상 삭제된다.|
|9|`rdedisktool delete prodos.po DOCS/README.TXT && rdedisktool rmdir prodos.po DOCS`|모든 하위 항목 제거 후 상위 디렉터리 삭제 성공을 확인한다.|
|10|`rdedisktool validate prodos.po`|전체 구조 검증이 통과한다.|

## 시나리오 3. MSX-DOS(DMK) 이미지에서 다중 서브디렉터리 및 글로벌 옵션
- **목적**: MSX-DOS FAT12 이미지에서 디렉터리 조작, 글로벌 옵션, FAT 상태를 교차 검증한다.
- **커버리지**: `create`(`-f dmk --fs msxdos`), `mkdir`, `add`, `list`(경로/옵션), `extract`, `delete`, `rmdir`, MSX 플랫폼 지원, 글로벌 옵션, FAT 엔트리 확인.
- **준비**: `rdedisktool create msx.dmk -f dmk --fs msxdos -n GAMES` 실행.

| 단계 | 절차 | 기대 결과 |
|------|------|------------|
|1|`rdedisktool mkdir msx.dmk GAMES && rdedisktool mkdir msx.dmk GAMES/RPG`|MSX-DOS 디렉터리가 생성되고 FAT 엔트리가 증가한다.|
|2|`rdedisktool add msx.dmk tests/fixtures/DRAGON.COM GAMES/RPG/DRAGON.COM`|파일 추가 로그와 남은 공간 정보 확인, `rdedisktool info msx.dmk -v`에서 Volume=GAMES, Free Space 감소, FAT Cluster Map에 사용된 클러스터 체인이 나타난다.|
|3|`rdedisktool list msx.dmk GAMES -v`|디렉터리 엔트리 수, FAT 클러스터 정보 등 상세 로그 확인.|
|4|`rdedisktool list msx.dmk GAMES/RPG -q`|조용한 모드에서 필수 정보만 출력되며 `-q` 플래그가 로그를 억제함을 확인.|
|5|`rdedisktool extract msx.dmk GAMES/RPG/DRAGON.COM ./DRAGON_out.COM && cmp tests/fixtures/DRAGON.COM ./DRAGON_out.COM`|파일 내용 동일.|
|6|`rdedisktool delete msx.dmk GAMES/RPG/DRAGON.COM`|파일 삭제되고 `list` 결과 빈 상태 확인.|
|7|`rdedisktool info msx.dmk -v`|클러스터 맵과 Free Space가 삭제 이전과 동일한 값으로 되돌아온 것을 확인한다.|
|8|`rdedisktool rmdir msx.dmk GAMES/RPG && rdedisktool rmdir msx.dmk GAMES`|빈 디렉터리가 정상 삭제되며 `rdedisktool validate msx.dmk`가 통과한다. 필요 시 GAMES 같은 볼륨 라벨과 동일한 디렉터리명을 사용할 때 제한이 있는지 README/CLI 도움말로 확인한다.|

## 시나리오 4. X68000 XDF 이미지의 기본 파일 작업
- **목적**: X68000 XDF 이미지에서 Human68k 파일시스템을 사용하여 `info`, `list`, `add`, `extract`, `delete`가 정상 동작하고 각 작업 이후 FAT12 메타데이터가 일관성 있게 유지되는지 확인한다.
- **커버리지**: X68000 XDF 포맷 지원, Human68k 파일시스템, 1024바이트 섹터, 8.3 파일명 변환, 파일 나열·추출·추가·삭제.
- **준비**: 빈 XDF 이미지를 Human68k 파일시스템으로 생성.

| 단계 | 절차 | 기대 결과 |
|------|------|------------|
|1|`rdedisktool create x68k.xdf -f xdf --fs human68k -n X68KDISK`|약 1.2MB 크기의 Human68k XDF 이미지가 생성되고 성공 메시지가 출력된다.|
|2|`rdedisktool info x68k.xdf`|Format: X68000 XDF, Platform: X68000, Geometry: 154 tracks, 2 sides, 8 sectors/track, 1024 bytes/sector로 표기된다.|
|3|`rdedisktool list x68k.xdf`|빈 디스크로 파일 수 0, Free space가 약 1.3MB로 표시된다. Volume: X68KDISK 확인.|
|4|`rdedisktool add x68k.xdf tests/fixtures/GAME.X GAME.X`|파일이 루트에 추가되며 여유 공간 감소량이 파일 크기(클러스터 단위)와 일치한다.|
|5|`rdedisktool add x68k.xdf tests/fixtures/long_filename_test.dat`|긴 파일명이 8.3 형식으로 변환됨(예: LONG_FIL.DAT)을 확인.|
|6|`rdedisktool list x68k.xdf`|루트 목록에 GAME.X, LONG_FIL.DAT가 보이고 속성/크기가 기대와 일치한다.|
|7|`rdedisktool extract x68k.xdf GAME.X ./GAME_out.X && cmp tests/fixtures/GAME.X ./GAME_out.X`|호스트에 복사되고 내용이 완전히 동일하다.|
|8|`rdedisktool delete x68k.xdf GAME.X`|루트에서 파일이 제거되고 `list` 시 항목이 사라진다.|
|9|`rdedisktool list x68k.xdf`|GAME.X가 삭제되어 LONG_FIL.DAT만 남아 있고 Free space가 증가함을 확인.|
|10|`rdedisktool validate x68k.xdf`|삭제 후에도 디스크 무결성이 유지됨을 확인한다.|

## 시나리오 5. X68000 DIM 이미지에서 서브디렉터리 작업
- **목적**: X68000 DIM 이미지 생성 후 Human68k 파일시스템에서 디렉터리 생성/삭제, 파일 추가/삭제, 추출, `rmdir` 제약을 검증하고, DIM 헤더 구조가 올바르게 처리되는지 확인한다.
- **커버리지**: `create`(`-f dim --fs human68k`), `mkdir`, `add`, `list`, `extract`, `delete`, `rmdir`, DIM 헤더 처리, Human68k 서브디렉터리 지원.
- **준비**: `rdedisktool create x68k.dim -f dim --fs human68k -n TESTDIM` 실행.

| 단계 | 절차 | 기대 결과 |
|------|------|------------|
|1|`rdedisktool info x68k.dim`|Format: X68000 DIM, Platform: X68000이 표시되고 DIM 타입(2HD 등)이 표기된다.|
|2|`rdedisktool mkdir x68k.dim GAMES`|루트에 GAMES 디렉터리가 생성되고 `list x68k.dim`에서 DIR로 표기된다.|
|3|`rdedisktool mkdir x68k.dim GAMES/ACTION`|중첩 디렉터리가 생성되고 `rdedisktool list x68k.dim GAMES` 결과에 ACTION 표시.|
|4|`rdedisktool add x68k.dim tests/fixtures/GAME.X GAMES/ACTION/SHOOTER.X`|파일이 GAMES/ACTION 하위에 생성되고 `rdedisktool list x68k.dim GAMES/ACTION`에서 확인.|
|5|`rdedisktool rmdir x68k.dim GAMES`|디렉터리에 ACTION이 존재하므로 실패하며, 에러 메시지가 "directory not empty" 류로 출력되는지 확인.|
|6|`rdedisktool extract x68k.dim GAMES/ACTION/SHOOTER.X ./SHOOTER_out.X && cmp tests/fixtures/GAME.X ./SHOOTER_out.X`|추출 결과가 동일하다.|
|7|`rdedisktool delete x68k.dim GAMES/ACTION/SHOOTER.X`|파일 삭제 후 `list GAMES/ACTION` 결과 비어 있음.|
|8|`rdedisktool rmdir x68k.dim GAMES/ACTION`|비어 있는 디렉터리가 정상 삭제된다.|
|9|`rdedisktool rmdir x68k.dim GAMES`|하위 항목 제거 후 상위 디렉터리 삭제 성공을 확인한다.|
|10|`rdedisktool validate x68k.dim`|전체 구조 검증이 통과한다.|

## 시나리오 6. 포맷 변환 및 다중 플랫폼 검증
- **목적**: Apple II 섹터 순서 변환, MSX 이미지 형식 변환, X68000 XDF↔DIM 변환 기능(`convert`)을 검증하고 변환 전후 데이터를 cross-check 한다.
- **커버리지**: `convert` 명령, Apple DO↔PO 섹터 순서 변환, MSX DSK↔DMK 형식 변환, X68000 XDF↔DIM 형식 변환, 변환 후 `info`/`list`/`validate` 검증.
- **참고**: Apple II의 DO↔PO 변환은 섹터 순서만 변경하며, 파일시스템(DOS 3.3/ProDOS)은 변환되지 않는다. X68000의 XDF↔DIM 변환은 DIM 헤더 추가/제거가 수행된다.

| 단계 | 절차 | 기대 결과 |
|------|------|------------|
|1|MSX DSK 생성: `rdedisktool create msx.dsk -f msxdsk --fs msxdos -n SAMPLE`|720KB MSX-DOS 이미지 생성.|
|2|`rdedisktool add msx.dsk tests/fixtures/PATCH.BIN PATCH.BIN`|DSK에 파일이 존재함을 `list`로 확인.|
|3|`rdedisktool convert msx.dsk msx_out.dmk -f dmk`|DMK 변환 성공.|
|4|`rdedisktool list msx_out.dmk`|파일 목록과 속성, 크기가 변환 전과 동일하다.|
|5|`rdedisktool validate msx_out.dmk`|변환된 DMK 이미지가 검증을 통과한다.|
|6|`rdedisktool convert msx_out.dmk msx_back.dsk -f msxdsk && cmp msx.dsk msx_back.dsk`|재변환이 성공하고 원본 DSK와 바이트 수준 일치.|
|7|Apple II 섹터 순서 변환: `rdedisktool create test.do -f do --fs dos33 && rdedisktool add test.do tests/fixtures/HELLO.BAS HELLO.BAS`|DOS 3.3 이미지에 파일 추가.|
|8|`rdedisktool convert test.do test.po -f po`|DO→PO 섹터 순서 변환 성공.|
|9|`rdedisktool convert test.po test_back.do -f do && cmp test.do test_back.do`|PO→DO 재변환 후 원본과 바이트 수준 일치.|
|10|X68000 XDF→DIM 변환: `rdedisktool create x68k_conv.xdf -f xdf --fs human68k -n CONVTEST && rdedisktool add x68k_conv.xdf tests/fixtures/GAME.X GAME.X`|XDF 이미지에 파일 추가.|
|11|`rdedisktool convert x68k_conv.xdf x68k_conv.dim -f dim`|XDF→DIM 변환 성공. DIM 헤더가 추가됨.|
|12|`rdedisktool list x68k_conv.dim`|변환된 DIM에서 파일 목록이 동일하게 표시된다.|
|13|`rdedisktool info x68k_conv.dim`|Format: X68000 DIM, DIM Type 정보가 표시된다.|
|14|`rdedisktool convert x68k_conv.dim x68k_back.xdf -f xdf`|DIM→XDF 재변환 성공.|
|15|`rdedisktool list x68k_back.xdf`|재변환된 XDF에서 파일 목록이 원본과 동일.|

## 시나리오 7. MSX XSA 압축/복원 및 read-only 검증
- **목적**: MSX DSK/DMK 이미지를 XSA로 압축하고, XSA에서 정보 조회·목록 열람·파일 추출이 가능함을 확인하며, 쓰기 작업은 차단되는지 검증한다.
- **커버리지**: `convert`(DSK→XSA, XSA→DSK/DMK), `info`, `list`, `extract`, read-only 오류 처리, 압축 비율 확인.
- **준비**: 시나리오 6에서 생성한 `msx.dsk` (PATCH.BIN 포함)을 사용하거나 동일한 데이터가 있는 MSX-DOS 이미지를 준비한다.

| 단계 | 절차 | 기대 결과 |
|------|------|------------|
|1|`rdedisktool convert msx.dsk msx_compressed.xsa`|변환 성공 메시지와 함께 생성된 XSA 파일 크기가 DSK 대비 크게 감소한다.|
|2|`rdedisktool info msx_compressed.xsa`|포맷이 XSA로 표시되고 `Write Protected: Yes`/`Read-only: Yes` 정보가 출력된다. README에서 언급하듯 `list`/`extract`만 허용됨을 확인한다.|
|3|`rdedisktool list msx_compressed.xsa`|PATCH.BIN 목록이 표시되고 볼륨 이름/파일 크기가 원본과 일치한다.|
|4|`rdedisktool extract msx_compressed.xsa PATCH.BIN ./PATCH_from_xsa.BIN && cmp tests/fixtures/PATCH.BIN ./PATCH_from_xsa.BIN`|추출 파일이 호스트에서 원본과 1:1로 일치한다.|
|5|`rdedisktool add msx_compressed.xsa tests/fixtures/DRAGON.COM DRAGON.COM`|XSA가 read-only이므로 "operation not permitted" 등 오류 메시지가 출력되어야 한다 (성공하면 안 됨).|
|6|`rdedisktool delete msx_compressed.xsa PATCH.BIN`|삭제 시도 또한 read-only 오류로 거부된다.|
|7|`rdedisktool convert msx_compressed.xsa msx_roundtrip.dsk -f msxdsk && cmp msx.dsk msx_roundtrip.dsk`|XSA를 다시 DSK로 복원하면 원본과 바이트 수준 동일.|
|8|`rdedisktool convert msx_compressed.xsa msx_roundtrip.dmk -f dmk && rdedisktool list msx_roundtrip.dmk`|DMK 변환도 성공하며 목록/크기가 동일하다.|
|9|필요 시 `rdedisktool convert msx_roundtrip.dmk msx_roundtrip.xsa` 반복|재압축/복원 루프에서도 데이터 무결성이 유지되는지 확인한다.|

## 시나리오 8. 이미지 무결성 검사(`validate`)
- **목적**: 정상 이미지에서 통과하고, 손상된 이미지에서 실패하는지 확인한다.
- **커버리지**: `validate` 명령, 오류 보고, 로그 확인.
- **준비**: 정상 이미지 `prodos.po` (시나리오 2 산출물).

| 단계 | 절차 | 기대 결과 |
|------|------|------------|
|1|`rdedisktool validate prodos.po`|"Validation passed" 또는 유사 메시지, 종료 코드 0이며, 로그에서 체크된 영역(디렉터리/블록 맵)이 명시된다.|
|2|백업 후 손상: `cp prodos.po prodos_bad.po && printf '\xff' | dd of=prodos_bad.po bs=1 seek=512 count=1 conv=notrunc`|512바이트 위치가 덮어쓰여 손상 이미지 준비.|
|3|`rdedisktool validate prodos_bad.po`|무결성 오류 메시지를 출력하고 종료 코드가 0이 아님을 확인하며, 로그에 손상 위치(블록/섹터)가 기록되는지 확인한다.|

## 시나리오 9. 섹터/트랙 덤프(`dump`)
- **목적**: `dump` 명령이 지정된 트랙/섹터 데이터를 올바르게 표시하고, 덤프 결과를 실제 파일 작업과 대조한다.
- **커버리지**: `dump --track/--sector/--side`, 출력 포맷 검증, FAT/디렉터리 상태 확인.
- **준비**: 데이터가 있는 이미지(시나리오 3의 `msx.dmk` 등)를 사용.
- **참고**: MSX-DOS 디스크 구조는 Track 0에 부트섹터(Sector 0), FAT(Sectors 1-6), 루트 디렉터리(Sectors 7-13)가 위치함. Apple DOS 3.3과 달리 Track 17은 데이터 영역임.

| 단계 | 절차 | 기대 결과 |
|------|------|------------|
|1|`rdedisktool info msx.dmk -v`|README 예시처럼 Cluster Information과 FAT Cluster Map이 출력되는지 확인한다.|
|2|`rdedisktool dump msx.dmk -t 0 -s 0`|부트 섹터 덤프: `EB FE 90` 점프 명령과 `MSXDOS` OEM 이름, BPB 구조가 표시된다.|
|3|`rdedisktool dump msx.dmk -t 0 -s 1`|FAT 영역 덤프: 첫 바이트가 미디어 디스크립터(0xF9), 이후 `FF FF`가 표시된다.|
|4|`rdedisktool dump msx.dmk -t 0 -s 7`|루트 디렉터리 덤프: 볼륨 라벨(GAMES) 또는 파일/디렉터리 엔트리가 표시된다.|
|5|덤프 데이터를 기반으로 FAT 엔트리(예: DRAGON.COM이 사용한 클러스터)의 헥사 값을 계산하고, `info -v`에서 보고된 클러스터 맵과 일치하는지 수동 계산으로 검증한다.|

---

## 시나리오 10. 오류 처리 및 경계값 확인
- **목적**: 잘못된 작업 시 적절한 오류가 발생하고, 경계 조건(중복 파일명, 가득 찬 디스크, 대용량 파일)이 방어되는지 점검한다.
- **커버리지**: `add` 중복, 가득 찬 이미지에서 추가 실패, `delete` 대상 없음, `mkdir` 경로 오류, 대용량 파일(T/S list 복수개) 등.
- **준비**: 새 DOS 3.3 이미지 생성. 테스트용 더미 파일은 `dd` 명령으로 생성.

| 단계 | 절차 | 기대 결과 |
|------|------|------------|
|1|`rdedisktool create apple33.do -f do --fs dos33`|새 DOS 3.3 이미지 생성.|
|2|`dd if=/dev/zero of=hello.bin bs=256 count=4` 후 `rdedisktool add apple33.do hello.bin HELLO.BIN` 실행, 동일 명령 반복|두 번째 시도에서 "file already exists"류 오류 메시지와 비-0 종료 코드 확인.|
|3|`rdedisktool add --force apple33.do hello.bin HELLO.BIN`|`--force`를 사용하면 동일 파일을 덮어쓸 수 있음을 확인.|
|4|`rdedisktool delete apple33.do NOFILE.TXT`|존재하지 않는 파일 삭제 시 "file not found" 메시지와 비-0 종료 코드 확인.|
|5|`dd if=/dev/zero of=fill.bin bs=1024 count=10` 후 `for i in $(seq 1 15); do rdedisktool add apple33.do fill.bin "FILE${i}.BIN"; done`|12~13개 파일 추가 후 공간 부족 오류 발생. `info`에서 여유 공간 ~0 확인.|
|6|`rdedisktool mkdir apple33.do SUBDIR`|DOS 3.3은 디렉터리를 지원하지 않으므로 오류 메시지 출력.|
|7|`rdedisktool validate apple33.do`|오류 작업 후에도 디스크 무결성이 유지됨을 확인.|
|8|대용량 파일 테스트: `rdedisktool create bigtest.do -f do --fs dos33 && dd if=/dev/zero of=big.bin bs=256 count=130`|130섹터(33,280바이트) 파일 생성. T/S list 2개 필요.|
|9|`rdedisktool add bigtest.do big.bin BIG.BIN && rdedisktool validate bigtest.do`|대용량 파일 추가 후 `validate`가 경고 없이 "All validation checks passed"로 통과.|

---

각 시나리오는 Apple II(DOS/ProDOS), MSX(DSK/DMK/XSA), X68000(XDF/DIM/Human68k)의 수정 가능·불가능 흐름 전체를 아우르며, 특히 XSA(read-only) 시나리오에서는 쓰기 작업이 실패하는지도 함께 검증한다. 필요 시 각 시나리오 종료 후 생성한 임시 파일을 삭제하여 깨끗한 상태를 유지한다. 모든 작업은 결과 상태를 재차 검증해 실제 사용 환경에서도 안전하게 도구를 활용할 수 있음을 입증해야 한다.
