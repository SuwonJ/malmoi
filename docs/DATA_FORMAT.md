# MALMOI 공통 데이터 형식 v1

구현: [`tools/prepare.py`](../tools/prepare.py), [`src/vecdb.c`](../src/vecdb.c). 이 버전은 실행 가능한 초기 계약이다. 호환성을 깨는 변경은 새 format version으로 만들고, 변경 전에 호출부/fixture/검사를 함께 갱신한다.

## 논리 데이터와 ID

Word는 `id`, 원문 `word`, 검색용 `key`, 원본 어휘 식별용 `lexeme_id`, 선택적 벡터를 갖는다. Sense는 `id`, `word_id`, 원본 `source_id`, `definition`, `pos`, 선택적 벡터를 갖는다.

- word ID와 sense ID는 **각각 독립적인 0 기반 uint32 인덱스**다. 한 sense는 word 하나를 참조한다. `UINT32_MAX`는 API 실패 값으로 예약하며 레코드 개수 상한 때문에 실제 ID가 되지 않는다.
- 동일한 명시적 `lexeme_id`의 여러 sense는 같은 word를 공유한다. 같은 lexeme의 검색 키·word 벡터가 다르면 변환을 거부한다.
- 원본에 `lexeme_id`가 없으면 `sense:<source_id>`로 각각 보존한다. 같은 철자라는 이유로 동음이의어를 합치지 않는다. 입력의 `lexeme_id`는 내부에서 `lexeme:` 접두어를 붙여 fallback과 구분한다.
- word는 내부 lexeme 문자열, sense는 source ID 문자열의 Python Unicode 오름차순으로 정렬한다. 숫자처럼 보이는 ID도 문자열로 정렬한다. 원본 ID는 출력에 보존한다.
- 같은 선택 입력·버전·공간 설정에서 ID/파일은 재현된다. 레코드 추가/삭제로 행 번호가 바뀔 수 있으므로 ID는 **데이터셋 버전 안에서만** 의미가 있다. 세션·게임 저장에는 dataset version/hash도 저장한다.
- 검색 키는 `unicodedata.normalize('NFC', word).replace('-', '').replace('^', '')`다. 원문 표기는 별도 보존한다. C 코어는 저장된 키를 바이트 비교하고 Unicode 정규화를 직접 수행하지 않는다. 입력 정규화는 tokenizer/CLI 경계에서 같은 정책을 사용한다.

## 바이트 배치

모든 정수는 unsigned little-endian, 실수는 IEEE 754 binary32 little-endian이다. C 구조체의 메모리 배치·포인터·`size_t`를 저장하지 않는다. 파일 크기는 v1에서 **최대 512 MiB**, 공간별 차원은 **0..65,536**이다. 파일 전체를 메모리에 읽고 필요한 뷰를 디코딩하므로 순간 메모리는 파일 크기보다 크다. 대용량 전체 dense 사전은 분할/별도 차기 형식의 대상이다.

| 오프셋 | 크기 | 값 |
| ---: | ---: | --- |
| 0 | 8 | magic: ASCII `MALMOI` + NUL 2개 |
| 8 | 4 | format version = 1 |
| 12 | 4 | header bytes = 40 |
| 16 | 4 | word count |
| 20 | 4 | sense count |
| 24 | 4 | word dimension; 0이면 해당 벡터 없음 |
| 28 | 4 | sense dimension; 0이면 해당 벡터 없음 |
| 32 | 4 | flags: bit 0 word L2, bit 1 sense L2; 나머지는 0 |
| 36 | 4 | reserved = 0 |

40바이트 헤더 이후에는 다음 순서로 이어진다. padding/정렬 공백/끝 표식은 없으며 마지막 레코드 다음 추가 바이트도 거부한다.

1. 문자열 `metadata_json`, `word_space`, `sense_space`.
2. word count만큼: 문자열 `word`, `key`, `lexeme_id`, float32 word dimension개.
3. sense count만큼: uint32 `word_id`, 문자열 `source_id`, `definition`, `pos`, float32 sense dimension개.

모든 문자열은 **uint32 바이트 길이 + UTF-8 바이트**다. 저장 길이에 NUL을 넣지 않고 문자열 안의 NUL도 금지한다. C 로더가 별도로 NUL 종료 버퍼를 만든다. `pos`와 없는 공간의 descriptor만 빈 문자열을 허용한다. 비정상 UTF-8·overlong·surrogate·범위 밖 코드포인트는 거부한다. 원문에 탭·줄바꿈·따옴표·보충 평면 문자를 넣는 것은 가능하다.

metadata에는 데이터 버전, 생성기 버전, 레코드 수, 원본 SHA-256, 논리 데이터 SHA-256, synthetic 여부, 정규화/ID 정책을 기록한다. C는 metadata/space를 UTF-8 opaque 문자열로 제공하며 JSON의 의미를 해석하지 않는다. header/레코드가 로딩의 기준이다. Python 작성기·참조 리더가 JSON 구조를 다룬다. 외부 metadata를 UI/JSON에 넣을 때는 escape한다.

## 벡터 공간 계약

공간 descriptor는 키를 정렬하고 공백을 제거한 JSON 문자열이다. 필수 필드는 `model_id`, `revision`, `dimension`, `dtype: "float32"`, `pooling`, `normalization: "none" | "l2"`, `query_prefix`, `document_prefix`다. task·purpose·토크나이저 버전 등 필요한 정보는 추가 필드에 넣을 수 있다. 추가 필드 역시 공간 동등성에 포함한다.

비교하려면 **차원과 descriptor 문자열 전체가 일치**해야 한다. 차원만 같거나 모델 이름만 같은 벡터는 거부한다. 모델 descriptor는 실제 인코딩을 수행한 호출자가 정확하게 부여해야 한다. C 코어가 벡터 값만으로 사용 모델을 알아낼 수는 없다.

- word/sense 공간은 독립적이다. 같은 모델·표현 규칙을 의도적으로 공유할 때만 동일 descriptor를 사용한다.
- 차원 0이면 descriptor는 빈 문자열, L2 플래그는 0이며 벡터 바이트를 저장하지 않는다. 벡터 조회는 `VECDB_UNSUPPORTED`다.
- 값은 모두 유한한 float32여야 한다. `l2` 선언은 float32 반올림 뒤 제곱 norm이 1에서 0.0002 이내인지 검사하며 0벡터는 거부한다.
- `none`에서는 0벡터를 저장할 수 있으나 코사인 계산은 `VECDB_ZERO_VECTOR`를 반환한다. 내적은 0이 가능하다. 결과를 임의의 확률로 바꾸지 않는다.
- C 코사인은 정규화 여부와 무관하게 norm을 계산한다. raw 내적과 구분하고 double 누산 후 float로 반환한다. 테스트 허용 오차는 작은 fixture 값에서 1e-7, 알려진 코사인 값에서 1e-6이다.
- sparse TF-IDF와 dense 임베딩을 같은 레코드 표현으로 섞지 않는다. 현 형식은 dense와 metadata-only를 지원한다.

## 입력·출력

연구 JSONL의 `id`, `word`, `definition`, `word_type`, `pos`, `sense_type`를 읽는다. `id`는 문자열/정수이며 선택된 입력 안에서 유일해야 한다. `lexeme_id`, `word_vector`, `sense_vector`를 선택적으로 추가한다. 벡터 공간을 선언했으면 모든 선택 레코드에 해당 길이의 벡터가 있어야 한다. 없는 벡터를 임의 난수로 채우지 않는다.

```bash
# 원본 연구 JSONL -> metadata-only C 데이터. 벡터 조회는 unsupported.
python3 tools/prepare.py --input research/data/processed/dictionary.jsonl \
  --output build/data/dictionary.mlm --dataset-version urimalsam-local-v1 --limit-words 1000

# 벡터를 포함한 예제 JSONL -> dense C 데이터
python3 tools/prepare.py --input tests/fixtures/small.mlm.jsonl \
  --spaces tests/fixtures/small.mlm.spaces.json \
  --output build/data/small.mlm --dataset-version synthetic-8-v1
```

기본 필터는 `word_type=고유어`, `--word-type all`로 해제한다. `--limit-words N`은 정렬된 word ID의 처음 N개와 그에 속한 모든 sense를 선택한다. 실제 입력이 N개보다 작으면 있는 만큼만 생성하며 결과 개수를 확인한다. 입력을 바꾸지 않고 바이너리/JSON sidecar를 각각 임시 파일에서 교체한다. 두 파일이 하나의 트랜잭션으로 바뀌는 것은 아니므로 sidecar `file_sha256`을 확인한다.

sidecar는 파일명 뒤 `.json`이다. 파일 SHA-256은 무결성 확인용이며 C 로더가 sidecar를 자동 검증하는 것은 아니다. C 로더는 헤더·범위·최소 레코드 크기·문자열·벡터·매핑과 파일 끝을 검증한다. 원본 source ID/lexeme ID의 의미적 유일성과 NFC 정책은 작성기가 보장하며 C가 Unicode/JSON/사전 지식을 재검증하지 않는다.
