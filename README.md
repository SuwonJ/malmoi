# Malmoi — 말모이

설명으로 우리말 찾기(역방향 사전), 유사어 검색, 말랑이 게임을 위한 C11 프로젝트입니다. 기본 CMake 빌드는 전체 어종 문자 TF-IDF + Kiwi BM25를 C와 Kiwi C API로 실행합니다. 인덱스와 네이티브 라이브러리는 최초 구성 때 자동으로 준비하며 실행 시 Python·NumPy·SciPy·Kiwi Python 패키지가 필요하지 않습니다. 선택 E5 빌드는 설명 의미 검색을 위해 로컬 모델과 Python/ONNX Runtime을 사용합니다.

## 팀원 시작 방법

저장소를 clone하고 VS Code에서 폴더를 연 뒤 통합 터미널에서 아래 CMake 명령으로 빌드하고 실행합니다. CMake Tools 확장은 선택 사항입니다. 기존 C 개발 도구(C 컴파일러와 CMake)만 사용합니다. Python 패키지 설치나 모델 경로 설정은 필요 없습니다.

전체 사전은 `data/full/`의 분할 파일로 Git에서 관리합니다. CMake가 SHA-256을 검증하며 `data/full.mlm`으로 자동 복원하고 실행 파일 옆에 배치합니다. Git LFS나 수동 파일 병합도 필요 없습니다. 이 MLM은 유사어·게임용 고유어 204,632개 정규화 키 데이터입니다. 기본 역방향 검색은 이 범위와 별개인 전체 어종 1,215,000개 뜻풀이 인덱스를 사용합니다. 유사어와 게임은 기존 MLM의 저장된 임베딩을 사용합니다.

모델 담당자는 별도 `e5` 빌드에서 실험합니다. 기본 빌드와 `malmoi.h` 호출 방식은 동일합니다.

## 시작하기 (VS Code / 터미널)

프로젝트 루트에서 다음 명령을 실행합니다. VS Code 통합 터미널과 일반 터미널에서 동일합니다.

```bash
cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build/cmake
./build/cmake/malmoi

# 김시현의 오프라인 2인 단어 유사도 대결 (독립 실행 대상)
cmake --build build/cmake --target wordgame_sii
./build/cmake/wordgame_sii
```

Windows에서는 `cmake --build build/cmake --config Release` 후 `build/cmake/malmoi.exe`를 실행합니다. 실행 파일 옆의 사전과 `runtime/`를 자동으로 찾으므로 작업 디렉터리나 환경 변수를 지정하지 않아도 됩니다.

메뉴에서 `1` 역방향 사전, `2` 유사어, `3` 말랑이, `4` 워드브릿지를 선택할 수 있습니다. 2인 단어 대결은 별도 실행 대상 `wordgame_sii`입니다. 게임의 데모 정답은 `이슬비`이며, `MALLANGI_ANSWER` 환경 변수로 바꿀 수 있습니다. 이는 임시 실행 메뉴이고 최종 GUI는 아직 결정되지 않았습니다.

저장소의 `data/dictionary.mlm`은 아주 작은 **합성 예제 데이터**입니다. 실제 사전의 품질을 나타내지 않습니다. 실제 데이터는 `MALMOI_DATA=/path/to/dictionary.mlm`로 선택할 수 있습니다.

## 자동으로 준비되는 검색 데이터

최초 `cmake -S . -B build/cmake` 실행 시 공식 Kiwi 0.24.0 네이티브 라이브러리와 base 모델, [전체 검색 인덱스](https://github.com/SuwonJ/malmoi/releases/tag/search-index-v1)를 내려받고 SHA-256을 검증합니다. 약 900 MiB의 다운로드와 압축 해제를 위한 디스크 공간이 필요합니다. 이후 같은 빌드는 다운로드 캐시를 재사용하며 실행에는 네트워크가 필요하지 않습니다. 다운로드가 중단되면 네트워크를 확인한 뒤 CMake 명령을 다시 실행합니다.

CMake는 Windows x64, Linux x64/ARM64, macOS x64/ARM64의 공식 Kiwi 배포본을 선택합니다. 운영체제별 실제 검증 결과는 `Native search runtime` CI에서 확인합니다. 별도 ICU·Python 패키지는 설치하지 않습니다. `build/cmake` 폴더를 옮겨 실행할 때는 실행 파일·Kiwi DLL(Windows)·`runtime/`·`data/`를 함께 전달합니다. 지원하지 않는 플랫폼은 명시적으로 구성 오류를 반환합니다.

기존 C 검색기로 빌드하려면 `-DMALMOI_WITH_NATIVE_INDEX=OFF`를 지정합니다. 네이티브 인덱스 빌드에서 기존 검색기만 선택하려면 `MALMOI_SEARCH_BACKEND=native`를 사용합니다. 기본 설정은 전체 어종 인덱스입니다. 인덱스나 모델이 손상되면 오류를 반환하며 조용히 기존 검색기로 전환하지 않습니다.

전체 뜻풀이 ID·원본 행 번호는 `malmoi_search_senses`에서 확인합니다. 기존 C 데이터와 대응되지 않는 결과의 `word_id`는 `UINT32_MAX`입니다. 게임·유사어 기능은 기존 C 데이터 범위를 유지합니다.

인덱스 제작자만 오프라인 Python 도구 `tools/export_native_index.py`를 사용합니다. 팀원의 빌드·실행 과정에는 이 도구를 실행하지 않습니다. 대용량 인덱스는 Git 이력 대신 버전이 고정된 Release 첨부 파일로 배포합니다.

## 선택: E5 설명 의미 검색

E5는 별도 선택 빌드이며 기본 검색은 Kiwi C API 기반입니다. 모델 담당자의 수정 지점·교체 계약·검증 순서는 [모델 교체 가이드](docs/MODEL_GUIDE.md)를 참고하세요. 공용 `CMakePresets.json`의 `native`와 `e5`로 빌드를 분리할 수 있습니다.

MLM에는 미리 계산한 사전 벡터가 들어 있습니다. 새 설명 문장을 벡터로 만드는 모델은 들어 있지 않습니다. 유사어·게임은 MLM을 사용하고, 기본 역방향 검색은 자동 준비된 네이티브 인덱스를 사용합니다. E5는 일치하는 모델을 별도로 준비해야 합니다.

```bash
# Python 3.9+ 환경(개발 헤더/공유 라이브러리 포함)에 선택 의존성 설치
/path/to/python -m pip install -r tools/requirements-onnx.txt
cmake -S . -B build/e5 -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DMALMOI_WITH_NATIVE_INDEX=OFF -DMALMOI_WITH_E5=ON -DPython3_EXECUTABLE=/path/to/python
cmake --build build/e5
MALMOI_DATA=/path/to/dictionary.mlm MALMOI_MODEL=/path/to/model ./build/e5/malmoi
```

모델 폴더에는 `download.json`, `tokenizer.json`, `onnx/model_quantized.onnx`가 필요합니다. 모델 revision·파일 해시·벡터 규격이 MLM과 일치해야 합니다. 실행 중 다운로드나 외부 프로세스 실행은 하지 않습니다. 모델은 엔진을 열 때 한 번 로드하고 질의마다 재사용합니다. 시작 화면에 사전 경로와 설명 검색 모드가 표시됩니다.

기존 `malmoi.h` 호출은 그대로 사용합니다. `MALMOI_MODEL`을 지정하지 않으면 선택한 기본 검색기이며, 지정했는데 모델·의존성·빌드 설정이 잘못되면 오류로 종료합니다. 기본 빌드로 돌아가려면 별도 디렉터리에서 `-DMALMOI_WITH_E5=OFF`로 빌드하고 `MALMOI_MODEL`을 해제하세요.

VS Code에서도 통합 터미널에서 위 CMake 명령과 환경 변수를 사용합니다. 디버깅 설정을 추가한다면 작업 디렉터리와 환경 변수를 동일하게 지정합니다. 개인별 절대 경로는 공용 설정에 넣지 않습니다.

E5 모드는 현재 Linux에서 검증했습니다. 선택 빌드는 Python 공유 라이브러리와 해당 Python 환경도 배포해야 하므로, 실행 파일·MLM만 전달하려면 기본 빌드를 사용하세요. 별도 작업 디렉터리에서 실행하려면 `MALMOI_ENCODER_SCRIPT=/absolute/path/to/tools/embed_query.py`도 지정합니다.

실제 모델 통합 테스트(외부 데이터·모델은 내려받지 않음):

```bash
cmake --build build/e5 --target malmoi_cli
/path/to/python tests/test_e5_runtime.py --cli build/e5/malmoi_cli \
  --database /path/to/dictionary.mlm --model /path/to/model
```

## 구조와 문서

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md): 전체 구조, 파일 소유 경계, **주요 변수와 정보의 흐름**, 결정 사항/미결정 사항
- [`docs/API.md`](docs/API.md): `malmoi.h` 공개 함수의 인자·반환값·오류 규약
- `include/`: 공개 C 헤더 / `src/`: 공통 C 엔진과 기본 진입점
- `games/mallangi/`: 말랑이 게임 담당자 코드(위치 유지)
- `gameSH/`: 오프라인 2인 단어 대결 (독립 CMake 대상 `wordgame_sii`)
- `tools/`: 오프라인 데이터 생성 도구(Python)
- `tests/`, `examples/`: 기존 회귀 검증 및 호환 코드(CMake 기본 앱에는 포함하지 않음)

기존 GitHub Actions는 아직 Makefile을 사용합니다. `make test`로 회귀 검증할 수 있으며, CMake 빌드로 CI를 전환하기 전까지 Makefile과 테스트 코드를 삭제하지 않습니다.
