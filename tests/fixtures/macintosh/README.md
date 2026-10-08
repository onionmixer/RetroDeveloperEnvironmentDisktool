# Macintosh Test Fixtures

본 디렉토리는 Phase 1 Macintosh 지원 회귀에 사용되는 테스트 픽스처를 보관한다.
HFS/DC42 6 종은 외부 출처 (`MacDiskcopy/sample/`, `MacDiskcopy/external_fixtures/dc42/`)
에서 복사되었고, MFS 4 종은 Python `mfs-init-empty` 로 만들었다(아래). 모든 파일의
byte-for-byte 동일성은 SHA256SUMS 로 보장한다.

## 픽스처 목록

| 파일 | 크기 | 포맷 | 부팅 | 용도 |
|---|---|---|---|---|
| `608_SystemTools.img` | 819,200 | raw HFS 800K | yes | bootable HFS 회귀 |
| `LIDO.dsk` | 1,474,560 | raw HFS 1.44M | yes | 1.44M HFS 회귀 |
| `stuffit_expander_5.5.img` | 1,474,560 | raw HFS 1.44M | no | non-bootable HFS 회귀 |
| `lido.image` | 1,474,644 | DC42 → 1.44M HFS | yes | DC42 컨테이너 회귀 |
| `systemtools.image` | 819,284 | DC42 → 800K HFS | yes | DC42 컨테이너 회귀 |
| `stuffit_expander_5_5.image` | 1,474,644 | DC42 → 1.44M HFS | no | DC42 + non-bootable |
| `empty_mfs.img` | 409,600 | raw MFS 400K (볼륨 `Empty MFS`) | no | `test_mac_mfs_write.sh` |
| `sample_mfs.img` | 409,600 | raw MFS 400K (볼륨 `Empty MFS`, `Hello.txt` 16 B) | no | 시험 미사용 |
| `small_boot_mfs.img` | 409,600 | raw MFS 400K (볼륨 `Small Boot`, LK 부트 블록) | no | 시험 미사용 |
| `big_boot_mfs.img` | 819,200 | raw MFS 800K (볼륨 `Big MFS`, 할당 블록 2048 B, LK 부트 블록) | no | 시험 미사용 |

DC42 파일 크기 = `0x54 + data_size` (tag_size=0).

MFS 4 종은 위 MacDiskcopy 복사본이 아니라 Python `mfs-init-empty` 로 만든 것이다(커밋
`1b86341`: empty/sample, `83ec81c`: small_boot/big_boot). "부팅" 칸은 `rdedisktool info -v` 의
`BootDisk` 판정(2026-10-08) — `*_boot_mfs` 는 LK 부트 블록이 있지만 System/Finder 가 없어
의도적으로 `no` 다. "시험 미사용" 은 `tests/*.sh` 전수 grep 결과.

## 무결성 검증

```bash
cd tests/fixtures/macintosh && sha256sum -c SHA256SUMS
```

## 라이선스 노트

이들은 Apple 시스템 소프트웨어 (System 6 / 7 시기) 와 SCSI driver (LIDO) 를 포함한
배포가능 floppy 이미지다. PLAN_MACFDD.md §13 의 정책을 따라:

- 형식 사실관계 (header layout, MDB offset 등) 만 SPEC 에서 사용
- `undiskcopy` 같은 외부 코드 직접 복사 금지
- 픽스처 파일 자체는 **디스크 형식 검증 목적**으로 보존되며, 그 안의 콘텐츠는 추출/실행
  대상이 아님

원래 `MacDiskcopy/sample/` 의 README/EXTERNAL_FIXTURE_BASELINE.md 가 라이선스 검토 결과
배포 가능을 명시한 경로에 있으나, 추후 IP 검토에서 문제 시 본 디렉토리에서 즉시 제거
가능하다 (.gitignore 규칙 / git rm).
