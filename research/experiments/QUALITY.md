# 메소드별 품질·비용 비교

현재 4문항 개발셋의 최고 nDCG 방법: **char** (0.4050).
문자 TF-IDF 기준선: 0.4050; 차이 +0.0000.

전체 고유어 사전 266,506개 뜻풀이를 대상으로 검색했습니다. 정답을 후보에 강제로 넣지 않았습니다.

| 메소드 | nDCG@10 | Hit@1 | Hit@5 | Hit@10 | 시간 ms | 측정 구분 | 상세 |
|---|---:|---:|---:|---:|---:|---|---|
| char | 0.4050 | 0% | 0% | 25% | 13.3 | measured | [로그·결과](runs/20261015T062710.638809Z-advanced/REPORT.md) |
| e5+char-rrf | 0.3637 | 0% | 0% | 50% | 88.0 | composed | [로그·결과](runs/20261015T062710.638809Z-advanced/REPORT.md) |
| e5+char-rrf-top100-bge-word-def-prior-rrf | 0.3361 | 0% | 0% | 50% | 3815.0 | composed-excludes-small-fusion-cost | [로그·결과](fusion_analysis.json) |
| lexical-rrf | 0.3157 | 0% | 0% | 0% | 35.5 | composed | [로그·결과](runs/20261015T062710.638809Z-advanced/REPORT.md) |
| kiwi-tfidf | 0.3008 | 0% | 0% | 0% | 4.2 | measured | [로그·결과](runs/20261015T062710.638809Z-advanced/REPORT.md) |
| e5+char-rrf-top100-bge-def-prior-rrf | 0.2957 | 0% | 0% | 25% | 3270.1 | composed-excludes-small-fusion-cost | [로그·결과](fusion_analysis.json) |
| kiwi-bm25 | 0.2902 | 0% | 0% | 0% | 3.0 | measured | [로그·결과](runs/20261015T062710.638809Z-advanced/REPORT.md) |
| e5+char-rrf-top100-bge-word-def | 0.2722 | 0% | 25% | 50% | 3815.0 | composed | [로그·결과](runs/20261015T064333.730686Z-rerank/REPORT.md) |
| e5+lexical-rrf-rrf | 0.2685 | 0% | 0% | 0% | 99.2 | composed | [로그·결과](runs/20261015T062710.638809Z-advanced/REPORT.md) |
| e5+kiwi-bm25-rrf | 0.2315 | 0% | 0% | 0% | 68.6 | composed | [로그·결과](runs/20261015T062710.638809Z-advanced/REPORT.md) |
| e5-small-int8-full | 0.2081 | 25% | 25% | 25% | 41.7 | measured | [로그·결과](runs/20261015T062710.638809Z-advanced/REPORT.md) |
| e5+char-rrf-top50-bge-def | 0.2033 | 0% | 25% | 25% | 1741.6 | composed | [로그·결과](runs/20261015T064333.730686Z-rerank/REPORT.md) |
| e5+char-rrf-top100-bge-def | 0.1845 | 0% | 25% | 25% | 3270.1 | composed | [로그·결과](runs/20261015T064333.730686Z-rerank/REPORT.md) |
| e5-small-int8-full-top50-bge-def | 0.1780 | 0% | 0% | 0% | 1556.8 | composed | [로그·결과](runs/20261015T064333.730686Z-rerank/REPORT.md) |

measured는 호스트 반복 측정 p95, composed는 단계별 지연의 합으로 추정한 값입니다. 통합 서비스나 모바일 p95가 아닙니다.
시간에는 온라인 질의 처리를 반영하며, 최초 모델 로딩·오프라인 문서 임베딩 생성은 포함하지 않습니다.
CPU 스레드는 lexical 1개, 신경망 ONNX 4개입니다. 전체 메모리/배포 크기는 지연 표에서 비교하지 않았습니다.
평가 라벨이 있는 4문항에 여러 방법을 비교한 탐색 결과입니다. 독립 테스트 결과로 일반화하지 마세요.

`prior-rrf`는 재순위화 결과를 본 후 추가한 진단 실험이며, 검색 순위와 교차 인코더 순위를 같은 비중으로 결합합니다. 시간에는 작은 최종 결합 비용을 제외했습니다.

[방법론 조사·후속 후보](METHODS.md) · [실행 워크플로우](WORKFLOW.md) · [풀링 후보 판정](quality_review.csv)

[전체 검색 실행 정보](runs/20261015T062710.638809Z-advanced/manifest.json) · [재순위화 실행 정보](runs/20261015T064333.730686Z-rerank/manifest.json)
<!-- encoder-screening -->
## 한국어 특화·작업 지시형 임베딩 후보 재평가

**전체 검색 성능과 구분:** 아래는 문자/E5 각각 상위 200개 단어의 뜻풀이 합집합을 고정하고 재평가한 결과입니다.
같은 4문항, 문항당 358–377개 뜻풀이, 총 1,237개 중복 제거된 문장입니다. 정답 강제 삽입은 하지 않았습니다.

| 메소드 | nDCG@10 | Hit@1 | Hit@5 | Hit@10 | 문서 캐시 시 질의 p95 ms | 문서 생성 s | 상세 |
|---|---:|---:|---:|---:|---:|---:|---|
| kure-v1-int8-cls | 0.6197 | 25% | 75% | 100% | 20.7 | 26.5 | [로그·결과](runs/20261016T153031.806345Z-kure-screen/REPORT.md) |
| qwen3-embedding-0.6b-int8-instruct | 0.1532 | 0% | 0% | 25% | 58.5 | 56.4 | [로그·결과](runs/20261016T153107.509996Z-qwen-screen/REPORT.md) |
| qwen3-embedding-0.6b-int8-no-instruct | 0.2895 | 25% | 50% | 50% | 38.1 | 56.4 | [로그·결과](runs/20261016T153107.509996Z-qwen-screen/REPORT.md) |

현재 후보 재평가에서 최고 nDCG는 **kure-v1-int8-cls (0.6197)**입니다.
문서 캐시 시 지연은 질의 인코딩·후보 벡터 점수·정렬만 포함합니다. 앞단 문자/E5 검색·모델 로드·캐시 미스 문서 인코딩은 제외했습니다.
따라서 위 지연을 모바일 서비스 전체 지연 또는 전체 사전 dense 검색 지연으로 해석하면 안 됩니다.
KURE는 원본 설정에 따라 CLS 풀링, Qwen은 마지막 유효 토큰 풀링을 사용했습니다. 모두 int8 변환본입니다.
현재 4문항 개발셋에서 얻은 개선이며 독립 검증 결과가 아닙니다. 다음 검증은 평가셋 확장과 전체 사전 벡터 생성/기기 측정입니다.
