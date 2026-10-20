"""Publish encoder screening separately from full-corpus retrieval benchmarks."""
import csv
import json
from pathlib import Path
from src.experiment_suite import ROOT, dump, fingerprint

MARKER='\n<!-- encoder-screening -->\n'


def build(kure,qwen):
    runs=[kure,qwen]; rows=[]; predictions=[]
    manifests=[json.loads((p/'manifest.json').read_text()) for p in runs]
    assert all(m['status']=='completed' for m in manifests)
    assert manifests[0]['candidate_score_sha256']==manifests[1]['candidate_score_sha256']
    assert json.loads((kure/'candidates.json').read_text())==json.loads((qwen/'candidates.json').read_text())
    lines=['## 한국어 특화·작업 지시형 임베딩 후보 재평가','',
           '**전체 검색 성능과 구분:** 아래는 문자/E5 각각 상위 200개 단어의 뜻풀이 합집합을 고정하고 재평가한 결과입니다.',
           '같은 4문항, 문항당 358–377개 뜻풀이, 총 1,237개 중복 제거된 문장입니다. 정답 강제 삽입은 하지 않았습니다.','',
           '| 메소드 | nDCG@10 | Hit@1 | Hit@5 | Hit@10 | 문서 캐시 시 질의 p95 ms | 문서 생성 s | 상세 |',
           '|---|---:|---:|---:|---:|---:|---:|---|']
    for p in runs:
        for r in json.loads((p/'results.json').read_text()):
            row={**r,'run':str(p.relative_to(ROOT))};rows.append(row)
            lines.append(f"| {r['method']} | {r['ndcg10']:.4f} | {r['hit1']:.0%} | {r['hit5']:.0%} | {r['hit10']:.0%} | {r['cached_document_query_p95_ms']:.1f} | {r['document_encode_seconds']:.1f} | [로그·결과](runs/{p.name}/REPORT.md) |")
        predictions.extend(json.loads((p/'predictions.json').read_text()))
    best=max(rows,key=lambda r:r['ndcg10'])
    lines += ['',f"현재 후보 재평가에서 최고 nDCG는 **{best['method']} ({best['ndcg10']:.4f})**입니다.",
              '문서 캐시 시 지연은 질의 인코딩·후보 벡터 점수·정렬만 포함합니다. 앞단 문자/E5 검색·모델 로드·캐시 미스 문서 인코딩은 제외했습니다.',
              '따라서 위 지연을 모바일 서비스 전체 지연 또는 전체 사전 dense 검색 지연으로 해석하면 안 됩니다.',
              'KURE는 원본 설정에 따라 CLS 풀링, Qwen은 마지막 유효 토큰 풀링을 사용했습니다. 모두 int8 변환본입니다.',
              '현재 4문항 개발셋에서 얻은 개선이며 독립 검증 결과가 아닙니다. 다음 검증은 평가셋 확장과 전체 사전 벡터 생성/기기 측정입니다.','']
    report=ROOT/'experiments/QUALITY.md'
    text=report.read_text().split(MARKER)[0]
    report.write_text(text+MARKER+'\n'.join(lines),encoding='utf-8')
    dump(ROOT/'experiments/encoder_comparison.json',{
        'status':'completed','runs':[str(p.relative_to(ROOT)) for p in runs],
        'candidate_sha256':fingerprint(kure/'candidates.json'),
        'source_sha256':fingerprint(ROOT/'src/encoder_comparison_report.py'),
        'results':rows,'predictions':predictions})
    with (ROOT/'experiments/encoder_comparison.csv').open('w',encoding='utf-8',newline='') as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
    pooled={}
    existing=ROOT/'experiments/quality_review.csv'
    if existing.exists():
        for row in csv.DictReader(existing.open()):
            pooled[(row['query'],row['word'])]={'definitions':set(row['definitions'].split(' | ')),
                                                'grade':row['grade_0_to_3'],'notes':row['notes']}
    for p in predictions:
        for t in p['top']:
            row=pooled.setdefault((p['query'],t['word']),{'definitions':set(),'grade':'','notes':''})
            row['definitions'].add(t['definition'])
    with existing.open('w',encoding='utf-8',newline='') as f:
        w=csv.writer(f);w.writerow(['query','word','definitions','grade_0_to_3','notes'])
        for (q,word),v in sorted(pooled.items()):w.writerow([q,word,' | '.join(sorted(v['definitions'])),v['grade'],v['notes']])
    (ROOT/'experiments/LATEST.md').write_text('# 최신 실험\n\n[전체 메소드·추가 임베딩 비교](QUALITY.md)\n\n[방법론과 결론](METHODS.md) · [재실행 워크플로우](WORKFLOW.md)\n',encoding='utf-8')
    print(report)
