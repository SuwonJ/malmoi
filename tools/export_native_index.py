"""Export the existing whole-corpus lexical caches to a versioned native format.

This is an offline build tool.  It never changes existing dictionaries/caches and
refuses to overwrite its output. The exported index is not a Git artifact.
"""
import argparse
import hashlib
import json
import os
import shutil
import struct
import sys
import tempfile
import time
from pathlib import Path

import joblib
import numpy as np
import scipy.sparse as sp
import kiwipiepy
import kiwipiepy_model

ROOT = Path(os.environ.get('THESAURUS_ROOT', Path(__file__).resolve().parents[2] / 'thesaurus')).resolve()
sys.path.insert(0, str(ROOT))
from src.data import CORPUS_SCOPE, load_dictionary
from src.experiment_suite import canonical, fingerprint
from src.lexical_bm25 import _parameters

MAGIC = b'MLMIDX01'
VERSION = 1
HEADER = struct.Struct('<8sIIQ32s')
ROW = struct.Struct('<qIIII')
INTEGRITY_ROW = struct.Struct('<BBHQ32s')
MODEL_FILES = ('combiningRule.txt', 'cong.mdl', 'default.dict', 'dialect.dict', 'extract.mdl', 'multi.dict', 'nounchr.mdl', 'sj.morph', 'typo.dict')


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(8 * 1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def head(f, section, count, corpus_hash):
    f.write(HEADER.pack(MAGIC, VERSION, section, count, bytes.fromhex(corpus_hash)))


def save_terms(path, vocabulary, corpus_hash, section):
    items = sorted(vocabulary.items(), key=lambda x: x[0].encode('utf-8'))
    assert len(items) == len(set(vocabulary.values()))
    offsets = np.empty(len(items) + 1, dtype='<u8'); offsets[0] = 0
    for i, (term, _) in enumerate(items):
        offsets[i + 1] = offsets[i] + 4 + len(term.encode('utf-8'))
    with path.open('xb') as f:
        head(f, section, len(items), corpus_hash); offsets.tofile(f)
        for term, number in items:
            f.write(struct.pack('<I', number)); f.write(term.encode('utf-8'))


def save_postings(path, matrix, idf, corpus_hash, section):
    matrix = matrix.tocsr(copy=False)
    if matrix.shape[0] != len(idf):
        raise ValueError('인덱스와 IDF 크기 불일치')
    if matrix.shape[1] >= 2**32:
        raise ValueError('문서 수가 형식 한도를 초과합니다')
    with path.open('xb') as f:
        head(f, section, matrix.shape[0], corpus_hash)
        f.write(struct.pack('<QQ', matrix.shape[1], matrix.nnz))
        np.asarray(matrix.indptr, dtype='<u8').tofile(f)
        np.asarray(matrix.indices, dtype='<u4').tofile(f)
        np.asarray(matrix.data, dtype='<f4').tofile(f)
        np.asarray(np.diff(matrix.indptr), dtype='<u4').tofile(f)
        np.asarray(idf, dtype='<f4').tofile(f)


def save_senses(path, rows, corpus_hash):
    offsets = np.zeros(len(rows) + 1, dtype='<u8')
    with path.open('xb') as f:
        head(f, 5, len(rows), corpus_hash)
        f.seek(HEADER.size + offsets.nbytes)
        start = f.tell()
        for i, r in enumerate(rows):
            sense_id = r.get('id')
            if type(sense_id) is not int or not -(2**63) <= sense_id < 2**63:
                raise ValueError(f'지원하지 않는 뜻풀이 ID: row {i}')
            texts = [canonical(r['word']), r['word'], r['definition'], r.get('word_type') or '']
            encoded = [t.encode('utf-8') for t in texts]
            f.write(ROW.pack(sense_id, *(len(x) for x in encoded)))
            for item in encoded: f.write(item)
            offsets[i + 1] = f.tell() - start
        f.seek(HEADER.size); offsets.tofile(f)


def export(output):
    start = time.perf_counter()
    data = ROOT / 'data/processed/dictionary.jsonl'; data_hash = fingerprint(data)
    rows = load_dictionary(data)
    char_key = data_hash[:16] + '-' + fingerprint(ROOT / 'src/vectorizers.py')[:12] + '-' + CORPUS_SCOPE
    char_file = ROOT / 'artifacts/playground' / ('char-' + char_key + '.joblib')
    if not char_file.is_file(): raise ValueError('전체 어휘 TF-IDF 캐시가 없습니다')
    expected = _parameters(data_hash, len(rows), kiwipiepy.__version__)
    bm_dir = None
    for path in sorted((ROOT / 'artifacts/kiwi-bm25').glob('*/manifest.json')):
        meta = json.loads(path.read_text())
        if all(meta.get(k) == v for k, v in expected.items()): bm_dir = path.parent; break
    if bm_dir is None: raise ValueError('전체 어휘 BM25 캐시가 없습니다')
    if output.exists(): raise FileExistsError(f'출력 경로가 이미 존재합니다: {output}')
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix='.native-index-', dir=output.parent))
    try:
        vectorizer, char = joblib.load(char_file)
        stats = joblib.load(bm_dir / 'stats.joblib'); bm = sp.load_npz(bm_dir / 'postings.npz')
        if char.shape != (len(vectorizer.vocabulary_), len(rows)) or bm.shape != (len(stats['vocabulary']), len(rows)):
            raise ValueError('캐시와 사전 행 수가 다릅니다')
        print('문자 사전과 포스팅 내보내기', flush=True)
        save_terms(stage/'char_terms.bin', vectorizer.vocabulary_, data_hash, 1)
        save_postings(stage/'char_postings.bin', char, vectorizer.idf_, data_hash, 2)
        print('BM25 사전과 포스팅 내보내기', flush=True)
        save_terms(stage/'bm25_terms.bin', stats['vocabulary'], data_hash, 3)
        save_postings(stage/'bm25_postings.bin', bm, stats['idf'], data_hash, 4)
        with (stage/'bm25_lengths.bin').open('xb') as f:
            head(f, 6, len(rows), data_hash)
            f.write(struct.pack('<d', stats['mean_doc_length']))
            np.asarray(stats['doc_lengths'], dtype='<f4').tofile(f)
        print('뜻풀이 행 내보내기', flush=True)
        save_senses(stage/'senses.bin', rows, data_hash)
        files = {p.name: {'sha256': digest(p), 'bytes': p.stat().st_size} for p in sorted(stage.iterdir())}
        model_dir = Path(kiwipiepy_model.__file__).resolve().parent
        model_files = {name: {'sha256': digest(model_dir/name), 'bytes': (model_dir/name).stat().st_size} for name in MODEL_FILES}
        with (stage/'integrity.bin').open('xb') as f:
            head(f, 7, len(files)+len(model_files), data_hash)
            for location, group in ((0, files), (1, model_files)):
                for name, info in sorted(group.items()):
                    encoded = name.encode('ascii')
                    f.write(INTEGRITY_ROW.pack(location, len(encoded), 0, info['bytes'], bytes.fromhex(info['sha256'])))
                    f.write(encoded)
        files['integrity.bin'] = {'sha256': digest(stage/'integrity.bin'), 'bytes': (stage/'integrity.bin').stat().st_size}
        manifest = {'format':'malmoi-native-lexical','version':VERSION,'byte_order':'little',
                    'corpus_scope':CORPUS_SCOPE,'data_sha256':data_hash,'sense_rows':len(rows),
                    'char_features':char.shape[0],'char_nnz':char.nnz,
                    'bm25_features':bm.shape[0],'bm25_nnz':bm.nnz,
                    'kiwi_version':kiwipiepy.__version__,'kiwi_model_files':model_files,'bm25_k1':1.2,'bm25_b':.75,
                    'char_ngram_range':[2,4],'char_sublinear_tf':True,'char_norm':'l2',
                    'ranking':{'top_per_model':100,'decay_tau':10},'files':files}
        (stage/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
        os.rename(stage,output)
        print(f'완료: {len(rows):,}행, {time.perf_counter()-start:.1f}초, {output}',flush=True)
        return manifest
    except BaseException:
        shutil.rmtree(stage, ignore_errors=True)
        raise


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    export(args.output.resolve())

if __name__ == '__main__': main()
