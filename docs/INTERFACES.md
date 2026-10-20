# 공통 인터페이스 초안 (v0 — 아직 확정 아님)

이 문서는 **팀원이 독립 작업할 수 있도록 정하는 계약 초안**이다. 실제 로컬 `vecdb.h`, `hash.h`, `tokenizer.h`를 확인하기 전 새 인터페이스를 강제로 덮어쓰지 않는다.

## 공유 데이터 개념

- **Word**: 표제어 ID, UTF-8 표제어, 단어 벡터, 품사 등.
- **Sense**: 우리말샘 `target_code` (또는 이에 대응하는 안정 ID), 표제어 ID, 뜻풀이, 검색용 벡터.
- **VecDB**: 단어와 임베딩 배열을 담는 읽기 전용 저장소.
- **ReverseIndex**: 뜻풀이/검색 특징의 배열과 검색용 인덱스.
- **Hit**: 검색 결과 ID + float 점수. 유사어 결과 ID는 word ID, 역사전 결과 ID는 sense ID로 구분해야 한다.

동일한 표제어라도 뜻이 여러 개일 수 있으므로 **word ID와 sense ID를 혼동하지 않는다**.

## 제안 API (논의용, 실제 소스에는 아직 반영하지 않음)

```c
#include <stddef.h>
#include <stdint.h>

typedef struct VecDB VecDB;
typedef struct ReverseIndex ReverseIndex;

typedef struct {
    uint32_t id;
    float score;
} MalmoiHit;

/* A: 소유/메모리 관리. 0=성공, 나머지=오류 */
int vecdb_open(VecDB **out, const char *path);
void vecdb_close(VecDB *db);

/* B: 단어→ID 조회 및 단어 유사어 */
int vecdb_find_word(const VecDB *db, const char *utf8_word, uint32_t *out_word_id);
size_t similar_search(const VecDB *db, const char *utf8_word,
                      MalmoiHit *out, size_t k);

/* C: 설명→뜻풀이 검색 */
size_t reverse_search(const ReverseIndex *index, const char *utf8_query,
                      MalmoiHit *out, size_t k);
```

- 호출자가 `out`에 K개 크기의 배열을 할당하며 반환값은 **실제로 채운 결과 수**로 한다.
- 반환된 검색 결과 ID는 반드시 표제어/뜻풀이로 다시 해석할 수 있어야 한다.
- `char *`는 UTF-8이며, 비정상 입력 처리 정책을 별도 테스트한다.
- 점수는 0~1 확률을 뜻하지 않는다. 내적·cosine·BM25 점수를 화면에 퍼센트로 표시하지 않는다.
- 동일 점수에서는 일관된 타이브레이커(예: 안정 ID 오름차순)를 정의한다.
- 로딩 실패, 미등록 단어, 빈 질의의 오류 처리는 구현 전 합의한다. `size_t` 결과 0만으로 오류 원인을 구분하기 어려우면 상태 코드를 추가한다.

## 데이터 파일 계약 (형식 확정 전 체크리스트)

A와 B·C가 협의해 최소한 다음을 명시한다.

- 매직 바이트와 `format_version` (이후 변경 시 호환성 판단)
- 데이터 원본/사전 버전과 전처리 스크립트 버전
- vector dimension, 저장 dtype(`float32` 등), 정규화 여부
- UTF-8 문자열 인코딩, 길이 저장, endianness
- word ID ↔ 표제어, sense ID ↔ 뜻풀이, sense ID ↔ word ID 매핑
- 중복 표제어/동일 뜻풀이/동음이의어 처리
- 잘못된 파일 및 길이/메모리 초과 검사

**두 검색 기능이 같은 임베딩 모델을 강제로 공유할 필요는 없다.** 유사어 검색의 단어 벡터와 역사전 검색의 뜻풀이/질의 표현은 목적이 다르므로 파일 형식·모델 버전을 명시해 분리할 수 있어야 한다.

## 각 모듈이 외부에 제공할 것

- **A**: 단어 및 뜻풀이 정보 조회, 벡터 조회, 공통 유사도 연산, 메모리 해제.
- **B**: word ID 인덱싱과 유사어 Top-K; CLI가 세 기능을 호출하도록 연결.
- **C**: UTF-8 질의 전처리와 역사전 Top-K; 가중 평균 등 알고리즘별 구현을 교체 가능하게.
- **D**: 게임 시작/추측/힌트/종료 API; 서버는 동일한 엔진 함수를 호출하도록 분리.

## 합의가 필요한 부분

1. 로컬 코드 파일명 및 기존 구조 보존 또는 `src/`·`include/` 이동 여부
2. 단어 ID / sense ID 체계와 binary format
3. C 구현 방식: 코사인 전수 탐색 baseline과 인덱스 최적화 범위
4. 역사전 baseline과 Python의 E5/TF-IDF 연구 결과를 어떤 단계에서 연결할지

이 문서의 코드 조각은 **제안**이며 이미 빌드 가능한 API가 아니다.
