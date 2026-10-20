# Korean reverse dictionary experiments

한국어 설명으로 단어를 찾는 역방향 사전의 검색 방법을 비교합니다. 목표는 저사양 모바일에서 유용한 검색 결과를 5초 내 제공하는 것입니다.

- [확장 방법론 조사](experiments/METHODS.md)
- [최신 메소드별 결과](experiments/LATEST.md)
- [실험 실행·로그·평가 워크플로우](experiments/WORKFLOW.md)
- [초기 어휘 검색 실험 결론](experiments/DECISION.md)
- [기존 분석 기록](experiments/notes.md)

```bash
mamba run -n datascibase python -m src.experiment_suite
```

실행마다 `experiments/runs/` 아래에 비교 보고서, CSV, 문항별 검색 결과, 로그, 환경 및 소스 스냅샷을 함께 저장합니다.

의미 검색·교차 인코더까지 전체 비교하려면 도구/모델 준비 후 아래 명령을 사용합니다.

```bash
.venv/bin/python scripts/run_quality.py
```

## 직접 질의로 모델 비교

```bash
# 대화형: 설명을 입력하면 문자 검색과 KURE 결과를 비교
python scripts/try_models.py

# 원하는 모델 선택 + 질의 한 번
.venv/bin/python scripts/try_models.py --models char e5 kure qwen-plain --query "아주 조금 내리는 비"

# 기존 벤치마크 비교
.venv/bin/python scripts/try_models.py --models char kure --benchmark

# 여러 질의: --query 반복 또는 한 줄에 하나인 텍스트 파일
.venv/bin/python scripts/try_models.py --models kure qwen --queries-file queries.txt

# 선택 가능한 모델 / 상세 옵션
.venv/bin/python scripts/try_models.py --list-models
.venv/bin/python scripts/try_models.py --help
```

`--models`는 공백으로 구분합니다: `char`, `e5`, `hybrid`, `kure`, `qwen`, `qwen-plain`.
대화형 종료는 `/quit` 또는 Ctrl-D입니다. 기본값은 `char kure`, 상위 10개 출력, 문자/E5 각각 200개 단어의 후보입니다. `--top-k`, `--candidates`, `--threads`로 조절합니다.

매 실행 결과는 `experiments/manual/<실행 ID>.json`에 저장됩니다(`--output 경로`로 변경 가능). KURE/Qwen은 같은 문자·E5 후보를 재평가하며 전체 의미 검색과 구분해서 출력합니다. 처음 만나는 후보 문서는 벡터 생성 때문에 오래 걸릴 수 있고 이후에는 `artifacts/playground/` 캐시를 재사용합니다. 시간은 공통 후보 검색과 모델 처리 단계로 나눠 표시하며, 모델 단계에는 최초 모델 로드와 새 문서 벡터 생성이 포함될 수 있습니다. 모델별 점수 절댓값은 서로 비교하지 않습니다.

`datascibase`에서 실행해도 추가 도구가 없으면 준비된 로컬 `.venv`를 자동 사용합니다. `scripts` 폴더에서는 `python try_models.py`로 실행하면 바로 `검색:` 입력창이 열립니다. 예: `마음이 초조해서 가만히 있지 못하는 모습`처럼 찾는 단어의 뜻이나 상황을 입력하세요.
