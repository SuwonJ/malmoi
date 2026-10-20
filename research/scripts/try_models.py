"""설명이나 상황을 입력해 단어를 찾고 선택한 모델의 결과를 비교합니다."""
import argparse
import hashlib
import importlib.util
import json
import os
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

DESCRIPTIONS = {
    'char': '문자 TF-IDF 전체 검색',
    'e5': 'E5-small 전체 의미 검색 (기존 전체 벡터 필요)',
    'hybrid': '문자 + E5 순위 결합',
    'kure': '문자/E5 후보 → KURE CLS 재평가',
    'qwen': '같은 후보 → Qwen3, 작업 지시 포함',
    'qwen-plain': '같은 후보 → Qwen3, 작업 지시 제외',
}


def ensure_runtime(models):
    """Use the prepared local runtime when the active conda env lacks extras."""
    needed = ['numpy', 'sklearn', 'joblib', 'threadpoolctl']
    if any(name != 'char' for name in models):
        needed += ['onnxruntime', 'tokenizers']
    missing = [name for name in needed if importlib.util.find_spec(name) is None]
    if not missing:
        return
    local_python = ROOT / '.venv/bin/python'
    if local_python.exists() and Path(sys.prefix).resolve() != (ROOT / '.venv').resolve():
        print('준비된 검색 환경으로 실행합니다.', flush=True)
        os.execv(str(local_python), [str(local_python), str(Path(__file__).resolve()), *sys.argv[1:]])
    raise SystemExit('검색 도구가 필요합니다: ' + ', '.join(missing)
                     + '\n프로젝트 루트에서 python -m pip install -e ".[advanced]"를 실행하세요.')


def parser():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='예: .venv/bin/python scripts/try_models.py --models char kure --query "아주 조금 내리는 비"')
    p.add_argument('--models', nargs='+', choices=DESCRIPTIONS, default=['char', 'kure'])
    p.add_argument('--list-models', action='store_true')
    group = p.add_mutually_exclusive_group()
    group.add_argument('--query', action='append', help='질의. 여러 번 지정 가능')
    group.add_argument('--queries-file', type=Path, help='한 줄에 질의 하나인 UTF-8 파일')
    group.add_argument('--benchmark', action='store_true', help='data/benchmark.json으로 비교')
    p.add_argument('--top-k', type=int, default=10)
    p.add_argument('--candidates', type=int, default=200, help='재평가 후보: 문자/E5 각각의 단어 수')
    p.add_argument('--threads', type=int, default=4)
    p.add_argument('--output', type=Path, help='결과 JSON 경로. 생략 시 experiments/manual/에 자동 저장')
    return p


class Playground:
    def __init__(self, args):
        import joblib
        import numpy as np
        from src.data import load_dictionary
        from src.experiment_suite import canonical, fingerprint, Lexical
        self.args = args
        self.np = np
        self.cache = ROOT / 'artifacts/playground'
        self.cache.mkdir(parents=True, exist_ok=True)
        data = ROOT / 'data/processed/dictionary.jsonl'
        self.data_hash = fingerprint(data)
        print('사전/검색 인덱스 준비 중...', flush=True)
        self.records = load_dictionary(data, '고유어')
        self.words = [canonical(r['word']) for r in self.records]
        self.texts = [r['definition'] for r in self.records]
        char_key = self.data_hash[:16] + '-' + fingerprint(ROOT / 'src/vectorizers.py')[:12]
        char_path = self.cache / f'char-{char_key}.joblib'
        if char_path.exists():
            self.char_vectorizer, self.char_index = joblib.load(char_path)
        else:
            model = Lexical(self.texts, 'char', 200_000)
            self.char_vectorizer, self.char_index = model.vectorizer, model.index
            joblib.dump((self.char_vectorizer, self.char_index), char_path)
        self.encoders = {}
        self.document_caches = {}
        self.cache_paths = {}
        self.model_provenance = {}
        self.e5 = None
        if any(m != 'char' for m in args.models):
            from src.semantic_search import E5Encoder
            directory = ROOT / 'artifacts/models/multilingual-e5-small'
            weight = directory / 'onnx/model_quantized.onnx'
            if not weight.exists():
                raise ValueError('E5 모델 없음: .venv/bin/python scripts/fetch_e5.py 실행 필요')
            model_hash = fingerprint(weight)
            match = None
            for meta_path in sorted((ROOT / 'artifacts/advanced').glob('e5-*.json')):
                meta = json.loads(meta_path.read_text())
                if (meta.get('data_sha256') == self.data_hash and meta.get('model_sha256') == model_hash
                        and meta.get('completed') == meta.get('total') and meta_path.with_suffix('.npy').exists()):
                    match = meta_path
                    break
            if match is None:
                raise ValueError('완성된 E5 전체 사전 벡터 없음: .venv/bin/python -m src.advanced_experiments 실행 필요')
            unique = list(dict.fromkeys(self.texts))
            positions = {t: i for i, t in enumerate(unique)}
            self.mapping = np.asarray([positions[t] for t in self.texts], dtype=np.int32)
            self.vectors = np.load(match.with_suffix('.npy'), mmap_mode='r')
            if self.vectors.shape != (len(unique), 384):
                raise ValueError('E5 캐시와 사전 문장 순서/개수 불일치')
            self.e5 = E5Encoder(directory, threads=args.threads)
            self.model_provenance['e5'] = json.loads((directory / 'download.json').read_text())
        print('준비 완료. 첫 재평가는 후보 문서 벡터 생성 때문에 더 걸릴 수 있습니다.', flush=True)

    def encoder(self, model):
        import joblib
        from src.candidate_encoders import CandidateEncoder
        from src.experiment_suite import fingerprint
        kind = 'kure' if model == 'kure' else 'qwen'
        if kind not in self.encoders:
            name = 'kure-v1-int8' if kind == 'kure' else 'qwen3-embedding-0.6b-int8'
            directory = ROOT / 'artifacts/models' / name
            if not (directory / 'download.json').exists():
                raise ValueError(f'{name} 없음: .venv/bin/python scripts/fetch_candidates.py 실행 필요')
            provenance = json.loads((directory / 'download.json').read_text())
            stamp = json.dumps(provenance, sort_keys=True) + fingerprint(ROOT / 'src/candidate_encoders.py')
            key = hashlib.sha256(stamp.encode()).hexdigest()[:16]
            path = self.cache / f'{kind}-documents-{key}.joblib'
            self.encoders[kind] = CandidateEncoder(directory, kind, threads=self.args.threads)
            self.document_caches[kind] = joblib.load(path) if path.exists() else {}
            self.cache_paths[kind] = path
            self.model_provenance[kind] = provenance
        return kind, self.encoders[kind]

    def compare(self, query, relevance=None):
        import joblib
        from src.experiment_suite import ranked_unique, metrics
        from src.semantic_search import rrf
        np = self.np
        start = time.perf_counter()
        char = (self.char_vectorizer.transform([query]) @ self.char_index).toarray().ravel()
        e5 = None
        if self.e5 is not None:
            e5 = (self.vectors @ self.e5.encode([query], 'query: ')[0])[self.mapping]
        retrieval_ms = (time.perf_counter() - start) * 1000
        candidate_ids = None
        results = []
        print(f'\n질의: {query}\n공유 후보 검색: {retrieval_ms:.1f}ms', flush=True)
        for name in dict.fromkeys(self.args.models):
            start = time.perf_counter()
            document_ms = 0.
            missing_count = 0
            if name in ('char', 'e5', 'hybrid'):
                scores = char if name == 'char' else e5 if name == 'e5' else rrf([char, e5])
                ids = ranked_unique(scores, self.words, self.args.top_k)
                ranked = [(i, float(scores[i])) for i in ids]
                scope = 'full-corpus'
            else:
                if candidate_ids is None:
                    candidate_ids = sorted(set(ranked_unique(char, self.words, self.args.candidates))
                                           | set(ranked_unique(e5, self.words, self.args.candidates)))
                kind, encoder = self.encoder(name)
                cache = self.document_caches[kind]
                missing = list(dict.fromkeys(self.texts[i] for i in candidate_ids if self.texts[i] not in cache))
                missing_count = len(missing)
                doc_start = time.perf_counter()
                for offset in range(0, len(missing), 64):
                    batch = missing[offset:offset + 64]
                    print(f'  {name}: 문서 벡터 {min(offset + 64, len(missing))}/{len(missing)}', flush=True)
                    for text, vector in zip(batch, encoder.encode(batch)):
                        cache[text] = vector
                if missing:
                    path = self.cache_paths[kind]
                    tmp = path.with_suffix('.tmp')
                    joblib.dump(cache, tmp)
                    tmp.replace(path)
                document_ms = (time.perf_counter() - doc_start) * 1000
                query_vector = encoder.encode([query], query=(name != 'qwen-plain'))[0]
                matrix = np.stack([cache[self.texts[i]] for i in candidate_ids])
                scores = matrix @ query_vector
                positions = ranked_unique(scores, [self.words[i] for i in candidate_ids], self.args.top_k)
                ranked = [(candidate_ids[p], float(scores[p])) for p in positions]
                scope = 'char-e5-candidates'
            elapsed = (time.perf_counter() - start) * 1000
            row = {'model': name, 'scope': scope, 'model_stage_ms': elapsed,
                   'new_document_vectors': missing_count, 'document_encode_and_save_ms': document_ms,
                   'top': [{'word': self.words[i], 'definition': self.texts[i],
                            'id': self.records[i].get('id'), 'score': score} for i, score in ranked]}
            if relevance is not None:
                row['metrics'] = metrics([self.words[i] for i, _ in ranked], relevance)
            print(f'\n[{name}] {elapsed:.1f}ms / 새 문서 {missing_count}개', flush=True)
            if 'metrics' in row:
                print(f"  nDCG@10={row['metrics']['ndcg10']:.3f}  Hit@10={row['metrics']['hit10']:.0f}")
            for rank, (i, score) in enumerate(ranked, 1):
                print(f'{rank:2}. {self.words[i]} ({score:.4f}) — {self.texts[i]}', flush=True)
            results.append(row)
        return {'query': query, 'shared_retrieval_ms': retrieval_ms, 'results': results}


def main():
    p = parser()
    args = p.parse_args()
    if args.list_models:
        for name, description in DESCRIPTIONS.items():
            print(f'{name:12} {description}')
        return
    if min(args.top_k, args.candidates, args.threads) < 1:
        p.error('top-k, candidates, threads는 1 이상이어야 합니다')
    if args.benchmark and args.top_k < 10:
        p.error('--benchmark에서는 지표 계산을 위해 --top-k 10 이상을 사용하세요')
    ensure_runtime(args.models)
    from src.experiment_suite import dump, canonical
    from threadpoolctl import threadpool_limits
    output = args.output or ROOT / 'experiments/manual' / (datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ') + '.json')
    output.parent.mkdir(parents=True, exist_ok=True)
    record = {'started_utc': datetime.now(timezone.utc).isoformat(), 'models': args.models,
              'candidates_per_retriever': args.candidates, 'top_k': args.top_k,
              'threads': args.threads, 'status': 'running', 'queries': [],
              'timing_note': 'shared_retrieval_ms는 공통 문자/E5 검색. model_stage_ms는 이후 단계이며 모델 로드/문서 캐시 미스 비용도 포함할 수 있음. 모델 간 점수 절댓값 비교 불가.'}
    try:
        with threadpool_limits(limits=1):
            first_query = None
            if not (args.benchmark or args.query or args.queries_file):
                print('찾고 싶은 단어의 뜻이나 상황을 자유롭게 입력하세요. 종료: /quit', flush=True)
                while not first_query:
                    try:
                        first_query = input('\n검색: ').strip()
                    except EOFError:
                        record['status'] = 'completed'
                        return
                    if first_query in ('/quit', '/exit'):
                        record['status'] = 'completed'
                        return
            engine = Playground(args)
            record['data_sha256'] = engine.data_hash
            if args.benchmark:
                cases = json.loads((ROOT / 'data/benchmark.json').read_text())
            elif args.query:
                cases = [{'query': q} for q in args.query if q.strip()]
            elif args.queries_file:
                cases = [{'query': q.strip()} for q in args.queries_file.read_text(encoding='utf-8').splitlines() if q.strip()]
            else:
                cases = None
            while True:
                if cases is None:
                    if first_query is not None:
                        query, first_query = first_query, None
                    else:
                        try:
                            query = input('\n검색 (종료: /quit): ').strip()
                        except EOFError:
                            break
                    if query in ('/quit', '/exit'):
                        break
                    if not query:
                        continue
                    case = {'query': query}
                else:
                    if not cases:
                        break
                    case = cases.pop(0)
                relevance = case.get('relevance')
                if relevance is not None:
                    relevance = {canonical(w): g for w, g in relevance.items()}
                record['queries'].append(engine.compare(case['query'], relevance))
                record['model_provenance'] = engine.model_provenance
                dump(output, record)
            record['status'] = 'completed'
    except KeyboardInterrupt:
        record['status'] = 'interrupted'
        print('\n중단했습니다.')
    except Exception as error:
        record['status'] = 'failed'
        record['error'] = str(error)
        raise
    finally:
        record['finished_utc'] = datetime.now(timezone.utc).isoformat()
        dump(output, record)
        print(f'\n결과 저장: {output}')


if __name__ == '__main__':
    main()
