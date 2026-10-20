# 공통 인터페이스 v1 — 구현 기준

A의 C11 코어는 [`include/vecdb.h`](../include/vecdb.h)에 구현되어 있다. 기존 v0에서 제안했던 `vecdb_open(VecDB **out, const char *path)`와 `vecdb_close(VecDB *db)`·단일 단어 조회 시그니처를 유지했다. `similar_search`·`reverse_search`·게임 엔진은 각 담당자가 구현할 영역이다.

확인 범위인 현재 프로젝트와 Documents 아래에서 기존 `vecdb.[ch]`, `hash.h`, `tokenizer.h`는 찾지 못했다. 별도로 보유한 팀원 코드가 있으면 이 헤더와 비교하고 호출부를 함께 이행한다. 팀원 전원의 검토/동의를 이미 받았다는 뜻은 아니다.

## 공유 개념

- **Word**: 이번 데이터셋의 word ID, 원문 표제어, 정규화 검색 키, 원본 어휘 식별자.
- **Sense**: 별도 sense ID, word ID 참조, 원본 source ID, 뜻풀이·품사.
- **VecDB**: opaque 읽기 전용 저장소. word/sense 각각 독립 차원과 공간 descriptor를 가진다.
- **MalmoiVector**: `const float *data`, `size_t dimension`, `const char *space`.
- **MalmoiHit**: `uint32_t id`, `float score`. 유사어는 word ID, 역사전은 sense ID로 정한다.

## 실제 API

```c
int vecdb_open(VecDB **out, const char *utf8_path);
void vecdb_close(VecDB *db);
size_t vecdb_word_count(const VecDB *db);
size_t vecdb_sense_count(const VecDB *db);
size_t vecdb_word_dimension(const VecDB *db);
size_t vecdb_sense_dimension(const VecDB *db);
int vecdb_word(const VecDB *db, uint32_t id, VecDBWord *out);
int vecdb_sense(const VecDB *db, uint32_t id, VecDBSense *out);
int vecdb_word_vector(const VecDB *db, uint32_t id, MalmoiVector *out);
int vecdb_sense_vector(const VecDB *db, uint32_t id, MalmoiVector *out);
int vecdb_find_word(const VecDB *db, const char *key, uint32_t *out_id);
int vecdb_find_words(const VecDB *db, const char *key,
                     uint32_t *out, size_t capacity, size_t *count);
int vecdb_dot(const MalmoiVector *a, const MalmoiVector *b, float *out);
int vecdb_cosine(const MalmoiVector *a, const MalmoiVector *b, float *out);
```

metadata/모델 공간 조회와 전체 타입 정의는 헤더가 기준이다. 헤더를 복사해서 담당자별로 수정하지 않고 공용 파일을 include한다.

## 상태·메모리·입력 계약

- 0=`VECDB_OK`. I/O, 형식 오류, 할당 실패, 범위 오류, 미등록, 동음이의어, 모델 공간 불일치, 0벡터, 크기 제한, 벡터 없음 등을 enum으로 구분한다. 오류 메시지는 `vecdb_status_string`으로 조회한다.
- `vecdb_open`은 실패 시 `*out=NULL`, 조회 함수는 실패 시 view를 비우고 ID는 `UINT32_MAX`로 설정한다. `find_words`의 count는 실제 전체 일치 수이며 용량 부족이면 `VECDB_LIMIT`다. 호출자가 제공한 배열의 기존/미사용 항목은 초기화하지 않는다.
- `find_word`는 여러 ID가 일치하면 `VECDB_AMBIGUOUS`다. `find_words`는 word ID 오름차순으로 결과를 채운다. B의 해시 테이블 역시 같은 검색 키의 여러 ID를 보존해야 한다.
- 조회 view의 문자열/벡터는 DB 소유다. 읽기 전용이며 `vecdb_close` 전까지만 유효하다. 반환 포인터를 해제하거나 DB를 닫은 뒤 쓰지 않는다. 닫은 포인터를 재사용하지 않고 호출자가 NULL로 바꾼다. NULL close는 안전하다.
- DB 로딩 뒤에는 읽기/점수 계산에 공유 쓰기 상태가 없다. 사용 중 DB를 닫는 동시 호출은 허용하지 않는다.
- 문자열은 UTF-8이다. 입력 검색 키의 NFC·`-`/`^` 제거는 CLI/tokenizer 경계에서 수행하고, C 코어는 저장된 키와 바이트 비교한다. 잘못된 UTF-8 검색 입력은 거부한다.
- 내부 ID는 데이터 버전에 종속된다. word와 sense ID를 서로 교환하지 않는다. 함수마다 어느 ID를 받는지 이름으로 구분한다.
- 점수 계산은 차원과 공간 descriptor의 정확 일치를 요구한다. cosine은 norm을 계산하고 0벡터는 오류, dot은 raw 내적이다. 결과는 확률이 아니다.

## 모듈 경계와 후속 통합

| 담당 | 코어에서 받는 것 | 직접 구현할 것 |
| --- | --- | --- |
| A | 공통 형식·작성기·DB 로딩·읽기 view·벡터 계산 | 모델/데이터 버전 변경 시 호환성 유지 |
| B | word 열람, 단일/복수 키 조회, word 벡터 | 해시 인덱스, Top-K, 자기 자신 제외, CLI |
| C | sense→word 매핑, 뜻풀이/원본 ID, sense 벡터 공간 | tokenizer, 질의 벡터 생성, reverse Top-K·품질 평가 |
| D | word 공간·게임 후보 ID·cosine | 추측·힌트·시도 횟수·종료 규칙, 선택적 서버/UI |

B/C의 검색 API는 호출자 소유 `MalmoiHit` 배열에 상위 K개를 채우는 방향을 유지한다. 빈 결과와 오류가 섞이지 않도록 상태 코드+결과 수 방식의 최종 시그니처를 각 모듈 구현 때 맞춘다. 이 문서는 아직 구현하지 않은 검색 함수를 구현 완료로 선언하지 않는다.

파일 바이트 규격은 [DATA_FORMAT.md](DATA_FORMAT.md), 실행/팀별 예제는 [CORE_HANDOFF.md](CORE_HANDOFF.md)를 따른다. 변경 시 헤더·Python 작성기·C 리더·golden fixture·테스트를 함께 갱신한다.
