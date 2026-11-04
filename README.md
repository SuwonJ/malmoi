# Malmoi — 말모이

설명으로 우리말 찾기(역방향 사전), 유사어 검색, 말랑이 게임을 위한 C11 프로젝트입니다. 기본 빌드는 MLM만으로 실행하며 Python·ONNX·네트워크가 필요하지 않습니다. 선택 E5 빌드는 설명 의미 검색을 위해 로컬 모델과 Python/ONNX Runtime을 사용합니다.

## 팀원 시작 방법

같은 레포를 clone하고 CLion에서 루트를 열어 기본 CMake 빌드 후 `malmoi`를 실행합니다. Python·ONNX·모델·환경 변수 설정은 필요 없습니다.

전체 사전은 `data/full/`의 분할 파일로 Git에서 관리합니다. CMake가 SHA-256을 검증하며 `data/full.mlm`으로 자동 복원하고 실행 파일 옆에 배치합니다. Git LFS나 수동 파일 병합도 필요 없습니다. 현재 데이터는 고유어 204,632개 정규화 키 범위입니다. 기본 설명 검색은 문자 기반이며, 유사어와 게임은 저장된 임베딩을 사용합니다.

모델 담당자는 별도 `e5` 빌드에서 실험합니다. 기본 빌드와 `malmoi.h` 호출 방식은 동일합니다.

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

## 선택: E5 설명 의미 검색

현재 기본 개발 모드는 모델 없는 경량 실행입니다. 모델 담당자의 수정 지점·교체 계약·검증 순서는 [모델 교체 가이드](docs/MODEL_GUIDE.md)를 참고하세요. 공용 `CMakePresets.json`의 `native`와 `e5`로 빌드를 분리할 수 있습니다.

MLM에는 미리 계산한 사전 벡터가 들어 있습니다. 새 설명 문장을 벡터로 만드는 모델은 들어 있지 않습니다. 따라서 **유사어·게임·문자 검색은 MLM만으로**, **E5 설명 의미 검색은 일치하는 모델까지 있어야** 실행할 수 있습니다.

```bash
# Python 3.9+ 환경(개발 헤더/공유 라이브러리 포함)에 선택 의존성 설치
/path/to/python -m pip install -r tools/requirements-onnx.txt
cmake -S . -B build/e5 -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DMALMOI_WITH_E5=ON -DPython3_EXECUTABLE=/path/to/python
cmake --build build/e5
MALMOI_DATA=/path/to/dictionary.mlm MALMOI_MODEL=/path/to/model ./build/e5/malmoi
```

모델 폴더에는 `download.json`, `tokenizer.json`, `onnx/model_quantized.onnx`가 필요합니다. 모델 revision·파일 해시·벡터 규격이 MLM과 일치해야 합니다. 실행 중 다운로드나 외부 프로세스 실행은 하지 않습니다. 모델은 엔진을 열 때 한 번 로드하고 질의마다 재사용합니다. 시작 화면에 사전 경로와 설명 검색 모드가 표시됩니다.

기존 `malmoi.h` 호출은 그대로 사용합니다. `MALMOI_MODEL`을 지정하지 않으면 문자 검색이며, 지정했는데 모델·의존성·빌드 설정이 잘못되면 오류로 종료합니다. 기본 빌드로 돌아가려면 별도 디렉터리에서 `-DMALMOI_WITH_E5=OFF`로 빌드하고 `MALMOI_MODEL`을 해제하세요.

CLion에서는 CMake 프로필에 위 옵션들을 넣고 Run Configuration의 Working directory를 프로젝트 루트, Environment variables를 `MALMOI_DATA`와 `MALMOI_MODEL`로 지정합니다. IDE 설정은 개인별 `.idea/`에만 저장합니다.

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
- `games/mallangi/`: 게임 담당자 코드(위치 유지)
- `tools/`: 오프라인 데이터 생성 도구(Python)
- `tests/`, `examples/`: 기존 회귀 검증 및 호환 코드(CMake 기본 앱에는 포함하지 않음)

기존 GitHub Actions는 아직 Makefile을 사용합니다. `make test`로 회귀 검증할 수 있으며, CMake 빌드로 CI를 전환하기 전까지 Makefile과 테스트 코드를 삭제하지 않습니다.
