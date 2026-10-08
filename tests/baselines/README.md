# Baseline Outputs (Pre-Macintosh-Support)

이 디렉토리는 Macintosh 지원 추가 작업 **이전** 의 `rdedisktool info -v` / `list` 출력을
영구 보존한다. 머지 전후 byte-for-byte 비교로 기존 Apple/MSX/X68000 기능 무회귀를 보장한다.

## 캡처 시점

- Git tag: `pre-mac` (commit `a770ef0fcbb9a5019fd8d5b57248efb72806ed9b`)
- 커밋 메시지: `chore: remove temporary analysis documents` (2026-04-09)
- 빌드: `build/rdedisktool` (806104 bytes)

## 픽스처 4종

| 파일 | 위치 | 포맷 | 비고 |
|---|---|---|---|
| `Tutorial_apple_01.do` | `Examples/Tutorial_apple_01/` | Apple II DOS Order | ProDOS 디스크 |
| `Tutorial_apple_01.po` | `Examples/Tutorial_apple_01/` | Apple II ProDOS Order | 동일 디스크 다른 포맷 |
| `Tutorial_msx_01.dsk` | `Examples/Tutorial_msx_01/` | MSX DSK | FAT12 |
| `work.xdf` | `Emulator/x68000/` | X68000 XDF | FileSystem Unknown — list 시 에러 메시지 (정상 동작) |

## 경로 정규화 정책

baseline 파일 안의 경로는 **PROJECT_ROOT 기준 상대 경로**로 정규화되어 있다.
회귀 비교 시 실제 출력의 절대 경로를 동일하게 정규화한 뒤 `diff -u` 한다.
PROJECT_ROOT = `RetroDeveloperEnvironmentDisktool` 의 부모 디렉토리.

## 사용법 (회귀 비교)

자동 비교 스크립트는 없다(예전 계획의 `tests/test_baseline_diff.sh` 는 만들어지지 않았다). `RetroDeveloperEnvironmentDisktool/` 에서 수동으로 8 개 전부 비교:

```bash
PROJ_ROOT="$(cd .. && pwd)"
for b in tests/baselines/*.txt; do
  n=$(basename "$b" .txt); f=${n#*_}
  case $n in info_v_*) f=${f#v_}; args="info -v";; *) args=list;; esac
  case $f in
    Tutorial_apple_01.*) p=Examples/Tutorial_apple_01/$f;;
    Tutorial_msx_01.dsk) p=Examples/Tutorial_msx_01/$f;;
    work.xdf)            p=Emulator/x68000/$f;;
  esac
  ./build/rdedisktool $args "$PROJ_ROOT/$p" 2>&1 | sed "s|${PROJ_ROOT}/||g" \
    | diff -u "$b" - >/dev/null && echo "same $n" || echo "DIFF $n"
done
```

2026-10-08 실행: 8 개 모두 `same`.

## 갱신 정책

- baseline 은 **기존 기능의 동결된 동작 명세**다. Macintosh PR 머지 시점까지 변경 금지.
- Macintosh 머지 후, Mac 추가로 인해 기존 출력이 정당하게 바뀌어야 하는 경우(예: `--list-formats` 출력에 Mac 포맷 추가) 별도 갱신 commit 으로 처리하고 PR 설명에 사유 명시.
- 갱신 시에는 항상 **build/rdedisktool** 의 최신 빌드로 재캡처.

## 갱신 기록

- 2026-10-07 — `Tutorial_apple_01.do` 두 파일(`list`·`info -v`) 갱신. 원래 이 `.do` 는 **ProDOS 순서 이미지에 `.do` 이름이 붙은 파일**이었고(같은 파일을 `.po` 로 읽으면 `/TUTORIAL`), 옛 baseline 의 "DISK VOLUME 0 · 0 file(s)" 는 DOS 3.3 처리기가 0 으로 된 VTOC 를 기본값으로 받아들이던 결함의 산물이었다. 작업공간 예제 파일을 올바른 DOS 순서로 다시 만들고(내용 동일 — PO 로 되돌리면 원래 바이트와 같음), baseline 을 그 출력으로 바꿨다. `.po` · MSX · X68000 baseline 은 바이트 단위로 그대로.
- 2026-10-08 — `list_Tutorial_apple_01.{do,po}` 갱신. Apple II `list` 의 Type 열이 디스크 자신의 형식(ProDOS `TXT`/`BIN`…, DOS `T`/`B`…)을, Attr 열이 잠김(`L`)만 보이도록 바뀌었다(예전에는 형식·access 바이트를 FAT 속성 R/H/S 로 잘못 풀어 `FILE RHL`). 이때 두 이미지의 HELLO 가 서로 다르게(`.do` = `TXT`, `.po` = `BIN`) 나왔는데, 이는 실제 데이터 차이였다(볼륨 디렉터리 블록만 다름: 날짜 바이트와 HELLO 형식 바이트 `.do` `$04` · `.po` `$06`). `info -v` · MSX · X68000 baseline 은 그대로.
- 2026-10-08 — 예제 `Tutorial_apple_01.do` 를 `.po` 에서 `rdedisktool convert -f do` 로 다시 만들어 두 이미지를 순서만 다른 같은 디스크로 맞췄다(python 판독: 새 `.do` 를 ProDOS 순서로 되돌리면 `.po` 와 바이트 동일). `$0803` 에 적재되는 이진 HELLO 가 TXT 로 되어 있던 것이 BIN 이 됨 → `list_Tutorial_apple_01.do` 갱신. `info_v_Tutorial_apple_01.do` 는 출력이 같아 그대로.
- 2026-10-08 — `info_v_work.xdf` 갱신: X68000 기하를 실린더 × 헤드로 바로잡아 `Tracks: 154` → `77`, `Total Size: 2523136` → `1261568`(파일 실제 크기와 같음). 다른 줄은 그대로.
