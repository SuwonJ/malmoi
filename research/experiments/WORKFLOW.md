# 분석·실험 워크플로우

목표: 저사양 모바일에서 사용자의 한국어 설명으로 유용한 단어를 5초 내 찾기.
진입점은 [LATEST.md](LATEST.md)입니다. 메소드 이름·품질·시간·숫자 인덱스 크기를 한 표에서 보고, 같은 실행 폴더의 로그와 문항별 결과를 확인합니다.

[이번 실험의 판단과 다음 우선순위](DECISION.md)

## KURE·Qwen3 후보 재평가 재실행

```bash
# 최초 모델 준비: revision·파일 해시 기록 (이미 준비됨)
.venv/bin/python scripts/fetch_candidates.py
# 같은 후보 풀로 KURE와 Qwen을 순차 실행하고 QUALITY.md에 결과 추가
.venv/bin/python scripts/run_encoder_comparison.py --base experiments/runs/<새 advanced 실행 ID>
# 저장된 실행으로 보고서만 재생성
.venv/bin/python scripts/run_encoder_comparison.py --reuse-kure experiments/runs/20261016T153031.806345Z-kure-screen --reuse-qwen experiments/runs/20261016T153107.509996Z-qwen-screen
```

새 실험은 `--base`로 후보 생성 기준인 advanced 실행 폴더를 지정합니다. 과거 기준 경로는 기본값으로 보존했습니다. 평가셋을 확장할 때는 먼저 새 평가셋으로 전체 검색 실행을 만들고 이 기준 경로를 갱신해야 합니다. 사전 해시가 다르면 실행을 거부합니다. 비교 시 두 모델의 후보 파일이 일치하는지도 확인합니다.

`encoder_comparison.csv/json`에 추가 수치·문항별 결과를 저장하고 각 실행 폴더에 원본 후보·모델 revision·로그·소스를 보존합니다. 후보 문서 벡터가 준비된 조건의 질의 시간과 오프라인 문서 생성 시간을 구분합니다. 전체 사전 dense 검색 결과가 아닙니다.

보고서 갱신 순서는 기존 `run_quality.py` 실행 후 `run_encoder_comparison.py`입니다. 앞 명령이 QUALITY.md를 다시 생성하므로 추가 비교를 유지하려면 뒤 명령 또는 저장 실행을 이용한 보고서 재생성을 이어서 실행합니다. 추가 판정 후보를 합칠 때 기존 `quality_review.csv`의 수동 등급/메모를 보존합니다.

## 의미 검색·재순위화까지 한 번에 실행

추가 도구는 `datascibase`를 기반으로 한 프로젝트 로컬 `.venv`에 설치했습니다. 기존 mamba 환경을 수정하지 않습니다.

```bash
# 최초 준비 (현재 작업 환경에는 설치/다운로드 완료)
/home/suwonj/miniforge3/envs/datascibase/bin/python -m venv --system-site-packages .venv
.venv/bin/python -m pip install -e '.[advanced]'
.venv/bin/python scripts/fetch_e5.py
.venv/bin/python scripts/fetch_reranker.py

# 전체 검색 → 재순위화 → 순위 결합 진단 → 통합 비교표
.venv/bin/python scripts/run_quality.py
```

- `experiments/QUALITY.md`: 추가 방법 전체의 메소드명·품질·시간 비교표
- `experiments/METHODS.md`: 논문/개발팀 문서에 근거한 방법론과 후속 실험 방향
- `experiments/quality_summary.csv`: 통합 수치
- `experiments/fusion_analysis.json`: 재순위화 후 검색 순위를 다시 결합한 진단 2종의 설정·입력 해시·결과
- `experiments/quality_review.csv`: 모든 추가 방법의 후보를 합친 블라인드 판정 파일
- `artifacts/models/`: revision이 기록된 공개 ONNX 모델/토크나이저
- `artifacts/advanced/`: 형태소 전처리 및 전체 뜻풀이 E5 임베딩 캐시. 데이터/모델 식별자로 분리하며 512문장 단위 완료 위치를 기록합니다.

`.venv`와 `artifacts`는 Git에서 제외됩니다. 실행별 소스 스냅샷과 JSON 결과는 `experiments/runs/`에 저장됩니다. 큰 `score_arrays.npz`는 Git에서 제외되므로 다른 컴퓨터에서 재순위화만 재실행하려면 해당 파일을 별도 복사하거나 검색 단계를 재실행해야 합니다.

개별 실행 및 이전 검색 결과 재사용:

```bash
.venv/bin/python -m src.advanced_experiments
.venv/bin/python -m src.rerank_experiments --base experiments/runs/<advanced 실행 ID>
.venv/bin/python scripts/run_quality.py --reuse-retrieval experiments/runs/<advanced 실행 ID>
# 기존 두 실행의 보고서만 재생성
.venv/bin/python scripts/run_quality.py --reuse-retrieval experiments/runs/<advanced 실행 ID> --reuse-rerank experiments/runs/<rerank 실행 ID>
```

E5 인덱싱은 전체 고유어 사전의 중복 뜻풀이를 묶어 처리합니다. 정답 라벨로 문서를 추가하거나 후보를 보정하지 않습니다. reranker는 실제 검색 후보만 평가합니다. 메모리 사용을 줄이기 위해 두 단계는 순차 실행합니다. 형식/토큰화/임베딩 구현을 바꿨다면 기존 캐시 재사용 여부를 검토하고 해당 캐시를 새로 생성해야 합니다.

통합 표의 `measured`는 직접 반복 측정, `composed`는 저장된 단계별 지연을 합한 추정입니다. 특히 재순위화의 총시간은 검색 p95와 재순위화 p95의 합이며 통합 p95 실측이 아닙니다. 신경망 추론은 CPU 4스레드, lexical은 1스레드입니다.

## 기존 어휘 검색 비교 실행

프로젝트 루트에서:

```bash
mamba run -n datascibase python -m unittest discover -s tests -v
mamba run -n datascibase python -m src.experiment_suite
# 추가 LSA 차원 비교 (비용 증가)
mamba run -n datascibase python -m src.experiment_suite --dimensions 128 256
# LSA 없이 어휘 검색 비교
mamba run -n datascibase python -m src.experiment_suite --dimensions
# 별도 평가셋 지정
mamba run -n datascibase python -m src.experiment_suite --benchmark data/benchmark.json --repeats 10
```

`datascibase`의 Python 직접 경로는 `/home/suwonj/miniforge3/envs/datascibase/bin/python`입니다. 현재 사용한 패키지는 numpy, scipy, scikit-learn, threadpoolctl이며 모델 다운로드 없이 실행합니다. 다른 환경에서는 `python -m pip install -e .`로 프로젝트 의존성을 설치합니다.

## 자동화 범위

1. 데이터/평가셋/소스 SHA-256, Git 커밋과 작업 트리 상태, 패키지 버전, 설정을 기록합니다. 변경 중인 코드도 실행 폴더에 스냅샷으로 보존합니다.
2. 정답의 사전 내 포함 여부를 원문/정규화 표기로 각각 감사합니다.
3. char TF-IDF, word TF-IDF, word BM25, char/word 혼합 3종, char/BM25 혼합, char LSA를 실행합니다. 동일 실행 내 TF-IDF 빌드를 공유합니다.
4. 같은 평가셋으로 단어 중복 제거 후 Hit@1/5/10, Recall@10, nDCG@10을 계산합니다. 쿼리 첫 호출과 반복 호출을 구분하고 반복 지연 p50/p95를 기록합니다.
5. 메소드마다 JSON/CSV/보고서를 갱신하므로 실행 중에도 완료된 메소드 결과가 보입니다. 실패 시 manifest의 status와 traceback을 남깁니다. 실패한 실행의 보고서는 부분 결과일 수 있으니 manifest를 확인합니다.
6. 최종적으로 모든 방법의 후보를 합친 `review.csv`를 만듭니다. 메소드 이름을 숨기고 등급 입력란을 비워 사람이 재평가할 수 있게 합니다.

## 기록 위치와 재현

- `experiments/LATEST.md`: 최신 비교 보고서 링크
- `experiments/runs/<UTC 실행 ID>/REPORT.md`: 한눈에 보는 결과와 해석
- `summary.csv`, `results.json`: 메소드별 집계
- `predictions.json`: 문항별 지표, 상위 10개 단어·뜻풀이·원본 ID·점수
- `benchmark_audit.json`: 정답 누락/표기 문제
- `review.csv`: 중복 제거된 후보의 수동 판정용 표
- `manifest.json`, `benchmark.json`, `source/`, `working-tree.patch`: 실행 조건과 소스 스냅샷
- `run.log`: 시작/빌드/평가/종료/오류의 UTC 로그

실행마다 새 디렉터리를 만들어 기존 결과를 덮어쓰지 않습니다. 프로세스 중단 후 재실행은 새 실행으로 시작합니다. 실행 간 디스크 캐시·중단 지점 재개는 아직 제공하지 않으며, 오래된 캐시와 코드 혼용을 피하도록 실행 내에서만 행렬을 재사용합니다. 병렬 suite 실행은 권장하지 않습니다(호스트 부하와 최신 링크 경쟁).

## 평가 정의와 주의점

- 표제어의 `-`와 `^`는 사전 형태 구분 기호이므로 평가·중복 제거 시 삭제합니다. 원문 표기는 결과에 보존합니다.
- 한 단어의 여러 뜻풀이는 최고 점수 하나로 집계합니다. 점수 0 이하 후보는 결과에 넣지 않습니다.
- 동점은 원본 데이터 순서로 결정합니다.
- 기존 `src/evaluate.py`의 `recall*`은 실제로 Hit@k입니다. 새 suite는 Hit와 Recall을 구분합니다.
- BM25는 어절 1–2그램, k1=1.2, b=0.75, min_df=2입니다. 길이 정규화는 특징으로 남은 어절/빅램의 출현 횟수를 기준으로 합니다. 형태소 BM25와는 다릅니다.
- char는 문자 2–4그램, word는 어절 1–2그램; TF-IDF는 min_df=2, 최대 20만 특징, sublinear_tf, float32입니다. 혼합은 정규화된 TF-IDF 점수의 가중합입니다. char/BM25는 각각 쿼리별 최대 점수로 나눠 0.5씩 결합합니다.
- 역색인 방향(CSR term→document)으로 곱셈하여 쿼리에 해당하는 posting을 활용합니다. 검색 점수는 기존 cosine 계산과 같습니다.
- nDCG는 현재 라벨 밖의 결과를 0으로 취급합니다. 라벨이 드문 평가에서는 새로운 좋은 결과가 불리할 수 있습니다. 사람이 풀링 후보를 판단해야 합니다.
- 벤치마크 4문항은 개발용 탐색입니다. 혼합 가중치를 여기서 비교한 결과를 독립 테스트 성능으로 보고하지 않습니다.
- 숫자 인덱스 크기는 sparse 행렬 또는 LSA 문서 벡터+투영 행렬만입니다. 전체 서비스 메모리/배포 크기가 아닙니다.

## 목표에 도달하는 실험 순서

1. **평가 신뢰성 먼저:** 표기 정규화와 중복 제거를 고정하고 실제 사용자가 입력하는 설명 100–200개를 수집합니다. 품사·짧은/긴 설명·뜻풀이와 어휘가 겹치지 않는 설명·희귀어를 포함하고 동일 단어/유사 질의를 같은 분할에 둡니다.
2. **블라인드 판정:** 풀링된 후보에 0=무관, 1=약한 연관, 2=유용한 대안, 3=의도한 단어의 등급을 부여합니다. 사람이 평가셋을 승인합니다. 평가 라벨을 모델의 자기평가로 자동 생성하지 않습니다.
3. **개발/검증/최종 테스트 분리:** 가중치·특징 수·차원은 개발/검증에서만 선택합니다. 고정 테스트는 선택 이후 한 번 평가합니다. 같은 질의의 반복 타이밍을 독립 품질 샘플로 세지 않습니다.
4. **가벼운 베이스라인 유지:** 이번 결과로 sparse 검색의 기준점을 잡고, 형태소 분석 TF-IDF/BM25를 추가해 어절 활용형 불일치를 줄이는 실험을 진행합니다. 형태소 분석기는 현재 환경에 설치되어 있지 않습니다.
5. **의미 검색은 조건부 투자:** 어휘 중복이 적은 질의에서 지속적으로 실패할 때 한국어/다국어 임베딩 후보와 lexical+dense 혼합을 비교합니다. 모델 크기·라이선스·오프라인 실행 가능성을 검토하고 모바일 양자화/ANN 효과도 측정합니다. 이번 실행에는 임베딩 모델을 포함하지 않았습니다.
6. **기기 검증:** 품질 상위 후보를 실제 저사양 모바일에서 측정합니다. cold 앱 시작+인덱스 로드, warm 쿼리 p95, 최대 RSS, 저장 크기, 반복 검색 발열을 함께 기록합니다. 호스트의 5초 이하 결과로 모바일 성공을 선언하지 않습니다.
7. **결정 규칙:** 독립 평가에서 검색 유용성이 높은 후보 중 실제 기기 p95 ≤5초 및 기기 메모리·저장 예산을 만족하는 방법을 선택합니다. 속도 여유가 있으면 추가 최적화보다 실패 문항의 품질 개선에 집중합니다.

## 기존 기록

`notes.md`, `results.csv`는 원본 기록으로 보존합니다. 기존 CSV는 `ndcg10` 추가 시 헤더가 갱신되지 않아 행별 열 수가 다른 상태입니다. 자동으로 복구하거나 새 결과와 합치지 않습니다. 과거 정답률은 표기 정규화·중복 제거 이전이라 새 수치와 직접 비교할 수 없습니다. 새 실험의 공식 진입점은 이 워크플로우와 `LATEST.md`입니다.
