# malmoi

**뜻을 설명하고, 관련 우리말을 찾는 C 기반 검색 프로젝트**

2026년 2학기 C 언어 팀 프로젝트 — 김시현 · 나지연 · 장수원 · 허지훈

## 목표

1. **유사어 사전** — 단어를 입력해 벡터 유사도가 높은 관련 단어를 찾는다.
2. **역방향 사전** — 자연어로 상황·뜻을 설명하면 관련 표제어와 뜻풀이를 반환한다.
3. **유사도 게임** — 단어 벡터의 유사도·순위를 활용한 게임을 제공한다.

우리말샘의 고유어 데이터와 사전학습 벡터를 활용하되, **검색·게임의 주요 자료구조와 계산은 C에서 직접 구현**한다. 장기적으로 희귀하거나 잊혀가는 우리말을 탐색하기 쉬운 도구를 지향한다.

> **현재 상태 (2026-10-20):** Python 연구 코드와 A의 공통 C 코어를 반입·구현했다. word/sense 데이터 형식 v1, Python 작성기, C 벡터 로더·계산 API와 1,000단어 개발용 fixture가 동작한다. B·C·D는 [공통 코어 사용법](docs/CORE_HANDOFF.md)으로 독립 개발을 시작할 수 있다. [현재 진도](docs/PROGRESS.md)


## 바로 실행

C11 컴파일러와 Python 3.11 이상으로 공통 C 코어부터 확인한다. 이 단계에는 pip·원본 사전·모델이 필요 없다.

```bash
make core-test
make demo-b demo-c demo-d
```

Python 연구 실험은 별도로 설치한다:

```bash
make setup
make test
make smoke
```

`make smoke`는 직접 작성한 12개 예제 뜻풀이로 7개 어휘 검색 설정을 실행하고 `build/smoke/`에 결과를 저장한다. 실제 사전 전체 실험과 E5/KURE 모델 비교는 [research/README.md](research/README.md)를 따른다.

## 역할

| 담당 | 책임 | 주 소유 파일 |
| --- | --- | --- |
| 장수원 (A) | 데이터·공통 엔진·통합 관리 | `tools/prepare.py`, `src/vecdb.c`, `include/vecdb.h`, `Makefile` |
| 김시현 (B) | 유사어 검색·CLI·검증 | `hash.c/.h`, `similar.c`, `main.c` |
| 나지연 (C) | 형태소/토큰 처리·역방향 검색·품질 평가 | `tokenizer.c/.h`, `reverse.c` |
| 허지훈 (D) | 유사도 게임·웹 UI·서버 | `game.c`, `server.c`, `web/` |

모듈과 일정은 [팀 작업표](docs/TEAM.md), 함수와 데이터 계약은 [인터페이스 초안](docs/INTERFACES.md)을 참고한다.

## 저장소 구성

```text
research/            반입 완료: Python 검색·모델 실험과 평가 도구
  src/               TF-IDF, BM25, E5, BGE, KURE/Qwen 구현
  scripts/           데이터 변환, 모델 준비, 비교 실행, 수동 검색
  tests/             단위 테스트와 직접 작성한 소형 예제
  experiments/       방법론, 결과 요약, 과거 실행 보고서·메타데이터
src/vecdb.c          구현 완료: C 데이터 로더·조회·벡터 계산
include/vecdb.h      구현 완료: 공통 C API
tools/               공통 데이터 작성기·1,000단어 fixture 생성기
examples/            B·C·D용 독립 C API 호출 예제
tests/               C 단위 검사·Python↔C 호환성·golden fixture
docs/                설계, 역할, 파일 형식, 실험 규칙, 진행 현황
Makefile             C 빌드·메모리 검사·Python 설치/실험 진입점
.github/workflows/   연구 테스트와 Linux/macOS/Windows C CI
```

Python 연구 코드는 기존 `src` 패키지 구조를 유지한 채 `research/`로 분리했다. C용 `src/`·`include/`와 충돌하지 않는다. 별도로 보유한 기존 C 코드는 [로컬 이력 가져오기](docs/LOCAL_IMPORT.md)에 따라 보존·반입한다.

## 구현 순서

1. 공통 헤더/파일 형식 합의 → 1,000개 규모 테스트 데이터 배포
2. 각자 CLI에서 유사어·역방향 사전·게임 로직 독립 실행
3. 하나의 CLI에 통합하고 예외·품질·메모리 테스트
4. 검색 정확도/지연/메모리 분석, 결과 보고서
5. 여유가 있으면 웹 서버와 UI 연결

**첫 통합 목표는 UI보다 CLI로 세 기능이 작동하는 것.**

## 데이터와 라이선스

우리말샘 원본·사전학습 임베딩·대용량 바이너리 인덱스는 기본적으로 Git에 넣지 않는다. 배포·재배포 가능한 범위 및 출처 표기는 데이터 제공 기관과 모델 라이선스를 확인한 뒤 결정한다.

## 개발 규칙

- 이슈 하나당 작업 범위를 명시하고 `feat/<area>-<topic>` 브랜치에서 개발한다.
- 공용 헤더, 바이너리 형식, `main.c` 수정은 PR에서 다른 담당자의 검토를 받는다.
- 검색 알고리즘 변경 시 실행 명령·데이터 버전·측정 환경·전후 결과를 남긴다.
- **기존 코드 보존**, 무단 force-push 금지.

자세한 규칙은 [CONTRIBUTING.md](CONTRIBUTING.md)를 참고한다.
