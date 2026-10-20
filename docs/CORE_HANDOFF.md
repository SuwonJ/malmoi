# 공통 코어 사용법 — A → B·C·D

## 바로 실행

Python 3.11 이상과 C11 컴파일러가 있으면 대용량 사전·모델·pip 설치 없이 저장소 루트에서 실행한다.

```bash
make core-test
make demo-b
make demo-c
make demo-d
```

`make core-test`는 정적 라이브러리·C 검사기·단위 테스트를 빌드하고, 직접 작성한 1,000단어·1,001뜻풀이·8차원 fixture를 생성해 Python/C 호환성을 검사한다. `make fixture`만 실행하면 개발용 데이터와 원본 입력·모델 descriptor·게임 후보 목록을 받을 수 있다.

| 파일 | 용도 |
| --- | --- |
| `include/vecdb.h` | 모두가 포함할 공통 API |
| `build/core/libmalmoi.a` | Unix/MinGW 정적 코어 라이브러리 |
| `build/fixtures/dev-1000.mlm` | C 로더가 읽는 개발용 데이터 |
| `build/fixtures/dev-1000.mlm.json` | 데이터·모델·해시 metadata |
| `build/fixtures/dev-1000.mlm.jsonl` | 생성 입력; 연구 JSONL 호환 |
| `build/fixtures/dev-1000.mlm.spaces.json` | word/sense 공간 descriptor |
| `build/fixtures/dev-1000.mlm.candidates.json` | 0벡터를 제외한 게임 후보 word ID 999개 |
| `tests/fixtures/small.mlm` | Git에 포함된 8단어·9뜻풀이 golden 파일 |

1,000단어 파일 예상 SHA-256은 `749ed7b8ddb93f40e42c09e1332d533c07c2ad3cdced882868fdf8c2edc4103b`다. 모든 지원 환경에서 같은 바이트를 생성하는지 자동 검사한다. 데이터는 synthetic이며 실제 검색 정확도를 평가하지 않는다.

Python이 없는 팀원도 포함된 작은 fixture로 예제를 실행할 수 있다.

```bash
cc -std=c11 -Wall -Wextra -Wpedantic -Iinclude src/vecdb.c examples/word_demo.c -lm -o word-demo
./word-demo tests/fixtures/small.mlm
```

생성 파일은 `build/`에만 두며 Git에 올리지 않는다. 원본 사전·모델은 필요할 때 별도 준비한다. C 로더는 모델 추론을 수행하지 않는다.

## B: 유사어 검색·CLI

[`examples/word_demo.c`](../examples/word_demo.c)를 출발점으로 한다.

1. `vecdb_open` 한 번으로 DB를 열고 `vecdb_word_count`/`vecdb_word`로 사전 항목을 열람한다.
2. `VecDBWord.key`를 해시 테이블 키로 사용한다. 하나의 검색 키에 여러 word ID가 올 수 있다. 공통 `vecdb_find_word`는 선형 탐색 fallback이며 동음이의어에서 `VECDB_AMBIGUOUS`를 반환한다.
3. 여러 항목은 `vecdb_find_words`로 받는다. capacity=0으로 총 개수를 확인하면 결과가 있을 때 `VECDB_LIMIT`와 개수가 온다. 배열을 할당해 다시 호출한다.
4. `vecdb_word_vector`를 가져와 `vecdb_cosine`으로 비교한다. 자기 자신은 **word ID**로 제외하고, 동점은 word ID 오름차순으로 정한다. 0벡터는 제외한다.
5. 원문 출력은 `VecDBWord.word`, 검색은 `key`를 사용한다. 한국어 입력은 CLI/tokenizer에서 NFC·구분 기호 제거 정책을 적용한다. UTF-8 바이트를 한글 글자 단위로 잘라서는 안 된다.

모듈 빌드 예: `cc -std=c11 -Iinclude -c src/similar.c -o build/similar.o`. 실행 파일 링크에 `build/core/libmalmoi.a -lm`을 추가한다. `similar_search`·해시 테이블·CLI 메뉴 구현은 B 영역이며 코어가 대신 구현하지 않는다.

## C: 역방향 검색

[`examples/sense_demo.c`](../examples/sense_demo.c)는 sense ID→word ID→원문/뜻풀이와 벡터 공간을 연결한다.

- `vecdb_sense`의 `source_id`는 원본 사전 ID, `id`는 이번 데이터셋의 sense ID다. 결과를 표제어로 합칠 때 필요하면 같은 word ID의 최고 점수 뜻풀이를 선택한다.
- `vecdb_sense_vector`로 문서 벡터를 얻는다. 질의는 **같은 공간의 규칙으로 생성한 실제 float 배열**이어야 한다. `MalmoiVector`에 데이터·차원·일치하는 descriptor를 연결한다.
- 초기 C baseline은 tokenizer가 얻은 단어들의 동일 모델 벡터를 평균해 질의/뜻풀이를 표현하는 방식을 권장한다. 실제 데이터 제작 때 pooling·모르는 토큰·빈 질의·정규화 정책을 descriptor에 기록하고 query/document에 똑같이 적용한다. 새 모델 데이터로 바꾸면 fixture와 분리한다.
- KURE/E5 질의·뜻풀이 임베딩을 쓸 때는 각 모델의 query/document 지시문과 풀링을 유지한다. Python TF-IDF sparse 인덱스를 이 dense 형식에 넣지 않는다.
- 예제의 문서 벡터 복사는 API 연결 확인을 위한 정답이 알려진 self-test다. 자연어 입력을 벡터로 바꾸는 구현이나 검색 품질 개선을 뜻하지 않는다.

`reverse_search`와 tokenizer는 C 영역이다. 메모리 포인터는 DB가 살아 있는 동안만 쓸 수 있다.

## D: 게임

[`examples/game_demo.c`](../examples/game_demo.c)와 `.candidates.json`을 참고한다.

- 정답과 추측은 같은 데이터셋·word 공간의 word ID로 관리한다. 후보 파일의 dataset version과 file SHA-256을 DB sidecar와 맞춘다.
- 후보는 0벡터를 제외한 ID다. fixture의 ID 7은 예외 테스트용 0벡터로 후보에 없다. 실제 게임의 품사·희귀어·중복 철자 정책은 D가 별도로 정한다.
- 미등록 추측은 `VECDB_NOT_FOUND`, 동음이의어는 `VECDB_AMBIGUOUS`로 구분해 처리한다. 범위 밖 ID·공간 불일치·0벡터 오류를 점수 0으로 숨기지 않는다.
- 점수는 코사인이며 확률이 아니다. 시도 횟수·힌트·종료 규칙·날짜별 정답 선택은 게임 모듈이 담당한다.

## OS·컴파일러와 검사

Unix의 기본 `make core-test`는 `cc`를 사용한다. `CC`, `CPPFLAGS`, `CFLAGS`, `LDFLAGS`, `LDLIBS`, `BUILD_DIR`, `PYTHON3`를 덮어쓸 수 있다. GCC/Clang에서 C11·경고를 오류로 처리한다. macOS의 `gcc` 명령이 Apple Clang인 경우도 있으므로 CI에서 실제 compiler 버전을 함께 확인한다.

```bash
make core-test CC=clang BUILD_DIR=build/clang
make sanitize CC=clang
```

Windows는 MSVC Native Tools Command Prompt에서 GNU make 없이도 빌드할 수 있다.

```bat
mkdir build\core
cl /nologo /std:c11 /W4 /WX /utf-8 /Iinclude src\vecdb.c examples\word_demo.c /Febuild\core\word_demo.exe
build\core\word_demo.exe tests\fixtures\small.mlm
```

전체 Windows 빌드/검사 명령은 [CI](../.github/workflows/core.yml)에 있다. Python 도구의 콘솔 인코딩이 다르면 `PYTHONUTF8=1`을 설정한다. 파일 내부와 `vecdb_open` 경로는 언제나 UTF-8이다. Windows에서는 경로를 UTF-16으로 변환해 열고, 예제는 `wmain`에서 UTF-8 인자로 변환한다. 한글·이모지 파일 경로도 호환성 검사에 포함한다. MinGW에서 예제를 빌드할 때 필요한 `-municode`는 Windows용 Makefile 분기에 포함되어 있다.

CI는 Linux GCC/Clang, macOS compiler 명령 GCC/Clang, Windows MSVC에서 동일 fixture·손상 입력·필드·점수 검사를 수행한다. Linux Clang에서 AddressSanitizer/UndefinedBehaviorSanitizer도 실행한다. 포맷은 little-endian으로 고정했지만 big-endian CPU와 32-bit CPU에서 실행한 결과는 아직 없다.
