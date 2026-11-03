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
