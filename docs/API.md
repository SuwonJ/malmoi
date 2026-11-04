# Malmoi API

## 공개 호출

```c
#include "malmoi.h"

Malmoi *engine = NULL;
MalmoiResult hits[10];
size_t count = 0;
float score = 0;
int status = malmoi_open(&engine);
if (status == MALMOI_OK) {
    status = malmoi_search(engine, "작은 물방울로 조용히 내리는 비", hits, 10, &count);
    /* 성공하면 hits[0..count-1]의 word/key/score 사용 */
    if (status == MALMOI_OK)
        status = malmoi_similarity(engine, "먼지잼", "이슬비", &score);
}
malmoi_close(engine);
```

| 함수 | 입력과 결과 |
| --- | --- |
| `malmoi_open(&engine)` | MLM 로드·검색 인덱스 준비. 실패 시 engine=NULL |
| `malmoi_search(engine, text, out, k, &count)` | 설명 문자열 → 뜻풀이 문자 겹침 순서의 단어·점수 |
| `malmoi_similar(engine, word, out, k, &count)` | 기존 단어 → 저장 벡터로 유사어·점수, 자기 키 제외 |
| `malmoi_similarity(engine, a, b, &score)` | 두 기존 단어 → 저장 벡터 코사인 |
| `malmoi_close(engine)` | 자원 해제. NULL 허용 |
| `malmoi_error_string(status)` | 오류 코드의 설명 |

공개 함수에는 모델명·차원·인코더 설정이 없습니다. 모든 실행은 C 내부에서 처리하며 외부 프로세스를 실행하지 않습니다. MLM에 word 벡터가 있어야 엔진을 열 수 있습니다.

## 데이터 지정

기본 경로는 작업 디렉터리 기준 `data/dictionary.mlm`입니다. 담당자는 환경 변수 `MALMOI_DATA`로 다른 MLM의 UTF-8 경로를 지정합니다.

```bash
MALMOI_DATA=/path/to/dictionary.mlm ./build/core/malmoi_cli pair "먼지잼" "이슬비"
```

Windows PowerShell에서는 `$env:MALMOI_DATA='C:\path\dictionary.mlm'`으로 설정합니다. 데이터 교체는 다음 open부터 적용되고 열린 핸들은 기존 데이터를 유지합니다. MLM만 바꾸면 팀원 호출 코드는 유지됩니다.

## 점수와 결과 수명

- `malmoi_search`: 뜻풀이의 문자 1·2·3그램 빈도 코사인 [0,1]. 공백·ASCII 구두점·일반 Unicode 구두점을 제외하고 ASCII 대문자를 소문자로 바꿉니다. 64비트 해시로 n-gram을 표현합니다. 의미 모델 추론은 하지 않습니다. 겹침이 없는 결과는 제외합니다.
- `malmoi_similar`, `malmoi_similarity`: MLM word 벡터의 코사인 [-1,1]. 뜻이 여러 개면 쌍별 최대값을 사용합니다. 확률이 아니며 설명 검색 점수와 절댓값을 비교하지 않습니다.
- 같은 표제어 키는 결과에 한 번만 반환합니다. 점수 내림차순, 키·ID 오름차순으로 동점을 정합니다.
- out 배열은 호출자가 k개 할당합니다. count는 실제 결과 수이고 실패 시 0입니다. 점수 출력도 실패 시 0입니다.
- word/key 문자열은 엔진이 소유하며 close 이후 사용하면 안 됩니다. ID는 해당 데이터셋 안에서만 유효합니다.
- 입력은 NFC UTF-8을 사용합니다. 설명 입력은 8,192바이트 이내입니다. 빈 입력·잘못된 UTF-8·구두점뿐인 설명은 오류입니다. k=0/out=NULL은 결과 없이 성공할 수 있습니다.
- 미등록 단어는 NOT_FOUND, 0벡터만 있는 단어는 ZERO_VECTOR입니다. 오류를 점수 0으로 숨기지 않습니다.
- 열린 엔진의 읽기 호출은 함께 사용할 수 있지만 close와 호출이 겹치면 안 됩니다. 뜻풀이 n-gram 인덱스는 open 때 생성해 재사용합니다.

## 빌드·링크

```bash
make
cc -std=c11 -Iinclude my_app.c build/core/libmalmoi.a -lm -o my_app
make test
make sanitize CC=clang
```

make 없이 Unix에서 직접 빌드할 수도 있습니다.

```bash
cc -std=c11 -O2 -Iinclude src/vecdb.c src/malmoi.c src/internal/search_backend.c src/internal/lexical.c examples/malmoi_cli.c -lm -o malmoi
```

Windows MSVC Developer Command Prompt:

```bat
cl /nologo /std:c11 /W4 /WX /utf-8 /Iinclude src\vecdb.c src\malmoi.c src\internal\search_backend.c src\internal\lexical.c examples\malmoi_cli.c /Femalmoi.exe
malmoi.exe pair "먼지잼" "이슬비"
```

MinGW 예제 빌드에는 `-municode`가 필요합니다. 운영체제별 실행 파일은 각각 빌드합니다. 로컬 Linux에서 검사했고 다른 OS는 CI 설정으로 검사합니다.

## 저수준 로더

`include/vecdb.h`에는 단어·뜻풀이·벡터 조회와 내적·코사인 함수가 있습니다. `vecdb_open` → 조회/계산 → `vecdb_close` 순서로 사용합니다. 조회 문자열과 벡터는 DB 소유입니다. 벡터 비교는 같은 차원과 동일한 공간 descriptor가 필요합니다.

MLM v1은 little-endian 헤더, metadata/공간 descriptor, word·sense 레코드로 구성됩니다. 작성·검증은 `tools/prepare.py`, 작은 입력 예시는 `tests/fixtures/small.mlm.jsonl`과 `.spaces.json`을 사용합니다. 로더는 최대 512 MiB 파일을 지원합니다. word/sense 공간은 별개이며, 벡터가 없는 공간 조회는 UNSUPPORTED입니다. synthetic 예제와 실제 데이터의 벡터 제공 범위를 구분합니다.

## 선택 Python 도구

C 실행과 별개로 데이터 제작·검색에 재사용할 수 있는 도구입니다. 연구 보고서·평가 러너·모델 다운로드 코드는 포함하지 않습니다.

### 원본 사전 추출 · 표준 라이브러리

```bash
python tools/extract_dictionary.py --source /path/to/dictjson --output /path/to/dictionary.jsonl
```

원본의 word/definition/id/word_type/pos/sense_type을 JSONL로 추출합니다. 단어·뜻풀이가 없는 항목은 제외하고 입력 파일을 덮어쓰지 않습니다. 변환 실패 시 기존 출력은 유지합니다. 검색용 정규화와 고유어 필터는 검색 단계에서 적용합니다.

### 문자 TF-IDF 역색인 검색 · 선택 의존성

```bash
python -m pip install -r tools/requirements.txt
python tools/lexical_search.py --dictionary /path/to/dictionary.jsonl --query "아주 조금 내리는 비"
```

연구에서 사용한 문자 2–4그램·sublinear TF·L2·float32 TF-IDF와 term→document CSR 인덱스를 추렸습니다. 기본 min_df는 작은 사전에서도 쓰도록 1이며 연구 설정 2는 `--min-df 2`로 지정합니다. 기본 최대 특징 수는 200,000입니다. `--query`를 반복하면 인덱스를 한 번 만들어 재사용합니다. 고유어만 읽으며 `--word-type all`로 전체를 읽습니다.

Python 모듈로는 `from tools.lexical_search import LexicalSearch, load_records` 후 `engine = LexicalSearch(load_records(path))`, `engine.search(text, k=10)`으로 호출합니다. 반환 dict에는 원문·정규화 키·출처 ID·뜻풀이·점수가 있습니다. 같은 키는 최고 점수 하나만 반환하고 0점은 제외합니다. 동점은 원본 레코드 순서입니다. 질의와 문서는 NFC로 정규화합니다.

이 Python TF-IDF 인덱스는 C 검색에 연결된 것이 아니며 C의 문자 1–3그램 점수와 다릅니다. ONNX·모델 다운로드 없이 사용할 수 있지만 NumPy·scikit-learn은 선택 설치해야 합니다.

### 검증한 기존 E5 캐시 → MLM · NumPy

```bash
python tools/export_e5.py --dictionary /path/to/dictionary.jsonl --cache /path/to/cache.npy --manifest /path/to/completed-run/manifest.json --model /path/to/existing-model --output /path/to/dictionary.mlm --limit 10000
```

완료된 연구 캐시와 원본 manifest가 이미 있을 때 사용합니다. 사전·캐시·가중치·토크나이저의 해시와 완료 상태를 확인하고, 중복 뜻풀이의 벡터 매핑을 유지해 MLM word 위치에 저장합니다. sense 벡터는 중복 저장하지 않습니다. 원본 순서의 앞부분을 선택하며 필요한 표제어만 `--include-word WORD`로 우선 포함할 수 있습니다.

ONNX 런타임이나 모델 추론을 실행하지 않습니다. 출처 검증에는 기존 모델 파일이 필요하고 새 데이터는 인코딩하지 못합니다. 여기서 만든 벡터는 C의 단어 유사어·점수 계산에 쓰이며, C 설명 검색은 계속 문자 기반입니다.

`make python-tools-test PYTHON3=/path/to/python`으로 선택 도구만 검사합니다. 모델과 외부 사전을 내려받지 않는 작은 테스트를 사용합니다.

### 전체 E5 캐시 스트리밍 변환

```bash
python tools/export_e5_full.py --dictionary /path/to/dictionary.jsonl --cache /path/to/e5-cache.npy --manifest /path/to/completed-run/manifest.json --model /path/to/existing-model --output /path/to/dictionary.mlm
```

전체 고유어 캐시를 SQLite 임시 매핑과 스트리밍으로 변환합니다. NumPy가 필요하고 ONNX 추론은 실행하지 않습니다. 동일한 source ID·내용은 합치며 같은 ID의 내용이 다르면 실패합니다. MLM v1의 512 MiB 제한을 넘으면 실패합니다. 원본·캐시·모델 해시와 완료 상태를 확인합니다.

현재 준비한 전체 데이터는 원본 고유어 266,506행에서 같은 중복 16행을 합친 266,490개 항목·204,632개 정규화 키·384차원입니다. 파일 크기는 445,200,799바이트, SHA-256은 `4ca824a1d8d7aa7a41e40d36cc91bdb1de6409d5292d4058ca04e88c2afb2d03`입니다. 우리말샘의 모든 어종을 포함한 데이터가 아니며 큰 파일·모델은 저장소에 포함하지 않습니다. ID는 기존 부분집합의 ID와 다릅니다.

### ONNX 질의 임베딩 · 팀 개발용 선택 도구

```bash
python -m pip install -r tools/requirements-onnx.txt
python tools/embed_query.py --database /path/to/dictionary.mlm --model /path/to/multilingual-e5-small --text "우울하고 답답한 기분" --output query.json
```

MLM과 일치하는 ONNX·tokenizer.json·download.json을 가진 모델 폴더가 필요합니다. 도구는 파일을 다운로드하지 않습니다. NumPy·ONNX Runtime·tokenizers는 이 도구를 실행하는 Python 환경에만 설치합니다. C 실행의 의존성은 바뀌지 않습니다.

출력 JSON의 vector는 L2 정규화된 384차원 질의 벡터입니다. space는 공간 정보 객체, space_json은 C의 descriptor 대조에 사용할 canonical JSON 문자열입니다. 모델·토크나이저 해시와 revision을 검사하며 query: prefix·attention-mask mean pooling·max_length 128을 사용합니다. 모듈로는 tools를 import 경로에 두고 QueryEncoder(database, model)를 한 번 생성한 뒤 encode(text)를 반복 호출합니다.

현재 malmoi_search는 여전히 문자 기반입니다. 질의 벡터를 공개 C API로 주입하거나 C에서 ONNX를 직접 실행하는 연결은 아직 없으므로, 이 도구가 C 설명 검색을 자동으로 바꾸지는 않습니다. 기존 내부 벡터 검색 또는 별도 역방향 검색 모듈에 연결해야 합니다. 현재 Linux에서 기존 Python E5Encoder와 동일 입력의 벡터가 일치함을 확인했습니다. 다른 운영체제의 ONNX 실행은 별도 확인이 필요합니다.
