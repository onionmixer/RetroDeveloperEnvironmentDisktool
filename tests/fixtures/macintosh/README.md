# Macintosh Test Fixtures

본 디렉토리는 Phase 1 Macintosh 지원 회귀에 사용되는 테스트 픽스처를 보관한다.
모든 파일은 외부 출처 (`MacDiskcopy/sample/`, `MacDiskcopy/external_fixtures/dc42/`)
에서 복사되었으며 byte-for-byte 동일성을 SHA256SUMS 로 보장한다.

## 픽스처 목록

| 파일 | 크기 | 포맷 | 부팅 | 용도 |
|---|---|---|---|---|
| `608_SystemTools.img` | 819,200 | raw HFS 800K | yes | bootable HFS 회귀 |
| `LIDO.dsk` | 1,474,560 | raw HFS 1.44M | yes | 1.44M HFS 회귀 |
| `stuffit_expander_5.5.img` | 1,474,560 | raw HFS 1.44M | no | non-bootable HFS 회귀 |
| `lido.image` | 1,474,644 | DC42 → 1.44M HFS | yes | DC42 컨테이너 회귀 |
| `systemtools.image` | 819,284 | DC42 → 800K HFS | yes | DC42 컨테이너 회귀 |
| `stuffit_expander_5_5.image` | 1,474,644 | DC42 → 1.44M HFS | no | DC42 + non-bootable |

DC42 파일 크기 = `0x54 + data_size` (tag_size=0).

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
