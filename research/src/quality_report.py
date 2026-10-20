"""Consolidate a completed retrieval and reranker run without mixing evaluation sets."""
import csv
import json
from pathlib import Path
from src.experiment_suite import ROOT, fingerprint
from src.fusion_analysis import analyze


def build_report(retrieval,rerank):
    rm=json.loads((retrieval/'manifest.json').read_text())
    cm=json.loads((rerank/'manifest.json').read_text())
    if rm['status']!='completed' or cm['status']!='completed':
        raise ValueError('Only completed experiments can be consolidated')
    if cm['base_run']!=str(retrieval.relative_to(ROOT)) or cm['base_score_sha256']!=fingerprint(retrieval/'score_arrays.npz'):
        raise ValueError('Reranker was not run on these retrieval scores')
    if json.loads((retrieval/'benchmark.json').read_text())!=json.loads((rerank/'benchmark.json').read_text()):
        raise ValueError('Benchmark mismatch')
    rows=[]
    for r in json.loads((retrieval/'results.json').read_text()):
        rows.append({'method':r['method'],'ndcg10':r['ndcg10'],'hit1':r['hit1'],'hit5':r['hit5'],
                     'hit10':r['hit10'],'latency_ms':r['p95_ms'],'timing':r['timing_mode'],
                     'report':f'runs/{retrieval.name}/REPORT.md'})
    for r in json.loads((rerank/'results.json').read_text()):
        rows.append({'method':r['method'],'ndcg10':r['ndcg10'],'hit1':r['hit1'],'hit5':r['hit5'],
                     'hit10':r['hit10'],'latency_ms':r['estimated_total_ms'],'timing':'composed',
                     'report':f'runs/{rerank.name}/REPORT.md'})
    fusion_rows, fusion_predictions = analyze(retrieval, rerank)
    for r in fusion_rows:
        rows.append({k:r[k] for k in rows[0]})
    rows.sort(key=lambda r:(-r['ndcg10'],r['latency_ms']))
    baseline=next(r for r in rows if r['method']=='char')
    best=rows[0]
    lines=['# 메소드별 품질·비용 비교','',
           f"현재 4문항 개발셋의 최고 nDCG 방법: **{best['method']}** ({best['ndcg10']:.4f}).",
           f"문자 TF-IDF 기준선: {baseline['ndcg10']:.4f}; 차이 {best['ndcg10']-baseline['ndcg10']:+.4f}.",'',
           '전체 고유어 사전 266,506개 뜻풀이를 대상으로 검색했습니다. 정답을 후보에 강제로 넣지 않았습니다.','',
           '| 메소드 | nDCG@10 | Hit@1 | Hit@5 | Hit@10 | 시간 ms | 측정 구분 | 상세 |',
           '|---|---:|---:|---:|---:|---:|---|---|']
    for r in rows:
        lines.append(f"| {r['method']} | {r['ndcg10']:.4f} | {r['hit1']:.0%} | {r['hit5']:.0%} | {r['hit10']:.0%} | {r['latency_ms']:.1f} | {r['timing']} | [로그·결과]({r['report']}) |")
    lines += ['', 'measured는 호스트 반복 측정 p95, composed는 단계별 지연의 합으로 추정한 값입니다. 통합 서비스나 모바일 p95가 아닙니다.',
              '시간에는 온라인 질의 처리를 반영하며, 최초 모델 로딩·오프라인 문서 임베딩 생성은 포함하지 않습니다.',
              'CPU 스레드는 lexical 1개, 신경망 ONNX 4개입니다. 전체 메모리/배포 크기는 지연 표에서 비교하지 않았습니다.',
              '평가 라벨이 있는 4문항에 여러 방법을 비교한 탐색 결과입니다. 독립 테스트 결과로 일반화하지 마세요.', '',
              '`prior-rrf`는 재순위화 결과를 본 후 추가한 진단 실험이며, 검색 순위와 교차 인코더 순위를 같은 비중으로 결합합니다. 시간에는 작은 최종 결합 비용을 제외했습니다.', '',
              '[방법론 조사·후속 후보](METHODS.md) · [실행 워크플로우](WORKFLOW.md) · [풀링 후보 판정](quality_review.csv)', '',
              f'[전체 검색 실행 정보](runs/{retrieval.name}/manifest.json) · [재순위화 실행 정보](runs/{rerank.name}/manifest.json)']
    (ROOT/'experiments/QUALITY.md').write_text('\n'.join(lines),encoding='utf-8')
    with (ROOT/'experiments/quality_summary.csv').open('w',encoding='utf-8',newline='') as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)
    pooled={}
    for run in (retrieval,rerank):
        for p in json.loads((run/'predictions.json').read_text()):
            for t in p['top']:
                pooled.setdefault((p['query'],t['word']),set()).add(t['definition'])
    for p in fusion_predictions:
        for t in p['top']:
            pooled.setdefault((p['query'],t['word']),set()).add(t['definition'])
    with (ROOT/'experiments/quality_review.csv').open('w',encoding='utf-8',newline='') as f:
        w=csv.writer(f); w.writerow(['query','word','definitions','grade_0_to_3','notes'])
        for (q,word),defs in sorted(pooled.items()):
            w.writerow([q,word,' | '.join(sorted(defs)),'',''])
    (ROOT/'experiments/LATEST.md').write_text('# 최신 실험\n\n[메소드별 품질·비용 통합 비교](QUALITY.md)\n\n[방법론 조사와 판단](METHODS.md) · [워크플로우](WORKFLOW.md)\n',encoding='utf-8')
    print(ROOT/'experiments/QUALITY.md')
