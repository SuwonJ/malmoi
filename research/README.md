# 역방향 사전 실험 코드

로컬에서 진행한 Python 검색 연구를 이 폴더에 반입했다. TF-IDF/BM25/LSA, 형태소 검색, E5 전체 검색, BGE 재순위화, KURE/Qwen 후보 재평가와 수동 질의 도구가 포함되어 있다. C 구현은 저장소 루트의 `src/`·`include/`에서 별도로 진행한다.

## 원본 사전 없이 시작

저장소 루트에서 Python 3.11 이상으로 실행한다.

```bash
make setup
make test
make smoke
```

`make setup`은 `research/.venv`를 만들고 기본 의존성을 설치한다. `make test`는 모델 다운로드 없이 단위 테스트를 실행한다. `make smoke`는 직접 작성한 12개 예제 뜻풀이와 4개 예제 질의로 문자/어절 TF-IDF, BM25, 혼합 설정 총 7개를 실행한다. 결과·문항별 예측·판정표·로그·실행 조건은 `build/smoke/runs/`에 저장한다. 예제 점수는 실제 사전 품질 평가에 사용하지 않는다.

## 실제 사전으로 어휘 검색 비교

원본 사전은 사용자가 적법하게 확보한 우리말샘 JSON 파일을 준비한다. Git에는 원문을 올리지 않는다. 아래부터는 **`research/` 폴더 기준**이다.

```bash
cd research
.venv/bin/python scripts/extract.py --source /path/to/우리말샘-json
.venv/bin/python -m src.experiment_suite
.venv/bin/python scripts/try_models.py --models char --query "아주 조금 내리는 비"
```

변환 결과는 `data/processed/dictionary.jsonl`에 저장된다. 이미 변환한 JSONL이 있으면 이 위치에 복사하거나 `src.experiment_suite --data /path/to/dictionary.jsonl`로 지정한다. 형식은 한 줄에 JSON 객체 하나이며 필수 필드는 `word`, `definition`, 고유어 필터용 `word_type: "고유어"`다. 선택 필드는 `id`, `pos`, `sense_type`다. 다른 어종도 포함하려면 실험 실행에 `--word-type all`을 지정한다.

`data/benchmark.json`은 기존 연구의 4문항 개발셋이다. 실제 품질 판단 전에는 문항 확대와 독립 테스트 분리가 필요하다. 예제 fixture와 구분한다.

## 형태소·임베딩·재순위화 비교

저장소 루트에서 추가 의존성을 설치한다.

```bash
make setup-advanced
```

`research/`에서 데이터 준비 후 실행한다. 모델 다운로드에는 네트워크와 별도 디스크 공간이 필요하다.

```bash
.venv/bin/python scripts/fetch_e5.py
.venv/bin/python scripts/fetch_reranker.py
.venv/bin/python scripts/run_quality.py

.venv/bin/python scripts/fetch_candidates.py
.venv/bin/python scripts/run_encoder_comparison.py --base experiments/runs/<새 advanced 실행 ID>
.venv/bin/python scripts/try_models.py --models char kure --query "아주 조금 내리는 비"
```

`run_quality.py`는 전체 검색과 BGE 재순위화를 순차 실행한다. 로그에 출력되는 `*-advanced` 실행 폴더를 `--base`로 전달한다. KURE/Qwen은 같은 문자/E5 후보 풀을 재평가하며 전체 사전 dense 검색과 다르다. 원본과 다른 사전/전처리로 생성한 캐시를 섞지 않는다.

모델 준비 스크립트는 실행 시 원격 revision을 확인해 다운로드하고 기록한다. 과거 실행과 같은 결과를 검증하려면 과거 manifest에 기록된 모델 revision과 데이터 해시를 맞춰야 한다.

## 파일 안내

| 경로 | 역할 |
| --- | --- |
| `src/experiment_suite.py` | 어휘 검색, 정규화, 지표, 시간·인덱스 크기 비교 |
| `src/advanced_experiments.py` | Kiwi·E5·RRF 전체 검색 |
| `src/rerank_experiments.py` | 실제 검색 후보에 대한 BGE 재순위화 |
| `src/encoder_screening.py` | 지정한 전체 검색 결과에서 KURE/Qwen 후보 재평가 |
| `scripts/try_models.py` | 대화형·단일·파일 질의 및 모델 비교 |
| `tests/` | 정규화·검색 점수·지표·모델별 풀링 검증 |
| `experiments/` | [방법론](experiments/METHODS.md), [결과](experiments/QUALITY.md), [세부 워크플로우](experiments/WORKFLOW.md) |
| `IMPORT.json` | 로컬 반입 출처·기준 커밋·반입 전 소스 해시 |

과거 실행 폴더에는 소형 보고서·메타데이터·집계와 로그만 보존했다. 원본 사전, 모델, `score_arrays.npz`, 후보 임베딩과 전체 후보 문서는 제외했다. 과거 실행을 입력으로 재평가하거나 보고서를 다시 생성하려면 누락된 아티팩트를 로컬에서 복사하거나 검색부터 재실행한다. 새 실행 결과는 기본적으로 Git에서 제외된다.

이전 환경의 명령과 판단 기록은 `ORIGINAL_README.md` 및 실험 문서에 남겼다. 설치·경로 안내는 이 README를 우선한다. 반입 시에는 데이터 변환 경로를 통일하고, 후보 재평가에 `--base` 옵션을 추가하고, 선택적 ONNX 의존성을 모델 생성 시점에 불러오도록 조정했다. 원래 로컬 폴더는 수정하지 않았다.
