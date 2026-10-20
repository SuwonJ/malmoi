"""Fixed equal-weight rank fusion of actual reranker candidates and retrieval order."""
import json
import numpy as np
from src.experiment_suite import ROOT, dump, fingerprint, metrics


def analyze(retrieval, rerank):
    cases=json.loads((retrieval/'benchmark.json').read_text())
    relevance={c['query']:c['relevance'] for c in cases}
    predictions=json.loads((rerank/'predictions.json').read_text())
    timing={r['method']:r['estimated_total_ms'] for r in json.loads((rerank/'results.json').read_text())}
    # Candidate lists preserve original retrieval order; no labels enter scoring.
    sources=['e5+char-rrf-top100-bge-def','e5+char-rrf-top100-bge-word-def']
    rows, outputs=[],[]
    for source in sources:
        per_query=[]
        for p in predictions:
            if p['method']!=source:
                continue
            candidates=p['candidates']
            logits=np.asarray([c['rerank_logit'] for c in candidates])
            order=np.lexsort((np.arange(len(logits)),-logits))
            ce_rank=np.empty(len(order),dtype=int); ce_rank[order]=np.arange(1,len(order)+1)
            prior_rank=np.arange(1,len(order)+1)
            scores=1/(60+prior_rank)+1/(60+ce_rank)
            final=np.lexsort((np.arange(len(scores)),-scores))[:10]
            top=[candidates[i] for i in final]
            m=metrics([c['word'] for c in top],relevance[p['query']]); per_query.append(m)
            outputs.append({'method':source+'-prior-rrf','query':p['query'],'metrics':m,'top':top})
        rows.append({'method':source+'-prior-rrf',**{k:float(np.mean([m[k] for m in per_query])) for k in per_query[0]},
                     'latency_ms':timing[source], 'timing':'composed-excludes-small-fusion-cost',
                     'report':'fusion_analysis.json'})
    dump(ROOT/'experiments/fusion_analysis.json',{
        'status':'completed','retrieval_run':str(retrieval.relative_to(ROOT)),
        'rerank_run':str(rerank.relative_to(ROOT)),
        'source_sha256':fingerprint(ROOT/'src/fusion_analysis.py'),
        'predictions_sha256':fingerprint(rerank/'predictions.json'),
        'constant':60,'weights':[1,1],'selection':'posthoc development-set diagnostic; not held-out validation',
        'results':rows,'predictions':outputs})
    return rows,outputs
