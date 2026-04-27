# Baseline Outputs (Pre-Macintosh-Support)

이 디렉토리는 Macintosh 지원 추가 작업 **이전** 의 `rdedisktool info -v` / `list` 출력을
영구 보존한다. 머지 전후 byte-for-byte 비교로 기존 Apple/MSX/X68000 기능 무회귀를 보장한다.

## 캡처 시점

- Git tag: `pre-mac` (commit `a770ef0fcbb9a5019fd8d5b57248efb72806ed9b`)
- 커밋 메시지: `chore: remove temporary analysis documents` (2026-04-09)
- 빌드: `build/rdedisktool` (806104 bytes), `build_local/rdedisktool` (797040 bytes)

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

`tests/test_baseline_diff.sh` (선행 PR 1 에서 신설) 가 자동으로 비교한다. 수동 비교 시:

```bash
PROJ_ROOT="$(cd ../.. && pwd)"
./build/rdedisktool info -v "$PROJ_ROOT/Examples/Tutorial_apple_01/Tutorial_apple_01.do" \
  | sed "s|${PROJ_ROOT}/||g" \
  | diff -u tests/baselines/info_v_Tutorial_apple_01.do.txt -
```

## 갱신 정책

- baseline 은 **기존 기능의 동결된 동작 명세**다. Macintosh PR 머지 시점까지 변경 금지.
- Macintosh 머지 후, Mac 추가로 인해 기존 출력이 정당하게 바뀌어야 하는 경우(예: `--list-formats` 출력에 Mac 포맷 추가) 별도 갱신 commit 으로 처리하고 PR 설명에 사유 명시.
- 갱신 시에는 항상 **build_local/rdedisktool** 또는 **build/rdedisktool** 의 최신 빌드로 재캡처.
