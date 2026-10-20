"""Reproducible, local reverse-dictionary experiment suite."""
import argparse
import csv
import hashlib
import importlib.metadata
import json
import platform
import subprocess
import sys
import time
import traceback
import unicodedata
from datetime import datetime, timezone
from pathlib import Path

import numpy as np
from scipy import sparse
from sklearn.feature_extraction.text import CountVectorizer
from threadpoolctl import threadpool_limits

from src.data import load_benchmark, load_dictionary
from src.evaluate import ndcg_at_k
from src.reducers import make_svd
from src.search import normalize_dense
from src.vectorizers import make_char_vectorizer, make_word_vectorizer

ROOT = Path(__file__).resolve().parents[1]


def canonical(word):
    return unicodedata.normalize('NFC', word).replace('-', '').replace('^', '')


def ranked_unique(scores, words, k=10):
    # Deterministic ties, maximum sense score per word, no arbitrary zero hits.
    candidates = np.flatnonzero(scores > 0)
    order = candidates[np.lexsort((candidates, -scores[candidates]))]
    seen, result = set(), []
    for i in order:
        if words[i] not in seen:
            seen.add(words[i])
            result.append(int(i))
            if len(result) == k:
                break
    return result


def metrics(words, relevance):
    expected = {w for w, grade in relevance.items() if grade >= 3}
    return {
        **{f'hit{k}': float(bool(set(words[:k]) & expected)) for k in (1, 5, 10)},
        'recall10': len(set(words[:10]) & expected) / len(expected) if expected else 0.,
        'ndcg10': ndcg_at_k(words, relevance, 10),
        'judged10': sum(w in relevance for w in words[:10]) / 10,
    }


def fingerprint(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def dump(path, value):
    tmp = path.with_suffix(path.suffix + '.tmp')
    tmp.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    tmp.replace(path)


def git(*args):
    p = subprocess.run(['git', *args], cwd=ROOT, text=True, capture_output=True)
    return p.stdout.strip() if p.returncode == 0 else 'unavailable'


def array_bytes(x):
    return x.data.nbytes + x.indices.nbytes + x.indptr.nbytes if sparse.issparse(x) else x.nbytes


class Lexical:
    def __init__(self, texts, kind, max_features):
        self.kind = kind
        start = time.perf_counter()
        if kind == 'bm25':
            self.vectorizer = CountVectorizer(ngram_range=(1, 2), min_df=2,
                                             max_features=max_features, dtype=np.float32)
            x = self.vectorizer.fit_transform(texts).tocsr()
            lengths = np.asarray(x.sum(axis=1)).ravel()
            df = np.bincount(x.indices, minlength=x.shape[1])
            idf = np.log1p((x.shape[0] - df + .5) / (df + .5))
            norm = 1.2 * (1 - .75 + .75 * lengths / max(float(lengths.mean()), 1e-9))
            x.data = (x.data * 2.2 / (x.data + np.repeat(norm, np.diff(x.indptr)))
                      * idf[x.indices]).astype(np.float32)
        else:
            maker = make_char_vectorizer if kind == 'char' else make_word_vectorizer
            self.vectorizer = maker(max_features=max_features)
            x = self.vectorizer.fit_transform(texts)
        self.x = x
        # CSR term -> document index visits query posting lists instead of all rows.
        self.index = x.T.tocsr()
        self.build_seconds = time.perf_counter() - start
        self.numeric_bytes = array_bytes(self.index)

    def score(self, query):
        q = self.vectorizer.transform([query])
        if self.kind == 'bm25':
            q.data[:] = 1
        return (q @ self.index).toarray().ravel()


def audit(records, benchmark):
    raw = {r['word'] for r in records}
    normalized = {canonical(w) for w in raw}
    return [{'query': c['query'], 'labels': [
        {'word': w, 'grade': g, 'raw_present': w in raw,
         'normalized_present': canonical(w) in normalized}
        for w, g in c['relevance'].items()]} for c in benchmark]


def report(run, rows, metadata):
    ordered = sorted(rows, key=lambda r: (-r['ndcg10'], r['warm_p95_ms']))
    lines = ['# 한국어 역방향 사전 실험', '',
             f"실행: `{run.name}` · 문항 {metadata['queries']}개 · 뜻풀이 {metadata['senses']:,}개",
             '', '순위는 현재 소규모 개발셋의 nDCG@10 기준이며 일반화 성능을 뜻하지 않습니다.',
             '모든 점수는 표제어의 `-`, `^` 제거 후 단어별 최고 뜻풀이 점수로 평가합니다.', '',
             '| 메소드 | nDCG@10 | Hit@1 | Hit@5 | Hit@10 | Recall@10 | warm p50/p95 ms | cold 평균 ms | 숫자 인덱스 MiB | 빌드 s |',
             '|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|']
    for r in ordered:
        lines.append(f"| {r['method']} | {r['ndcg10']:.4f} | {r['hit1']:.2f} | {r['hit5']:.2f} | {r['hit10']:.2f} | {r['recall10']:.2f} | {r['warm_p50_ms']:.2f}/{r['warm_p95_ms']:.2f} | {r['cold_mean_ms']:.2f} | {r['numeric_index_mib']:.1f} | {r['build_seconds']:.2f} |")
    lines += ['', '## 해석과 다음 결정', '',
              '- Hit@k는 정답 중 하나 이상 검색한 문항 비율, Recall@10은 문항별 정답 회수 비율입니다.',
              '- 정답 등급 3을 Hit/Recall 정답으로 사용하며, nDCG는 등급 1–3을 모두 사용합니다. 미평가 단어는 0점으로 간주합니다.',
              '- cold는 모델이 메모리에 올라온 후 각 문항의 첫 호출입니다. 앱 시작/파일 로드 시간은 포함하지 않습니다.',
              '- warm은 첫 호출 제외 반복 측정입니다. 변환·검색·중복 제거·정렬을 포함하며 출력/파일 기록은 제외합니다.',
              '- 숫자 인덱스 크기는 어휘 사전·문자열·런타임·빌드 메모리를 제외합니다. 모바일 전체 RAM이나 배포 파일 크기가 아닙니다.',
              '- CPU 스레드 1개로 측정한 현재 호스트 결과입니다. 저사양 모바일 5초 조건 달성은 기기 실측이 필요합니다.',
              '- 4문항만으로 파라미터 최적화/최종 모델 선택을 확정하지 마세요. 평가 문항 확장과 블라인드 판정이 우선입니다.',
              '', '## 실행 파일', '',
              '- [전체 진행 로그](run.log)', '- [재현 설정·버전·해시](manifest.json)',
              '- [메소드별 수치 CSV](summary.csv)', '- [문항별 점수·검색 결과](predictions.json)',
              '- [정답 표기 및 사전 포함 여부](benchmark_audit.json)',
              '- [블라인드 후보 평가 CSV](review.csv)', '',
              '## 문항별 상위 5개', '']
    predictions = json.loads((run / 'predictions.json').read_text()) if (run / 'predictions.json').exists() else []
    for p in predictions:
        lines += [f"### {p['method']} · {p['query']}", '',
                  f"nDCG@10={p['metrics']['ndcg10']:.4f} · " + ', '.join(x['word'] for x in p['top'][:5]), '']
    (run / 'REPORT.md').write_text('\n'.join(lines), encoding='utf-8')
    with (run / 'summary.csv').open('w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(ordered)
    # One stable entry point; historic runs remain immutable.
    (run.parent.parent / 'LATEST.md').write_text(
        f'# 최신 실험\n\n[메소드·결과 비교 보고서](runs/{run.name}/REPORT.md)\n\n'
        f'[실행 로그](runs/{run.name}/run.log) · [워크플로우](WORKFLOW.md)\n', encoding='utf-8')


def execute(args, run, metadata, log):
    records = load_dictionary(args.data, None if args.word_type == 'all' else args.word_type)
    benchmark = load_benchmark(args.benchmark)
    if not records or not benchmark:
        raise ValueError('Dictionary and benchmark must be nonempty')
    for case in benchmark:
        if not case.get('query') or not case.get('relevance') or not any(g >= 3 for g in case['relevance'].values()):
            raise ValueError('Every query needs relevance labels including a grade >= 3')
        case['relevance'] = {canonical(w): g for w, g in case['relevance'].items()}
    metadata.update(senses=len(records), queries=len(benchmark))
    dump(run / 'benchmark_audit.json', audit(records, benchmark))
    dump(run / 'benchmark.json', benchmark)
    words = [canonical(r['word']) for r in records]
    texts = [r['definition'] for r in records]
    log(f'Loaded {len(records):,} senses / {len(benchmark)} queries')
    bases, rows, predictions = {}, [], []

    def evaluate(name, score, build_seconds, numeric_bytes):
        log(f'Evaluating {name}')
        times, cold, per_query = [], [], []
        for case in benchmark:
            for iteration in range(args.repeats + 1):
                start = time.perf_counter()
                scores = score(case['query'])
                ids = ranked_unique(scores, words)
                elapsed = (time.perf_counter() - start) * 1000
                (cold if iteration == 0 else times).append(elapsed)
            result = metrics([words[i] for i in ids], case['relevance'])
            per_query.append(result)
            predictions.append({'method': name, 'query': case['query'], 'metrics': result,
                                'top': [{'word': words[i], 'raw_word': records[i]['word'],
                                         'id': records[i].get('id'), 'definition': records[i]['definition'],
                                         'score': float(scores[i])} for i in ids]})
        row = {'method': name, **{k: float(np.mean([m[k] for m in per_query])) for k in per_query[0]},
               'warm_p50_ms': float(np.percentile(times, 50)), 'warm_p95_ms': float(np.percentile(times, 95)),
               'cold_mean_ms': float(np.mean(cold)), 'timing_samples': len(times),
               'numeric_index_mib': numeric_bytes / 1024 ** 2, 'build_seconds': build_seconds}
        rows.append(row)
        dump(run / 'predictions.json', predictions)
        dump(run / 'results.json', rows)
        report(run, rows, metadata)
        log(f"{name}: nDCG@10={row['ndcg10']:.4f}, Hit@10={row['hit10']:.3f}, p95={row['warm_p95_ms']:.2f} ms")

    for kind in ('char', 'word', 'bm25'):
        log(f'Building {kind}')
        base = bases[kind] = Lexical(texts, kind, args.max_features)
        evaluate(kind, base.score, base.build_seconds, base.numeric_bytes)
    char, word, bm25 = (bases[k] for k in ('char', 'word', 'bm25'))
    for weight in (.25, .5, .75):
        evaluate(f'hybrid-char-{weight:g}', lambda q, w=weight: w * char.score(q) + (1-w) * word.score(q),
                 char.build_seconds + word.build_seconds, char.numeric_bytes + word.numeric_bytes)

    def bm25_mix(q):
        c, b = char.score(q), bm25.score(q)
        return .5 * c / max(float(c.max()), 1e-9) + .5 * b / max(float(b.max()), 1e-9)
    evaluate('char-bm25-maxnorm-0.5', bm25_mix,
             char.build_seconds + bm25.build_seconds, char.numeric_bytes + bm25.numeric_bytes)
    for dimensions in args.dimensions:
        log(f'Building char-lsa-{dimensions}')
        start = time.perf_counter()
        svd = make_svd(dimensions)
        dense = normalize_dense(svd.fit_transform(char.x)).astype(np.float32)
        elapsed = time.perf_counter() - start
        log(f'Explained variance: {svd.explained_variance_ratio_.sum():.6f}')
        evaluate(f'char-lsa-{dimensions}',
                 lambda q: dense @ normalize_dense(svd.transform(char.vectorizer.transform([q])))[0],
                 char.build_seconds + elapsed, dense.nbytes + svd.components_.nbytes)
        del dense, svd
    # Pooled method-blind candidates; grades left blank for human annotation.
    pooled = {}
    for p in predictions:
        for item in p['top']:
            key = (p['query'], item['word'])
            pooled.setdefault(key, set()).add(item['definition'])
    with (run / 'review.csv').open('w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        writer.writerow(['query', 'word', 'retrieved_definitions', 'grade_0_to_3', 'notes'])
        for (query, word), definitions in sorted(pooled.items()):
            writer.writerow([query, word, ' | '.join(sorted(definitions)), '', ''])
    metadata['status'] = 'completed'
    report(run, rows, metadata)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', type=Path, default=ROOT / 'data/processed/dictionary.jsonl')
    parser.add_argument('--benchmark', type=Path, default=ROOT / 'data/benchmark.json')
    parser.add_argument('--output', type=Path, default=ROOT / 'experiments')
    parser.add_argument('--word-type', default='고유어')
    parser.add_argument('--repeats', type=int, default=5)
    parser.add_argument('--max-features', type=int, default=200_000)
    parser.add_argument('--dimensions', nargs='*', type=int, default=[128])
    args = parser.parse_args()
    if args.repeats < 1 or args.max_features < 1 or any(d < 1 for d in args.dimensions):
        parser.error('repeats, max-features and dimensions must be positive')
    run = args.output / 'runs' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    run.mkdir(parents=True)
    metadata = {'status': 'running', 'started_utc': datetime.now(timezone.utc).isoformat(),
                'command': sys.argv, 'parameters': {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()},
                'git_commit': git('rev-parse', 'HEAD'), 'git_status': git('status', '--short'),
                'python': sys.version, 'platform': platform.platform(), 'cpu': platform.processor(),
                'threads': 1, 'seed': 42,
                'packages': {p: importlib.metadata.version(p) for p in ('numpy', 'scipy', 'scikit-learn', 'threadpoolctl')},
                'data_sha256': fingerprint(args.data), 'benchmark_sha256': fingerprint(args.benchmark),
                'source_sha256': {str(p.relative_to(ROOT)): fingerprint(p) for p in sorted((ROOT/'src').glob('*.py'))}}
    (run / 'source').mkdir()
    for p in (ROOT / 'src').glob('*.py'):
        (run / 'source' / p.name).write_bytes(p.read_bytes())
    (run / 'working-tree.patch').write_text(git('diff', '--', 'src', 'data/benchmark.json'), encoding='utf-8')
    dump(run / 'manifest.json', metadata)
    with (run / 'run.log').open('w', encoding='utf-8', buffering=1) as logfile:
        def log(message):
            line = f'{datetime.now(timezone.utc).isoformat()} {message}'
            print(line, flush=True)
            logfile.write(line + '\n')
        try:
            with threadpool_limits(limits=1):
                execute(args, run, metadata, log)
            log(f'Complete: {run / "REPORT.md"}')
        except BaseException:
            metadata['status'] = 'failed'
            log(traceback.format_exc())
            raise
        finally:
            metadata['finished_utc'] = datetime.now(timezone.utc).isoformat()
            dump(run / 'manifest.json', metadata)


if __name__ == '__main__':
    main()
