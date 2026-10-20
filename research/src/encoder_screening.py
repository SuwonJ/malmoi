"""Compare larger encoders on a fixed, label-independent candidate pool."""
import argparse
import csv
import gc
import importlib.metadata
import json
import time
import traceback
from datetime import datetime, timezone
from pathlib import Path
import numpy as np
from threadpoolctl import threadpool_limits
from src.candidate_encoders import CandidateEncoder, INSTRUCTION
from src.data import load_dictionary
from src.experiment_suite import ROOT, canonical, ranked_unique, metrics, dump, fingerprint

BASE=ROOT/'experiments/runs/20261015T062710.638809Z-advanced'


def execute(args,run,meta,log):
    base=args.base.resolve()
    original=json.loads((base/'manifest.json').read_text())
    if original['data_sha256']!=fingerprint(ROOT/'data/processed/dictionary.jsonl'):
        raise ValueError('Candidate scores and dictionary must match')
    records=load_dictionary(ROOT/'data/processed/dictionary.jsonl','고유어')
    words=[canonical(r['word']) for r in records]
    cases=json.loads((base/'benchmark.json').read_text())
    archive=np.load(base/'score_arrays.npz')
    pools=[]
    for qi,case in enumerate(cases):
        ids=set()
        for method in ['char','e5-small-int8-full']:
            ids.update(ranked_unique(archive[method][qi],words,args.depth))
        pools.append(sorted(ids))
    all_ids=sorted(set(i for pool in pools for i in pool))
    unique_texts=list(dict.fromkeys(records[i]['definition'] for i in all_ids))
    text_position={t:i for i,t in enumerate(unique_texts)}
    log(f'{len(all_ids)} senses / {len(unique_texts)} unique definitions; per-query candidates {[len(x) for x in pools]}')
    dump(run/'candidates.json', [{'query':c['query'],'candidates':[{'row':i,'id':records[i].get('id'),'word':words[i],'definition':records[i]['definition']} for i in ids]} for c,ids in zip(cases,pools)])
    dump(run/'benchmark.json',cases)
    meta.update(base_run=str(base.relative_to(ROOT)),candidate_score_sha256=fingerprint(base/'score_arrays.npz'),
                pool_depth_per_method=args.depth,unique_definitions=len(unique_texts),candidate_senses=len(all_ids))
    name='kure-v1-int8' if args.kind=='kure' else 'qwen3-embedding-0.6b-int8'
    directory=ROOT/'artifacts/models'/name
    meta['model_download']=json.loads((directory/'download.json').read_text())
    model=CandidateEncoder(directory,args.kind,threads=args.threads)
    meta['onnx_inputs']=[{'name':x.name,'shape':x.shape,'type':x.type} for x in model.inputs]
    meta['onnx_outputs']=[{'name':x.name,'shape':x.shape,'type':x.type} for x in model.outputs]
    log(f'Loaded {name}; encoding documents with {"CLS" if args.kind=="kure" else "last-token"} pooling')
    start=time.perf_counter(); chunks=[]
    for begin in range(0,len(unique_texts),64):
        chunks.append(model.encode(unique_texts[begin:begin+64],batch_size=8))
        log(f'Encoded {min(begin+64,len(unique_texts))}/{len(unique_texts)}')
    vectors=np.concatenate(chunks)
    meta['document_encode_seconds']=time.perf_counter()-start
    meta['dimensions']=vectors.shape[1]
    meta['vectors_normalized_max_error']=float(np.max(np.abs(np.linalg.norm(vectors,axis=1)-1)))
    np.save(run/'candidate_embeddings.npy',vectors)
    rows,predictions=[],[]
    variants=[True] if args.kind=='kure' else [True,False]
    for instruct in variants:
        method=name+('-instruct' if args.kind=='qwen' and instruct else '-no-instruct' if args.kind=='qwen' else '-cls')
        log(f'Evaluating {method}')
        times,query_metrics,ceiling=[],[],[]
        for qi,(case,ids) in enumerate(zip(cases,pools)):
            positions=np.asarray([text_position[records[i]['definition']] for i in ids])
            for rep in range(args.repeats+1):
                start=time.perf_counter()
                q=model.encode([case['query']],query=instruct)[0]
                scores=vectors[positions] @ q
                # Maximum sense score per normalized word, preserving global row tie order.
                top_positions=ranked_unique(scores,[words[i] for i in ids],10)
                duration=(time.perf_counter()-start)*1000
                if rep:times.append(duration)
            selected=[ids[p] for p in top_positions]
            m=metrics([words[i] for i in selected],case['relevance']); query_metrics.append(m)
            expected={w for w,g in case['relevance'].items() if g>=3}
            ceiling.append(bool(expected & {words[i] for i in ids}))
            predictions.append({'method':method,'query':case['query'],'metrics':m,
                                'candidate_target_words':sorted(expected & {words[i] for i in ids}),
                                'top':[{'word':words[i],'definition':records[i]['definition'],'id':records[i].get('id'),
                                        'score':float(scores[ids.index(i)])} for i in selected]})
        rows.append({'method':method,**{k:float(np.mean([x[k] for x in query_metrics])) for k in query_metrics[0]},
                     'candidate_hit':float(np.mean(ceiling)),'cached_document_query_p95_ms':float(np.percentile(times,95)),
                     'document_encode_seconds':meta['document_encode_seconds'],'unique_definitions':len(unique_texts),
                     'dimensions':vectors.shape[1], 'timing_samples':len(times)})
        log(f"{method}: nDCG={rows[-1]['ndcg10']:.4f}, Hit@10={rows[-1]['hit10']:.2f}, query p95={rows[-1]['cached_document_query_p95_ms']:.2f}ms")
    dump(run/'results.json',rows); dump(run/'predictions.json',predictions)
    with (run/'summary.csv').open('w',encoding='utf-8',newline='') as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)
    lines=['# 더 큰 임베딩 모델 후보 재평가','',
           f'모델: {name}; 전체 사전 검색이 아니라 문자/E5 각각 상위 {args.depth}개 단어에 대응하는 뜻풀이의 합집합을 다시 평가합니다.','',
           '문항별 후보 풀은 정답 라벨을 보기 전에 검색 점수만으로 구성했습니다. 같은 후보 풀에 모든 모델을 적용합니다.','',
           '| 메소드 | nDCG@10 | Hit@1 | Hit@5 | Hit@10 | 후보 Hit | 캐시된 문서 기준 질의 p95 ms | 문서 생성 s |',
           '|---|---:|---:|---:|---:|---:|---:|---:|']
    for r in rows:
        lines.append(f"| {r['method']} | {r['ndcg10']:.4f} | {r['hit1']:.2f} | {r['hit5']:.2f} | {r['hit10']:.2f} | {r['candidate_hit']:.2f} | {r['cached_document_query_p95_ms']:.1f} | {r['document_encode_seconds']:.1f} |")
    lines += ['', '- 질의 p95는 첫 호출 제외 3회×4문항으로, 질의 임베딩·후보 벡터 검색·정렬을 포함합니다. 앞단 문자/E5 검색 시간과 모델 로드를 포함하지 않습니다.',
              '- 문서 벡터를 미리 계산했다고 가정한 실험입니다. 신규 질의를 지원하는 실제 배포에는 전체 사전 벡터 생성 또는 캐시 미스 처리 비용이 필요합니다.',
              '- 전체 의미 검색과 달리 후보 밖의 문서는 검색하지 않으므로 이 결과를 전체 사전 dense 검색 성능으로 해석하지 않습니다.',
              '- KURE는 원본 1_Pooling/config.json의 CLS 풀링을 따릅니다. 변환본 README의 mean 권고와 다릅니다.',
              '- Qwen은 마지막 유효 토큰 풀링을 사용합니다. 작업 지시를 넣은 질의와 넣지 않은 질의를 비교합니다.',
              '- 두 모델 모두 int8 변환본이며, 원본 float 모델의 성능 검증은 이번에 수행하지 않았습니다.',
              '- 현재 평가셋은 4문항 개발셋입니다. 동일 데이터에 대한 반복 탐색을 독립 검증으로 보지 않습니다.','',
              '[원본 후보](candidates.json) · [검색 결과](predictions.json) · [실행 로그](run.log) · [재현 정보](manifest.json)','']
    for p in predictions:
        lines += [f"## {p['method']} · {p['query']}",'', ' / '.join(t['word'] for t in p['top']),'']
    (run/'REPORT.md').write_text('\n'.join(lines),encoding='utf-8')
    meta['status']='completed'


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--kind',choices=['kure','qwen'],required=True)
    p.add_argument('--base',type=Path,default=BASE,help='Completed advanced retrieval run containing score_arrays.npz')
    p.add_argument('--depth',type=int,default=200)
    p.add_argument('--threads',type=int,default=4)
    p.add_argument('--repeats',type=int,default=3)
    args=p.parse_args()
    if min(args.depth,args.threads,args.repeats)<1:p.error('Numeric options must be positive')
    if not (args.base/'score_arrays.npz').exists():
        p.error('--base must contain score_arrays.npz; run scripts/run_quality.py first')
    run=ROOT/'experiments/runs'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ-'+args.kind+'-screen')
    run.mkdir(parents=True); (run/'source').mkdir()
    meta={'status':'running','started_utc':datetime.now(timezone.utc).isoformat(),'parameters':{k:str(v) if isinstance(v,Path) else v for k,v in vars(args).items()},'instruction':INSTRUCTION,
          'packages':{x:importlib.metadata.version(x) for x in ['numpy','onnxruntime','tokenizers']},
          'pooling':'CLS' if args.kind=='kure' else 'last-valid-token','max_length':128}
    for file in (ROOT/'src').glob('*.py'):(run/'source'/file.name).write_bytes(file.read_bytes())
    dump(run/'manifest.json',meta)
    with (run/'run.log').open('w',encoding='utf-8',buffering=1) as f:
        def log(s):
            line=f'{datetime.now(timezone.utc).isoformat()} {s}';print(line,flush=True);f.write(line+'\n')
        try:
            with threadpool_limits(limits=1):execute(args,run,meta,log)
            log(f'Complete: {run/"REPORT.md"}')
        except BaseException:
            meta['status']='failed';log(traceback.format_exc());raise
        finally:
            meta['finished_utc']=datetime.now(timezone.utc).isoformat();dump(run/'manifest.json',meta)


if __name__=='__main__':main()
