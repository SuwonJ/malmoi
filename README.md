# Malmoi — 말모이

설명으로 우리말 찾기(역방향 사전), 유사어 검색, 말랑이 게임을 위한 C11 프로젝트입니다. 기본 CMake 빌드는 전체 어종 문자 TF-IDF + Kiwi BM25를 C와 Kiwi C API로 실행합니다. 인덱스와 네이티브 라이브러리는 최초 구성 때 자동으로 준비하며 실행 시 Python·NumPy·SciPy·Kiwi Python 패키지가 필요하지 않습니다. 선택 E5 빌드는 설명 의미 검색을 위해 로컬 모델과 Python/ONNX Runtime을 사용합니다.

## 팀원 시작 방법

저장소를 clone하고 VS Code에서 폴더를 연 뒤 통합 터미널에서 아래 CMake 명령으로 빌드하고 실행합니다. CMake Tools 확장은 선택 사항입니다. 기존 C 개발 도구(C 컴파일러와 CMake)만 사용합니다. Python 패키지 설치나 모델 경로 설정은 필요 없습니다.

기본 네이티브 모드는 `runtime/index`의 전체 어종 1,215,000개 뜻풀이 인덱스를 모든 공개 API에 사용합니다. 역검색은 기존 TF-IDF + Kiwi BM25 순위 결합을 유지하고, 유사어·단어 쌍·게임은 뜻풀이 문자 TF-IDF와 Kiwi BM25를 대칭 결합한 0~1 점수를 사용합니다. 기존 E5 의미 유사도와 동등하지 않습니다. MLM 파일은 기본 모드에서 열거나 복원하지 않습니다. `MALMOI_SEARCH_BACKEND=native`로 명시적으로 선택하는 레거시 모드만 기존 MLM을 사용합니다.

모델 담당자는 별도 `e5` 빌드에서 실험합니다. 기본 빌드와 `malmoi.h` 호출 방식은 동일합니다.

## 시작하기 (VS Code / 터미널)

프로젝트 루트에서 다음 명령을 실행합니다. VS Code 통합 터미널과 일반 터미널에서 동일합니다.

```bash
cmake --preset team
cmake --build --preset team
./build/team/malmoi
# CLI 상태 확인 (CLI 대상은 명시적으로 빌드)
cmake --build build/team --target malmoi_cli
./build/team/malmoi_cli --status
```

`team` 프리셋은 Ninja를 사용합니다. Ninja 없이 시스템 기본 생성기를 쓰려면 새 빌드 경로에서 다음처럼 같은 옵션을 명시합니다.

```bash
cmake -S . -B build/team-default -DMALMOI_WITH_NATIVE_INDEX=ON -DMALMOI_WITH_E5=OFF
cmake --build build/team-default --config Release
```

Windows에서는 `cmake --build build/team --config Release` 후 `build/team/malmoi.exe`를 실행합니다. 실행 파일 옆의 사전과 `runtime/`를 자동으로 찾으므로 작업 디렉터리나 환경 변수를 지정하지 않아도 됩니다.

메뉴에서 `1` 역방향 사전, `2` 유사어, `3` 말랑이, `4` 워드브릿지를 선택할 수 있습니다. 2인 단어 대결은 별도 실행 대상 `wordgame_sii`입니다. 게임의 데모 정답은 `이슬비`이며, `MALLANGI_ANSWER` 환경 변수로 바꿀 수 있습니다. 이는 임시 실행 메뉴이고 최종 GUI는 아직 결정되지 않았습니다.

저장소의 `data/dictionary.mlm`은 아주 작은 **합성 예제 데이터**입니다. 실제 사전의 품질을 나타내지 않습니다. 실제 데이터는 `MALMOI_DATA=/path/to/dictionary.mlm`로 선택할 수 있습니다.

## 자동으로 준비되는 검색 데이터

최초 `cmake --preset team` 실행 시 공식 Kiwi 0.24.0 네이티브 라이브러리와 base 모델, [전체 검색 인덱스](https://github.com/SuwonJ/malmoi/releases/tag/search-index-v1)를 내려받고 SHA-256을 검증합니다. 약 900 MiB의 다운로드와 압축 해제를 위한 디스크 공간이 필요합니다. 이후 같은 빌드는 다운로드 캐시를 재사용하며 실행에는 네트워크가 필요하지 않습니다. 다운로드가 중단되면 네트워크를 확인한 뒤 CMake 명령을 다시 실행합니다.

CMake는 Windows x64, Linux x64/ARM64, macOS x64/ARM64의 공식 Kiwi 배포본을 선택합니다. 운영체제별 실제 검증 결과는 `Native search runtime` CI에서 확인합니다. 별도 ICU·Python 패키지는 설치하지 않습니다. `build/team` 폴더를 옮겨 실행할 때는 실행 파일·Kiwi DLL(Windows)·`runtime/`를 함께 전달합니다. 기본 모드에는 `data/`나 MLM이 필요하지 않습니다. 지원하지 않는 플랫폼은 명시적으로 구성 오류를 반환합니다.

기존 MLM이 있는 레거시 C 검색기로 빌드하려면 `-DMALMOI_WITH_NATIVE_INDEX=OFF`를 지정합니다. 네이티브 인덱스 빌드에서 기존 검색기만 선택하려면 `MALMOI_SEARCH_BACKEND=native`를 사용합니다. 기본 설정은 전체 어종 인덱스입니다. 인덱스나 모델이 손상되면 오류를 반환하며 조용히 기존 검색기로 전환하지 않습니다.

전체 뜻풀이 ID·원본 행 번호는 `malmoi_search_senses`에서 확인합니다. `word_id`는 이와 별개인 전체 표제어 목록의 ID로, `malmoi_word_at(engine, word_id, ...)`와 일치합니다. 정규화 표제어는 UTF-8 바이트 순서로 정렬하고 동음이의어·다의어를 하나의 ID로 묶습니다. 현재 고유 표제어는 986,759개입니다. 이 ID는 코퍼스 버전에 종속되므로 영구 외부 ID로 저장하지 않습니다.

유사어는 자신과 같은 정규화 표제어를 제외하며 뜻풀이 쌍 중 최대 결합 점수로 정렬합니다. 각 뜻풀이 쌍은 TF-IDF 코사인과 대칭 BM25를 1:1로 평균합니다. BM25는 A→B와 B→A를 각각 계산하고 질의 뜻풀이의 자기 BM25로 나눈 뒤 각 값을 1로 제한해 평균합니다. 어느 한 구성요소를 양쪽에서 계산할 수 없으면 계산 가능한 구성요소만 사용하며, 공통 구성요소가 없으면 0입니다. 이 가중치는 검증·조정된 품질 모델이 아닌 초기 기준값입니다. 단어 쌍 점수는 0~1이고 대칭입니다. 같은 표제어는 비영 벡터가 있으면 1, 모든 뜻풀이가 영 벡터이면 ZERO_VECTOR 오류입니다. 정의가 같으면 서로 다른 표제어도 1이 될 수 있습니다. 이것은 E5 의미 판단이 아닙니다.

문서별 접근은 기존 TF-IDF 및 BM25 포스팅 위치만 가리키는 32비트 참조 구조를 초기화 시 생성합니다(각 약 489 MiB와 84 MiB, 가중치 복사 없음). 문서 norm과 표제어 목록도 생성합니다. 256슬롯 단어 쌍 캐시 및 최근 4개 유사어 점수 배열을 재사용합니다. 이 구조는 메모리에만 존재하며 배포 인덱스·원본 캐시를 변경하지 않습니다.

인덱스 제작자만 오프라인 Python 도구 `tools/export_native_index.py`를 사용합니다. 팀원의 빌드·실행 과정에는 이 도구를 실행하지 않습니다. 대용량 인덱스는 Git 이력 대신 버전이 고정된 Release 첨부 파일로 배포합니다.

## 선택: E5 설명 의미 검색

E5는 별도 선택 빌드이며 기본 검색은 Kiwi C API 기반입니다. 모델 담당자의 수정 지점·교체 계약·검증 순서는 [모델 교체 가이드](docs/MODEL_GUIDE.md)를 참고하세요. 공용 `CMakePresets.json`의 `native`와 `e5`로 빌드를 분리할 수 있습니다.

MLM에는 미리 계산한 사전 벡터가 들어 있습니다. 새 설명 문장을 벡터로 만드는 모델은 들어 있지 않습니다. 레거시 E5 빌드는 유사어·게임에 MLM을 사용하고, 기본 네이티브 빌드는 모든 검색·게임에 자동 준비된 인덱스를 사용합니다. E5는 일치하는 모델을 별도로 준비해야 합니다.

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

### 역검색과 유사어의 실행 경로 확인

`team` 프리셋은 기존 캐시 값에 관계없이 `MALMOI_WITH_NATIVE_INDEX=ON`, E5는 OFF로 지정합니다. 기본 실행 파일은 `build/team/malmoi`(Windows: `malmoi.exe`)입니다. 기존 `build/*` 실행 파일은 자동 교체되지 않습니다. CMake에서 OFF 경고가 나오면 기존 캐시가 남은 빌드입니다.

메뉴 1 및 CLI `query`는 `malmoi_search`로, 메뉴 2 및 CLI `word`는 `malmoi_similar`로 연결됩니다. 기본 모드에서 `--status`는 `reverse_backend=native-index`, `similar_backend=definition-tfidf-bm25`입니다. `malmoi_similarity`·열거·말랑이·워드브릿지·2인 게임도 같은 전체 사전 인덱스를 사용합니다. 레거시 모드만 `stored-vectors`를 표시합니다.

| 빌드·환경 | 역검색 |
|---|---|
| team, 환경 변수 없음 | native-index |
| team, MALMOI_SEARCH_BACKEND=native-index | 동일한 native-index |
| MALMOI_SEARCH_BACKEND=native | 명시적 구형 문자 검색 |
| 네이티브 OFF 빌드, 환경 변수 없음 | 구형 문자 검색, 구성·메뉴 경고 |
| 네이티브 OFF 빌드, native-index 선택 | 미지원 오류 |
| team, MALMOI_MODEL 지정 | E5 미포함으로 설정 오류 |

인덱스·모델 누락/손상/버전 불일치 시 시작 오류와 경로를 stderr에 표시하고 종료합니다. 구형 검색기로 자동 전환하지 않습니다. `MALMOI_NATIVE_INDEX`, `MALMOI_KIWI_MODEL`은 명시적 경로 재지정용입니다.

### 전체 API 검증

```bash
cmake --build build/team --target test_native_all_api test_native_two_player test_unicode_lower test_native_index_live malmoi_cli
ctest --test-dir build/team --output-on-failure
python tests/test_native_runtime.py --binary build/team/test_native_index_live --unicode build/team/test_unicode_lower --cli build/team/malmoi_cli
```

마지막 Python 명령은 표준 라이브러리만 사용하는 테스트 드라이버이며 검색 런타임 의존성이 아닙니다. CTest의 게임 테스트는 존재하지 않는 MLM 경로를 지정해 기본 모드의 독립성을 확인합니다. `tests/fixtures/native_similarity.json`은 기존 TF-IDF 행렬에서 별도로 계산한 float64 TF-IDF 기준값이며, `tests/fixtures/native_hybrid_similarity.json`은 TF-IDF 및 BM25 행렬을 독립적으로 결합한 float64 기준값입니다.

### 말랑이 정답 뜻풀이 고정

기본 단어 유사도 API는 기존처럼 모든 뜻풀이 쌍 중 최대값을 사용합니다. 말랑이는 별도 API로 정답 뜻풀이 하나를 고정합니다.

- `malmoi_sense_count/at`: 표제어의 원본 뜻풀이 ID와 정의를 열거합니다. `sense_id`는 원본 ID이며 행 번호나 표제어 ID가 아닙니다.
- `malmoi_similar_sense(engine, word, sense_id, ...)`: 선택한 정답 뜻풀이와 각 후보 단어의 가장 가까운 뜻풀이를 비교합니다.
- `malmoi_similarity_sense(engine, word, sense_id, guess, ...)`: 동일한 정답 뜻풀이로 추측 점수를 계산합니다. 방향이 있는 API이며 추측 단어의 뜻풀이는 최대값으로 선택합니다.
- 잘못된 표제어·뜻풀이 조합은 NOT_FOUND, 동일 ID가 서로 다른 정의를 가리키면 AMBIGUOUS, 영 벡터는 ZERO_VECTOR입니다. 레거시 엔진은 새 뜻풀이 API에 UNSUPPORTED를 반환합니다.

`game_start_sense`는 `GameState.answer + answer_sense_id`를 저장합니다. GameState 구조가 확장되므로 게임 실행 파일과 C 브리지를 함께 다시 빌드해야 합니다. 힌트·추측·복원·방 참가에서 이 조합을 유지합니다. 기존 `game_start`는 호환용으로 첫 번째 비영 뜻풀이를 선택하여 ID를 저장합니다. 이 순서는 의미에 대한 사람의 판정이 아니므로 일일 정답에는 명시적 ID 지정을 권장합니다.

```bash
cmake --build build/team --target malmoi test_native_sense_game
MALLANGI_ANSWER=이슬비 MALLANGI_SENSE_ID=250524 ./build/team/malmoi
```

`250524`는 비, `1626980`은 노래 제목의 뜻풀이입니다. 명시한 ID가 틀리면 다른 뜻풀이로 전환하지 않고 게임 초기화에 실패합니다. 정답 문자열을 맞히는 기존 승리 조건과 중복 추측·기회 차감 규칙은 유지합니다.

기존 선택형 웹 프로토타입은 `cmake --build build/team --target mallangi_web`로 새 브리지를 빌드해야 합니다. 서버·시작 스크립트는 기본적으로 `build/team/libmallangi_web.so`를 사용하며 `MALLANGI_LIBRARY`로 재지정할 수 있습니다. 웹 저장 형식 v3에는 `sense_id`와 현재 점수 체계가 적용됩니다. v1에는 뜻풀이 정보가 없고 v2는 이전 점수 체계를 사용하므로 자동 복원하지 않습니다. 기존 파일을 보존하고 별도 새 저장 파일로 시작하세요. 게임 진행 중에는 정답 ID를 HTTP 응답에 노출하지 않으며, 종료 후 답과 함께 제공합니다.

말랑이 추측은 C 게임 진입점에서 양끝 Unicode 공백을 제거한 뒤 정답 비교·중복 검사·뜻풀이 점수 조회·기록에 동일한 문자열을 사용합니다. 내부 공백은 보존합니다. 공백만 있는 입력·잘못된 UTF-8·길이 초과는 INVALID이며 기회를 차감하지 않습니다. 메뉴의 완전히 빈 줄은 기존처럼 게임에서 나가기입니다. 실제 메뉴 입력 회귀 검증은 `MALMOI_MENU_BINARY=build/team/malmoi python -m unittest discover -s tests -p test_mallangi_menu.py -v`로 실행합니다.

점수 체계가 바뀌었으므로 이전 웹 게임 저장 파일은 그대로 보존하고, 웹 프로토타입을 새 상태로 시작하려면 새 경로를 지정합니다.

```bash
MALLANGI_STATE="$PWD/games/mallangi/mallangi-v3.sqlite3" bash games/mallangi/start.sh
```
