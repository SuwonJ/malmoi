"""Compare cross-encoder reranking on real retrieved candidates, never injected labels."""
import argparse
import csv
import importlib.metadata
import json
import platform
import sys
import time
import traceback
from datetime import datetime, timezone
from pathlib import Path

import numpy as np
import onnxruntime as ort
from tokenizers import Tokenizer
from threadpoolctl import threadpool_limits

from src.data import load_dictionary
from src.experiment_suite import ROOT, canonical, metrics, ranked_unique, dump, fingerprint, git


class Reranker:
    def __init__(self, model_dir, threads=4):
        self.tokenizer=Tokenizer.from_file(str(model_dir/'tokenizer.json'))
        self.tokenizer.enable_truncation(max_length=256)
        self.tokenizer.enable_padding(pad_id=1,pad_token='<pad>')
        opts=ort.SessionOptions(); opts.intra_op_num_threads=threads; opts.inter_op_num_threads=1
        self.session=ort.InferenceSession(str(model_dir/'onnx/model_quantized.onnx'),
                                          sess_options=opts,providers=['CPUExecutionProvider'])
        self.inputs={x.name for x in self.session.get_inputs()}

    def predict(self,query,documents,batch_size=4):
        scores=[]
        for start in range(0,len(documents),batch_size):
            tokens=self.tokenizer.encode_batch([(query,d) for d in documents[start:start+batch_size]])
            feed={'input_ids':np.asarray([t.ids for t in tokens],dtype=np.int64),
                  'attention_mask':np.asarray([t.attention_mask for t in tokens],dtype=np.int64),
                  'token_type_ids':np.asarray([t.type_ids for t in tokens],dtype=np.int64)}
            logits=self.session.run(None,{k:v for k,v in feed.items() if k in self.inputs})[0]
            scores.extend(logits.ravel().astype(float).tolist())
        return np.asarray(scores)


def render(run,base,rows,predictions):
    dump(run/'results.json',rows); dump(run/'predictions.json',predictions)
    with (run/'summary.csv').open('w',encoding='utf-8',newline='') as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)
    lines=['# 교차 인코더 재순위화 실험','',f'후보 검색 실행: [{base.name}](../{base.name}/REPORT.md)','',
           '| 메소드 | nDCG@10 | Hit@1 | Hit@5 | Hit@10 | 후보 Hit | 재순위화 p95 ms | 후보검색+재순위화 추정 ms |',
           '|---|---:|---:|---:|---:|---:|---:|---:|']
    for r in sorted(rows,key=lambda r:-r['ndcg10']):
        lines.append(f"| {r['method']} | {r['ndcg10']:.4f} | {r['hit1']:.2f} | {r['hit5']:.2f} | {r['hit10']:.2f} | {r['candidate_hit']:.2f} | {r['rerank_p95_ms']:.1f} | {r['estimated_total_ms']:.1f} |")
    lines += ['', '- BGE-reranker-v2-m3 int8 ONNX, CPU 스레드 4, 최대 입력 256토큰, batch 4.',
              '- 표의 재순위화 지연은 첫 호출을 제외한 반복 측정입니다. 토큰화·모델 추론·정렬을 포함합니다.',
              '- 총시간은 이전 검색 실행의 p95와 이번 재순위화 p95의 합입니다. 서로 다른 단계의 추정 합이며 통합 p95 실측은 아닙니다.',
              '- 후보는 실제 검색 점수에서만 뽑습니다. 정답 강제 삽입은 하지 않습니다.',
              '- 후보 수는 중복 제거된 단어 수입니다. 각 단어는 검색 점수가 가장 높은 뜻풀이 하나로 표현합니다.',
              '- def는 뜻풀이만, word-def는 표제어+뜻풀이를 교차 인코더에 입력합니다.',
              '- 현재 4문항 개발셋이므로 방법 선택 결과를 독립 테스트 성능으로 볼 수 없습니다.', '',
              '[진행 로그](run.log) · [재현 정보](manifest.json) · [전체 문항 결과](predictions.json) · [블라인드 판정 후보](review.csv)', '',
              '## 문항별 결과','']
    for p in predictions:
        lines += [f"### {p['method']} · {p['query']}",'',
                  f"nDCG={p['metrics']['ndcg10']:.4f}; 후보 내 정답={p['candidate_targets']}",'',
                  ' / '.join(x['word'] for x in p['top']),'']
    (run/'REPORT.md').write_text('\n'.join(lines),encoding='utf-8')


def execute(args,run,meta,log):
    base=args.base
    base_meta=json.loads((base/'manifest.json').read_text())
    if base_meta['status']!='completed':
        raise ValueError('Base retrieval run must be complete')
    if base_meta['data_sha256']!=fingerprint(ROOT/'data/processed/dictionary.jsonl'):
        raise ValueError('Dictionary changed since candidate retrieval')
    records=load_dictionary(ROOT/'data/processed/dictionary.jsonl','고유어')
    words=[canonical(r['word']) for r in records]
    cases=json.loads((base/'benchmark.json').read_text())
    dump(run/'benchmark.json',cases)
    archive=np.load(base/'score_arrays.npz')
    base_rows={r['method']:r for r in json.loads((base/'results.json').read_text())}
    meta['base_run']=str(base.relative_to(ROOT)); meta['base_score_sha256']=fingerprint(base/'score_arrays.npz')
    log('Loading BGE reranker')
    model=Reranker(args.model,threads=args.threads)
    rows,predictions=[],[]
    configs=[('e5-small-int8-full',50,False),('e5+char-rrf',50,False),
             ('e5+char-rrf',100,False),('e5+char-rrf',100,True)]
    for source,k,headword in configs:
        name=f'{source}-top{k}-bge-'+('word-def' if headword else 'def')
        log(f'Evaluating {name}')
        all_times, per_query, coverage=[],[],[]
        for qi,case in enumerate(cases):
            ids=ranked_unique(archive[source][qi],words,k)
            documents=[(words[i]+': ' if headword else '')+records[i]['definition'] for i in ids]
            times=[]
            for rep in range(args.repeats+1):
                start=time.perf_counter()
                logits=model.predict(case['query'],documents)
                # Rank raw logits directly; negative logits are valid candidates.
                order=np.lexsort((np.asarray(ids),-logits))
                top=[ids[i] for i in order[:10]]
                if rep:
                    times.append((time.perf_counter()-start)*1000)
            all_times.extend(times)
            m=metrics([words[i] for i in top],case['relevance']); per_query.append(m)
            expected={w for w,g in case['relevance'].items() if g>=3}
            present=expected & {words[i] for i in ids}; coverage.append(bool(present))
            predictions.append({'method':name,'query':case['query'],'metrics':m,'candidate_targets':sorted(present),
                'candidates':[{'id':records[i].get('id'),'word':words[i],'definition':records[i]['definition'],
                               'rerank_logit':float(logits[j])} for j,i in enumerate(ids)],
                'top':[{'word':words[i],'definition':records[i]['definition'],'id':records[i].get('id')} for i in top]})
            log(f'query {qi+1}: nDCG={m["ndcg10"]:.4f}, rerank mean={np.mean(times):.1f}ms')
        row={'method':name,**{k:float(np.mean([m[k] for m in per_query])) for k in per_query[0]},
             'candidate_hit':float(np.mean(coverage)),'rerank_p50_ms':float(np.percentile(all_times,50)),
             'rerank_p95_ms':float(np.percentile(all_times,95)),
             'estimated_total_ms':float(np.percentile(all_times,95))+base_rows[source]['p95_ms'],
             'timing_samples':len(all_times)}
        rows.append(row); render(run,base,rows,predictions)
        log(f'{name}: nDCG={row["ndcg10"]:.4f}, Hit@10={row["hit10"]:.2f}')
    pooled={}
    for p in predictions:
        for v in p['top']:
            pooled.setdefault((p['query'],v['word']),set()).add(v['definition'])
    with (run/'review.csv').open('w',encoding='utf-8',newline='') as f:
        w=csv.writer(f); w.writerow(['query','word','definitions','grade_0_to_3','notes'])
        for (q,word),defs in sorted(pooled.items()):
            w.writerow([q,word,' | '.join(sorted(defs)),'',''])
    meta['status']='completed'
    render(run,base,rows,predictions)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base',type=Path,required=True)
    parser.add_argument('--model',type=Path,default=ROOT/'artifacts/models/bge-reranker-v2-m3')
    parser.add_argument('--threads',type=int,default=4)
    parser.add_argument('--repeats',type=int,default=2)
    args=parser.parse_args(); args.base=args.base.resolve()
    if args.repeats<1 or args.threads<1:
        parser.error('threads and repeats must be positive')
    run=ROOT/'experiments/runs'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ-rerank'); run.mkdir(parents=True)
    meta={'status':'running','started_utc':datetime.now(timezone.utc).isoformat(),'command':sys.argv,
          'parameters':{k:str(v) if isinstance(v,Path) else v for k,v in vars(args).items()},
          'model_sha256':fingerprint(args.model/'onnx/model_quantized.onnx'),
          'tokenizer_sha256':fingerprint(args.model/'tokenizer.json'),
          'packages':{p:importlib.metadata.version(p) for p in ['numpy','onnxruntime','tokenizers','scikit-learn']},
          'model_download':json.loads((args.model/'download.json').read_text()),
          'git_commit':git('rev-parse','HEAD'),'platform':platform.platform()}
    (run/'source').mkdir()
    for p in (ROOT/'src').glob('*.py'):
        (run/'source'/p.name).write_bytes(p.read_bytes())
    dump(run/'manifest.json',meta)
    with (run/'run.log').open('w',encoding='utf-8',buffering=1) as f:
        def log(s):
            line=f'{datetime.now(timezone.utc).isoformat()} {s}'
            print(line,flush=True); f.write(line+'\n')
        try:
            with threadpool_limits(limits=1):
                execute(args,run,meta,log)
            log(f'Complete: {run/"REPORT.md"}')
        except BaseException:
            meta['status']='failed'; log(traceback.format_exc()); raise
        finally:
            meta['finished_utc']=datetime.now(timezone.utc).isoformat(); dump(run/'manifest.json',meta)


if __name__=='__main__':
    main()
