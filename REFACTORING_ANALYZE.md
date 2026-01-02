# Refactoring Opportunities

아래 항목들은 전체 코드베이스를 살펴보며 발견한 반복/중복 패턴이다. 공통 유틸리티로 추출하거나 공통 베이스 클래스를 도입하면 유지보수성이 좋아질 수 있다.

## 1. Apple 파일 시스템 핸들러 공통화
- `AppleDOS33Handler`(`src/filesystem/apple/AppleDOS33Handler.cpp:21-185`)와 `AppleProDOSHandler`(`src/filesystem/apple/AppleProDOSHandler.cpp:1-180` 등)는 모두 `DiskImage* m_disk`를 이용해 섹터/블록을 읽고, 이름을 `std::memcpy`로 파싱하고, 디렉터리 엔트리를 순회하는 반복 코드가 있다.
- 특히 `readSector`/`writeSector`(DOS)와 `readBlock`/`writeBlock`(ProDOS), `parse`/`write` VTOC/VolumeHeader, 디렉터리 엔트리 반복(`for (size_t i...)`) 구조가 거의 동일한데, 오프셋과 상수만 다르다.
- 제안: `AppleFileSystemBase` 같은 베이스 클래스를 두고 `readChunk(track,sector)`/`writeChunk`/`parseEntries` 같은 공통 헬퍼를 제공하면 중복된 `std::vector<uint8_t>` 조작 코드와 `std::memcpy` 블록을 줄일 수 있다. 또한 Apple 계열 디스크는 모두 0-based 섹터 접근을 사용하므로 공통 래퍼를 두기 쉽다.

## 2. CLI 명령 처리 로직의 반복
- `cmdList`, `cmdExtract`, `cmdAdd`, `cmdDelete`, `cmdMkdir`, `cmdRmdir`, `cmdCreate`, `cmdConvert`, `cmdValidate`, `cmdDump` 등 (`src/cli/CLI.cpp:522-900` 일대)은 모두 아래 순서를 반복한다:
  1. 인자 부족 여부 체크 및 `printCommandHelp` 호출
  2. `DiskImageFactory::detectFormat`, `DiskImageFactory::open`, `FileSystemHandler::create`
  3. 핸들러가 없는 경우 동일한 에러 메시지
  4. try/catch 블록에서 `DiskException` 처리
- 이 로직이 함수마다 복붙되어 있어, 새로운 명령을 추가하거나 에러 메시지를 수정할 때 모든 곳을 손봐야 한다.
- 제안: `withHandler(imagePath, lambda)` 헬퍼를 CLI 내부에 만들어 공통 감싸기(형식 감지, 디스크 열기, 핸들러 생성, 예외 처리) 후 콜백에서 실제 명령만 수행하도록 구조화하면 중복을 제거할 수 있다. 동시에 조용한 모드/에러 출력 형식도 한 곳에서 제어 가능.

## 3. CLI 옵션 파싱 중복
- `cmdAdd`(`src/cli/CLI.cpp:663-720`)는 `-f/--force` 플래그를 직접 스캔해 나머지를 `positionalArgs`로 수집한다. `cmdMkdir`/`cmdRmdir`도 포맷(`-f <format>`)을 유사한 방식으로 파싱하고, `cmdCreate`/`cmdConvert` 또한 비슷한 수동 파싱을 수행한다.
- 각각이 자체적으로 옵션을 해석하여 코드가 늘어나고, 새로운 공통 옵션을 추가하기 어렵다.
- 제안: 간단한 옵션 파서(예: `ParsedArgs parseOptions(args, {"-f", "--force"})`)를 만들고 CLI 전체에서 공유하면 중복 감소와 옵션 처리 일관성을 확보할 수 있다.

## 4. DiskImageFactory의 포맷 판별 로직
- `DiskImageFactory::detectFormat`(`src/core/DiskImageFactory.cpp:18-200`)은 Apple/MSX 구분 로직을 extension·size·매직 시그니처로 반복해서 구현하고 있다. Apple과 MSX 각각에 대해 별도의 함수(`detectAppleFormat`, `detectMSXFormat`, `detectDSKByContent`)가 있지만, 확장자 비교/소문자 변환/파일 크기 조건이 함수마다 거의 동일하게 반복된다.
- 또한 `.dsk` 확장자를 감지하는 로직이 `detectFormat` 내에 하드코딩되어 있어 새로운 포맷 조건을 추가할 때 중복된 확장자 비교를 수정해야 한다.
- 제안: (1) 확장자→포맷 매핑 테이블을 두고, (2) `.dsk` 처리를 별도 헬퍼(`detectDSKFormatBySizeOrContent`)로 추출하여 Apple/MSX 공통 로직을 최대한 공유하면 유지보수가 수월해진다.

## 5. XSA 처리 코드의 반복
- `MSXXSAImage`, `XSAExtractor`, `XSACompressor` 등 XSA 관련 파일들(`src/msx/MSXXSAImage.cpp`, `src/msx/XSAExtractor.cpp`, `src/msx/XSACompressor.cpp`)에서 헤더 파싱/쓰기(매직, 원본 길이, 압축 길이) 코드가 여러 곳에서 반복된다. 예를 들어 헤더 구조체를 정의하지 않고 매번 바이트 오프셋으로 접근하고 있어, 잘못된 수정이 여러 파일로 퍼질 수 있다.
- 제안: XSA 헤더 정보를 표현하는 `struct XSAHeader { ...; bool read(std::istream&); void write(std::ostream&); }`를 도입해 파싱/검증 코드를 한 곳으로 모으고, 압축기/추출기/디스크 이미지가 공통 API를 사용하도록 개선하면 중복을 줄일 수 있다.

## 6. 테스트/예제 로직 중복
- `README`, `TESTCASE.md`, `tests/` 폴더의 스크립트들은 모두 `rdedisktool detect-format -> open -> create FS handler` 순서를 예시로 보여주지만, 실제 코드 (`CLI::cmdList` 등)에서는 동일 흐름을 여러 번 복사해 구현한다. CLI 내 공통 헬퍼를 만든 뒤 README/테스트/코드가 같은 경로를 공유하면, 문서와 실제 동작의 일탈 가능성이 줄어든다.

---

위 항목 중 1~4는 코드베이스 직접 수정 시 가장 중복 제거 효과가 클 것으로 보인다. 특히 CLI 공통화와 Apple 파일 시스템 베이스 클래스를 도입하면 이후 기능 추가나 버그 수정 시 작업량을 크게 줄일 수 있다.
