# Malmoi — 말모이

설명으로 우리말 찾기(역방향 사전), 유사어 검색, 말랑이 게임을 위한 C11 프로젝트입니다. 검색 실행에는 Python·ONNX·네트워크가 필요하지 않습니다.

## 시작하기 (CLion / CMake)

프로젝트 루트를 CLion에서 열거나 다음 명령을 실행합니다.

```bash
cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build/cmake
./build/cmake/malmoi
```

Windows에서는 `build/cmake/Debug/malmoi.exe` 또는 사용한 CMake 생성기의 출력 경로에서 실행합니다. **작업 디렉터리를 저장소 루트로 지정**하세요. 기본 사전 경로가 `data/dictionary.mlm`이기 때문입니다.

메뉴에서 `1` 역방향 사전, `2` 유사어, `3` 말랑이를 선택할 수 있습니다. 게임의 데모 정답은 `이슬비`이며, `MALLANGI_ANSWER` 환경 변수로 바꿀 수 있습니다. 이는 임시 실행 메뉴이고 최종 GUI는 아직 결정되지 않았습니다.

저장소의 `data/dictionary.mlm`은 아주 작은 **합성 예제 데이터**입니다. 실제 사전의 품질을 나타내지 않습니다. 실제 데이터는 `MALMOI_DATA=/path/to/dictionary.mlm`로 선택할 수 있습니다.

## 구조와 문서

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md): 전체 구조, 파일 소유 경계, **주요 변수와 정보의 흐름**, 결정 사항/미결정 사항
- [`docs/API.md`](docs/API.md): `malmoi.h` 공개 함수의 인자·반환값·오류 규약
- `include/`: 공개 C 헤더 / `src/`: 공통 C 엔진과 기본 진입점
- `games/mallangi/`: 게임 담당자 코드(위치 유지)
- `tools/`: 오프라인 데이터 생성 도구(Python)
- `tests/`, `examples/`: 기존 회귀 검증 및 호환 코드(CMake 기본 앱에는 포함하지 않음)

기존 GitHub Actions는 아직 Makefile을 사용합니다. `make test`로 회귀 검증할 수 있으며, CMake 빌드로 CI를 전환하기 전까지 Makefile과 테스트 코드를 삭제하지 않습니다.
