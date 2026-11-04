# 모델 교체와 실행 모드

## 현재 기본: MLM만 사용하는 C 실행

CLion의 Debug는 `MALMOI_WITH_E5=OFF`, 실행 설정의 `MALMOI_MODEL`은 빈 값이다. 팀원용 전체 사전은 `data/full.mlm`과 실행 파일 옆 사전에서 자동으로 찾는다. `MALMOI_DATA`는 실험용 선택 설정이다. 모델 추론을 하지 않는다는 의미의 경량 모드이며, 전체 MLM 크기와 검색 인덱스에 필요한 메모리가 작아지는 것은 아니다.

| 기능 | 기본 모드 | 선택 E5 모드 |
|---|---|---|
| 설명 검색 | 문자 1–3그램 코사인 | 새 설명의 E5 벡터와 사전 벡터 코사인 |
| 유사어·게임 점수 | MLM의 저장 벡터 | 동일 |
| 배포 | OS에 맞는 실행 파일 + MLM | 실행 파일 + MLM + Python 환경 + 모델 + 인코더 스크립트 |

```bash
cmake --preset native
cmake --build --preset native
MALMOI_MODEL= MALMOI_DATA=/path/to/dictionary.mlm ./build/native-debug/malmoi
```

Ninja가 필요하다. Windows에서는 같은 환경 변수를 CLion 실행 설정 또는 PowerShell의 `$env:MALMOI_DATA`로 지정한다. 작업 디렉터리는 프로젝트 루트다. 기본 사전 `data/dictionary.mlm`은 8단어 합성 예제이므로 실제 개발에서는 전체 사전을 지정한다.

## 호출자는 그대로 유지

`include/malmoi.h`의 기존 함수 인자, 상태값, `MalmoiResult` 구조는 유지한다. 게임과 UI 담당자는 모델을 직접 로드하지 않는다.

```c
Malmoi *engine = NULL;
int status = malmoi_open(&engine);
if (status == MALMOI_OK) {
    MalmoiResult hits[10];
    size_t count = 0;
    status = malmoi_search(engine, "뜨거운 느낌", hits, 10, &count);
    /* status를 확인한 뒤 hits[0..count)를 사용한다. */
    malmoi_close(engine);
}
```

`malmoi_similar()`와 `malmoi_similarity()`도 그대로 사용한다. 추가된 경로·모드 조회 함수는 진단용이며 기존 호출자에게 필수는 아니다. 모델 교체 후에는 엔진을 닫고 다시 연다. 기존 엔진이 반환한 문자열은 close 이후 사용할 수 없다.

## 무엇을 바꾸려는지에 따른 수정 지점

| 변경 | 수정 지점 | 함께 필요한 작업 |
|---|---|---|
| 이미 만들어진 MLM 교체 | `MALMOI_DATA` | 재실행. C 재빌드 불필요 |
| 기본 설명 검색의 문자 처리·점수 | `src/internal/lexical.c`, `search_backend.c` | C 재빌드·회귀 테스트 |
| 같은 E5 규격의 새 모델·양자화 파일 | `MALMOI_MODEL`, 새 MLM | 새 모델로 사전 벡터 재계산, 해시·revision 갱신 |
| E5 입력 길이·prefix·pooling 변경 | `tools/embed_query.py`, `tools/export_e5.py`, `tools/export_e5_full.py` | 질의와 사전 양쪽 규칙을 맞추고 벡터·MLM 재생성 |
| 다른 모델 계열 | 별도 `QueryEncoder` 스크립트 + 해당 모델용 exporter | 토크나이저·출력 처리·차원·공간 검증 구현 |
| Python을 없앤 네이티브 인코더 | `src/internal/e5_encoder.c` 및 CMake | 내부 `MalmoiEncoder` 계약 구현, 공개 헤더 유지 |

양자화·가중치·토크나이저·pooling이 바뀌면 같은 차원이어도 같은 벡터 공간이라고 가정하지 않는다. 기존 MLM의 메타데이터만 바꿔 검사를 통과시키면 안 된다. 현재 exporter는 **이미 계산하고 검증한 캐시를 MLM으로 변환**할 뿐, 학습이나 전체 사전 임베딩 생성까지 수행하지 않는다. 새 모델의 캐시는 별도 오프라인 작업으로 만든다.

## 선택 E5를 켜는 방법

```bash
/path/to/python -m pip install -r tools/requirements-onnx.txt
cmake --preset e5 -DPython3_EXECUTABLE=/path/to/python
cmake --build --preset e5
MALMOI_DATA=/path/to/new/dictionary.mlm \
MALMOI_MODEL=/path/to/new/model \
MALMOI_ENCODER_SCRIPT=/absolute/path/to/tools/embed_query.py \
./build/e5/malmoi
```

Python 개발 헤더·공유 라이브러리도 필요하다. 선택 빌드는 Python 라이브러리에 링크하므로 MLM만 배포할 때에는 native 빌드의 실행 파일을 사용한다. CLion에서 시도할 때에는 별도 E5 프로필을 만들고 동일 옵션과 환경 변수를 지정한다. 개인별 절대 경로는 `.idea/` 또는 `CMakeUserPresets.json`에 보관한다.

모델 폴더 구성은 `download.json`, `tokenizer.json`, `onnx/model_quantized.onnx`다. 기본 인코더는 E5의 `query: ` / `passage: `, attention-mask mean pooling, L2 정규화, 길이 128을 검증한다. 모델·토크나이저 SHA-256과 revision도 MLM과 대조한다. 명시한 모델 설정이 잘못되면 오류이며 문자 모드로 자동 전환하지 않는다.

## 인코더 교체 계약

`MALMOI_ENCODER_SCRIPT`는 신뢰하는 Python 스크립트의 경로다. 기본은 `tools/embed_query.py`이며, 다른 구현도 아래 계약을 지키면 공개 C API를 바꾸지 않고 연결할 수 있다.

- `QueryEncoder(database, model)` 생성자: MLM과 모델을 읽고 호환성을 검증한다.
- `instance.space`: MLM의 word-space descriptor와 정확히 대응하는 JSON 객체다. C 어댑터가 canonical JSON으로 비교한다.
- `encode(text)`: MLM 차원과 같은 길이의 유한한 L2 정규화 float 목록을 반환한다. 오류는 예외로 전달한다.
- 모델과 토크나이저는 생성자에서 한 번 로드하고 재사용한다. 동시에 호출될 가능성을 고려한다.

현재 C의 의미 검색은 MLM **word vector 슬롯**을 검색한다. E5 exporter는 이 슬롯에 뜻풀이 임베딩을 저장한다. 다른 모델도 이 의미에 맞는 벡터를 저장하거나 내부 검색 구현을 함께 바꿔야 한다. 파일 규격 v1은 float32·최대 512 MiB이므로 int8 벡터 파일로 바꾸는 작업은 별도의 포맷 변경이다.

## 교체 검증 순서

1. 새 모델의 사전 벡터 캐시를 생성하고 출처·설정을 기록한다.
2. 동일 설정의 exporter로 새 경로에 MLM을 만든다. 기존 모델/MLM은 비교를 위해 보관한다.
3. `make test`로 모델 없는 공개 API와 파일 호환성을 검사한다.
4. `cmake --build build/e5 --target malmoi_cli` 후 아래 실제 모델 통합 테스트를 실행한다.
5. 대표 설명 20개 이상에 기대 단어를 정하고 상위 10개 포함률, 시작 시간, 질의 시간, 메모리를 이전 모델과 비교한다. 코사인 점수 자체를 정확도로 해석하지 않는다.

```bash
/path/to/python tests/test_e5_runtime.py \
  --cli build/e5/malmoi_cli --database /path/to/new/dictionary.mlm --model /path/to/new/model
```

이 테스트는 현재 E5 `QueryEncoder`의 연결·코사인·모드 전환·오류 검증이며 검색 품질 평가를 대신하지 않는다. 다른 모델 계열로 바꿀 때에는 테스트의 인코더 import와 테스트 자산도 새 구현에 맞춘다. 선택 E5 경로의 실제 실행 검증은 현재 Linux 기준이다.
