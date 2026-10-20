"""Morphology, full-corpus E5 and fusion experiments with resumable embeddings."""
import argparse
import csv
import gc
import importlib.metadata
import json
import os
import platform
import sys
import time
import traceback
from datetime import datetime, timezone
from pathlib import Path

import numpy as np
from threadpoolctl import threadpool_limits

from src.data import load_dictionary, load_benchmark
from src.experiment_suite import (ROOT, Lexical, canonical, metrics, ranked_unique,
                                  fingerprint, dump, git, audit, array_bytes)
from src.semantic_search import MorphIndex, content_text, rrf, E5Encoder


def render(run, rows, predictions):
    with (run/'summary.csv').open('w', encoding='utf-8', newline='') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader(); w.writerows(rows)
    dump(run/'results.json', rows)
    dump(run/'predictions.json', predictions)
    lines = ['# 확장 검색 실험', '',
             '고유어 전체 266,506개 뜻풀이를 검색 대상으로 사용. 현재 4문항 개발셋 결과이며 최종 테스트가 아닙니다.', '',
             '| 메소드 | nDCG@10 | Hit@1 | Hit@5 | Hit@10 | Recall@10 | Hit@100 | p95 ms | 시간 측정 | 숫자 인덱스 MiB |',
             '|---|---:|---:|---:|---:|---:|---:|---:|---|---:|']
    for row in sorted(rows, key=lambda r: -r['ndcg10']):
        lines.append(f"| {row['method']} | {row['ndcg10']:.4f} | {row['hit1']:.2f} | {row['hit5']:.2f} | {row['hit10']:.2f} | {row['recall10']:.2f} | {row['hit100']:.2f} | {row['p95_ms']:.2f} | {row['timing_mode']} | {row['numeric_index_mib']:.1f} |")
    lines += ['', '- measured: 질의 변환·전체 점수 계산·상위 1000개 단어 중복 제거까지 직접 반복 측정(첫 호출 제외).',
              '- composed: 각 단계의 반복 지연과 결합/정렬 시간을 합한 추정치. 통합 파이프라인 실측 p95가 아닙니다.',
              '- lexical 측정은 CPU 1스레드, ONNX 추론 스레드 수는 manifest의 threads를 확인하세요.',
              '- 숫자 인덱스는 모델 가중치·어휘·Kiwi 분석기·문자열·런타임 메모리를 제외합니다.',
              '- E5 문서 벡터는 전체 사전을 오프라인 처리한 것입니다. 질의 라벨로 후보를 추가하지 않습니다.',
              '- E5 입력은 query:/passage: 접두어, attention-mask 평균 풀링, L2 정규화, 최대 128토큰, int8 ONNX입니다.',
              '- E5와 lexical 결합은 순위 기반 RRF(constant=60, 각 방법 상위 1000뜻풀이).',
              '- 정답을 포함하는 후보를 찾는 능력(Hit@100)과 상위에 올리는 능력(Hit@1/10)을 구분합니다.',
              '- 정답 표기 정규화, 동일 단어 최고 뜻풀이 점수, 동점 데이터 순서를 적용합니다.',
              '- 4문항의 미평가 단어는 nDCG 계산 시 0점입니다. 블라인드 후보 재평가가 필요합니다.', '',
              '[실행 로그](run.log) · [재현 설정](manifest.json) · [문항별 결과](predictions.json) · [후보 판정](review.csv)', '',
              '## 문항별 상위 결과', '']
    for p in predictions:
        lines += [f"### {p['method']} · {p['query']}", '',
                  f"nDCG@10={p['metrics']['ndcg10']:.4f}; 등급3 정답 순위: {p['target_ranks']}", '',
                  ' / '.join(x['word'] for x in p['top'][:10]), '']
    (run/'REPORT.md').write_text('\n'.join(lines),encoding='utf-8')
    (ROOT/'experiments/LATEST.md').write_text(
        f'# 최신 실험\n\n[확장 메소드 결과](runs/{run.name}/REPORT.md) · [실행 로그](runs/{run.name}/run.log)\n\n'
        '[방법론 조사 및 결론](METHODS.md) · [워크플로우](WORKFLOW.md)\n', encoding='utf-8')


def execute(args, run, meta, log):
    from kiwipiepy import Kiwi
    data = ROOT/'data/processed/dictionary.jsonl'
    records = load_dictionary(data, '고유어')
    cases = load_benchmark(args.benchmark)
    for c in cases:
        c['relevance'] = {canonical(w): grade for w, grade in c['relevance'].items()}
    dump(run/'benchmark.json', cases)
    dump(run/'benchmark_audit.json', audit(records, cases))
    words = [canonical(r['word']) for r in records]
    texts = [r['definition'] for r in records]
    rows, predictions, cached_scores, latency = [], [], {}, {}
    meta['senses'], meta['queries'] = len(records), len(cases)
    log(f'Loaded {len(records):,} senses / {len(cases)} queries')

    def evaluate(name, scorer, size, stage_latency=None):
        all_times, stored, per_case, query_latencies = [], [], [], []
        for qi, case in enumerate(cases):
            times = []
            for rep in range(args.repeats + 1):
                t = time.perf_counter()
                scores = scorer(case['query'], qi)
                ids = ranked_unique(scores, words, 1000)
                duration = (time.perf_counter() - t) * 1000
                if rep:
                    times.append(duration)
            stored.append(scores.copy())
            if stage_latency is not None:
                times = [v + stage_latency[qi][j] for j, v in enumerate(times)]
            query_latencies.append(times)
            all_times.extend(times)
            ranked = [words[i] for i in ids]
            m = metrics(ranked, case['relevance'])
            expected = {w for w, grade in case['relevance'].items() if grade >= 3}
            m['hit100'] = float(bool(expected & set(ranked[:100])))
            per_case.append(m)
            predictions.append({'method':name, 'query':case['query'], 'metrics':m,
                'target_ranks': {w: ranked.index(w)+1 if w in ranked else None for w in sorted(expected)},
                'top': [{'word':words[i], 'raw_word':records[i]['word'], 'id':records[i].get('id'),
                         'definition':records[i]['definition'], 'score':float(scores[i])} for i in ids[:10]]})
        row = {'method':name, **{k:float(np.mean([m[k] for m in per_case])) for k in per_case[0]},
               'p50_ms':float(np.percentile(all_times,50)), 'p95_ms':float(np.percentile(all_times,95)),
               'timing_mode':'measured' if stage_latency is None else 'composed',
               'numeric_index_mib':size/1024**2, 'timing_samples':len(all_times)}
        rows.append(row); cached_scores[name] = stored; latency[name] = query_latencies
        render(run, rows, predictions)
        log(f"{name}: nDCG={row['ndcg10']:.4f}, Hit@10={row['hit10']:.2f}, Hit@100={row['hit100']:.2f}, p95={row['p95_ms']:.2f}ms ({row['timing_mode']})")

    log('Building char baseline')
    char = Lexical(texts, 'char', 200_000)
    size_char = char.numeric_bytes
    del char.x
    evaluate('char', lambda q, _: char.score(q), size_char)
    del char; gc.collect()

    log('Loading Kiwi and tokenizing corpus')
    kiwi = Kiwi(num_workers=1)
    cache = ROOT/'artifacts/advanced'
    cache.mkdir(parents=True, exist_ok=True)
    morph_key = meta['data_sha256'][:16] + '-' + importlib.metadata.version('kiwipiepy') + '-' + fingerprint(ROOT/'src/semantic_search.py')[:12]
    tokens_path = cache/f'morph-{morph_key}.json'
    t = time.perf_counter()
    if tokens_path.exists():
        token_texts = json.loads(tokens_path.read_text())
        log('Using fingerprint-matched morphology cache')
    else:
        token_texts = []
        for i, tokens in enumerate(kiwi.tokenize(texts)):
            token_texts.append(content_text(tokens))
            if (i+1) % 25000 == 0:
                log(f'Morphology {i+1:,}/{len(texts):,}')
        dump(tokens_path, token_texts)
    meta['morphology_load_or_build_seconds'] = time.perf_counter()-t
    morph_sizes = {}
    for kind in ('tfidf','bm25'):
        log(f'Building Kiwi {kind}')
        model = MorphIndex(token_texts,kind)
        size = morph_sizes[kind] = array_bytes(model.index)
        evaluate('kiwi-'+kind, lambda q, _: model.score_tokens(content_text(kiwi.tokenize(q))), size)
        del model; gc.collect()
    del kiwi, token_texts; gc.collect()
    lexical_names = ['char','kiwi-tfidf','kiwi-bm25']
    lexical_stage = [[sum(latency[n][q][j] for n in lexical_names) for j in range(args.repeats)] for q in range(len(cases))]
    evaluate('lexical-rrf', lambda _, qi: rrf([cached_scores[n][qi] for n in lexical_names]),
             size_char+sum(morph_sizes.values()), lexical_stage)

    # Encode ALL unique definitions; duplicate definitions share a vector.
    lookup, unique, mapping = {}, [], []
    for text in texts:
        if text not in lookup:
            lookup[text] = len(unique); unique.append(text)
        mapping.append(lookup[text])
    mapping = np.asarray(mapping,dtype=np.int32)
    del lookup; gc.collect()
    meta['unique_definitions'] = len(unique)
    log(f'Loading E5: {len(unique):,} unique definitions / {len(texts):,} senses')
    encoder = E5Encoder(args.model, threads=args.threads)
    vector_key = meta['data_sha256'][:16]+'-'+meta['model_sha256'][:12]+'-len128-passage-v1'
    vector_file, progress_file = cache/f'e5-{vector_key}.npy', cache/f'e5-{vector_key}.json'
    completed = 0
    if vector_file.exists() and progress_file.exists():
        progress = json.loads(progress_file.read_text())
        completed = progress['completed']
        vectors = np.load(vector_file,mmap_mode='r+')
        if vectors.shape != (len(unique),384):
            raise ValueError('Cached embedding shape mismatch')
        log(f'Resuming embeddings at {completed:,}')
    else:
        vectors = np.lib.format.open_memmap(vector_file,mode='w+',dtype=np.float32,shape=(len(unique),384))
    t = time.perf_counter()
    initially_completed = completed
    for start in range(completed,len(unique),512):
        end = min(start+512,len(unique))
        vectors[start:end] = encoder.encode(unique[start:end],'passage: ',batch_size=16)
        vectors.flush()
        dump(progress_file,{'completed':end,'total':len(unique),'data_sha256':meta['data_sha256'],
                            'model_sha256':meta['model_sha256']})
        if (start//512)%10==0 or end==len(unique):
            elapsed = time.perf_counter()-t
            rate = (end-initially_completed)/max(elapsed,1e-9)
            log(f'E5 embeddings {end:,}/{len(unique):,}; {rate:.1f} definitions/s; remaining ~{(len(unique)-end)/max(rate,1):.0f}s')
    meta['embedding_seconds_this_run'] = time.perf_counter()-t
    meta['embedding_cache_start'] = initially_completed
    meta['embedding_cache_file'] = str(vector_file.relative_to(ROOT))
    meta['embedding_sha256'] = fingerprint(vector_file)
    dense_bytes = vectors.nbytes+mapping.nbytes
    evaluate('e5-small-int8-full', lambda q, _: (vectors @ encoder.encode([q],'query: ')[0])[mapping], dense_bytes)
    for lexical in ('char','kiwi-bm25','lexical-rrf'):
        name = 'e5+'+lexical+'-rrf'
        stage = [[latency['e5-small-int8-full'][q][j]+latency[lexical][q][j]
                  for j in range(args.repeats)] for q in range(len(cases))]
        size = dense_bytes + (size_char if lexical=='char' else morph_sizes['bm25'] if lexical=='kiwi-bm25' else size_char+sum(morph_sizes.values()))
        evaluate(name, lambda _, qi, l=lexical: rrf([cached_scores['e5-small-int8-full'][qi],cached_scores[l][qi]]),size,stage)
    np.savez_compressed(run/'score_arrays.npz', **{n:np.asarray(v) for n,v in cached_scores.items()})
    pooled = {}
    for p in predictions:
        for v in p['top']:
            pooled.setdefault((p['query'],v['word']),set()).add(v['definition'])
    with (run/'review.csv').open('w',encoding='utf-8',newline='') as f:
        w=csv.writer(f); w.writerow(['query','word','definitions','grade_0_to_3','notes'])
        for (q,wrd),defs in sorted(pooled.items()):
            w.writerow([q,wrd,' | '.join(sorted(defs)),'',''])
    meta['status']='completed'
    render(run,rows,predictions)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--benchmark',type=Path,default=ROOT/'data/benchmark.json')
    parser.add_argument('--model',type=Path,default=ROOT/'artifacts/models/multilingual-e5-small')
    parser.add_argument('--threads',type=int,default=4)
    parser.add_argument('--repeats',type=int,default=3)
    args=parser.parse_args()
    if args.threads<1 or args.repeats<1:
        parser.error('threads and repeats must be positive')
    run=ROOT/'experiments/runs'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ-advanced')
    run.mkdir(parents=True)
    meta={'status':'running','started_utc':datetime.now(timezone.utc).isoformat(),'command':sys.argv,
          'parameters':{k:str(v) if isinstance(v,Path) else v for k,v in vars(args).items()},
          'git_commit':git('rev-parse','HEAD'),'git_status':git('status','--short'),
          'platform':platform.platform(),'python':sys.version,'threads':args.threads,
          'packages':{p:importlib.metadata.version(p) for p in ['numpy','scipy','scikit-learn','kiwipiepy','kiwipiepy_model','onnxruntime','tokenizers']},
          'data_sha256':fingerprint(ROOT/'data/processed/dictionary.jsonl'),
          'benchmark_sha256':fingerprint(args.benchmark),
          'model_sha256':fingerprint(args.model/'onnx/model_quantized.onnx'),
          'tokenizer_sha256':fingerprint(args.model/'tokenizer.json'),
          'model_download':json.loads((args.model/'download.json').read_text())}
    (run/'source').mkdir()
    for p in (ROOT/'src').glob('*.py'):
        (run/'source'/p.name).write_bytes(p.read_bytes())
    meta['source_sha256']={str(p.relative_to(ROOT)):fingerprint(p) for p in (ROOT/'src').glob('*.py')}
    dump(run/'manifest.json',meta)
    with (run/'run.log').open('w',encoding='utf-8',buffering=1) as f:
        def log(message):
            s=f'{datetime.now(timezone.utc).isoformat()} {message}'
            print(s,flush=True); f.write(s+'\n')
        try:
            with threadpool_limits(limits=1):
                execute(args,run,meta,log)
            log(f'Complete: {run / "REPORT.md"}')
        except BaseException:
            meta['status']='failed'; log(traceback.format_exc()); raise
        finally:
            meta['finished_utc']=datetime.now(timezone.utc).isoformat()
            dump(run/'manifest.json',meta)


if __name__=='__main__':
    main()
