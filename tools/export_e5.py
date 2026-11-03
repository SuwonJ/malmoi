"""Export a verified local research E5 cache to real C-search data; no inference."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from prepare import build, canonical, json_text, write_db
def fingerprint(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()




def export(dictionary, cache, manifest_path, model, output, limit=10000, include_words=()):
    import numpy as np
    if limit < 1:
        raise ValueError('limit must be positive')
    protected = {Path(dictionary).resolve(), Path(cache).resolve(), Path(cache).with_suffix('.json').resolve(),
                 Path(manifest_path).resolve(), (Path(model)/'download.json').resolve(),
                 (Path(model)/'tokenizer.json').resolve(), (Path(model)/'onnx/model_quantized.onnx').resolve()}
    if Path(output).resolve() in protected or Path(str(output)+'.json').resolve() in protected:
        raise ValueError('Output must not overwrite an input')
    manifest = json.loads(Path(manifest_path).read_text())
    state = json.loads(Path(cache).with_suffix('.json').read_text())
    source_hash = fingerprint(dictionary)
    if (manifest['status'] != 'completed' or manifest['data_sha256'] != source_hash
            or state['data_sha256'] != source_hash or state['completed'] != state['total']
            or fingerprint(cache) != manifest['embedding_sha256']):
        raise ValueError('Dictionary/cache hash or completion status mismatch')
    model_hash = fingerprint(Path(model) / 'onnx/model_quantized.onnx')
    tokenizer_hash = fingerprint(Path(model) / 'tokenizer.json')
    download = json.loads((Path(model) / 'download.json').read_text())
    if (model_hash != manifest['model_sha256'] or model_hash != state['model_sha256']
            or tokenizer_hash != manifest['tokenizer_sha256'] or download != manifest['model_download']):
        raise ValueError('Model provenance mismatch')
    vectors = np.load(cache, mmap_mode='r', allow_pickle=False)
    if (vectors.ndim != 2 or vectors.shape[0] != state['total'] or vectors.dtype != np.dtype('float32')):
        raise ValueError('Cache shape/dtype mismatch')
    lookup, selected, priority = {}, [], []
    include = {canonical(w) for w in include_words}
    eligible = 0
    with Path(dictionary).open(encoding='utf-8') as f:
        for line in f:
            row = json.loads(line)
            if row.get('word_type') != '고유어' or not row.get('word') or not row.get('definition'):
                continue
            definition = row['definition']
            if definition not in lookup:
                lookup[definition] = len(lookup)
            item = (row, lookup[definition])
            if len(selected) < limit:
                selected.append(item)
            if canonical(row['word']) in include:
                priority.append(item)
            eligible += 1
    if len(lookup) != vectors.shape[0] or eligible != manifest['senses']:
        raise ValueError('Definition mapping differs from original experiment')
    if len(priority) > limit:
        raise ValueError('Selected required senses exceed --limit')
    items, seen = [], set()
    for row, index in priority + selected:
        if str(row['id']) in seen:
            continue
        if len(items) >= limit:
            break
        seen.add(str(row['id']))
        # Preserve source senses rather than guessing lexical/homonym identity.
        items.append({**row, 'word_vector': vectors[index].tolist()})
    info = {'model_id': download['repo'], 'revision': download['revision'],
            'dimension': int(vectors.shape[1]), 'dtype': 'float32', 'pooling': 'attention-mask-mean',
            'normalization': 'l2', 'query_prefix': 'query: ', 'document_prefix': 'passage: ',
            'max_length': 128, 'onnx_sha256': model_hash, 'tokenizer_sha256': tokenizer_hash,
            'representation': 'dictionary-definition'}
    db = build(items, {'word': info, 'sense': None}, dataset_version=f'e5-definitions-{len(items)}-v1',
               source_sha256=source_hash)
    # Additional provenance belongs in metadata, not in the vector space identity.
    db['metadata'].update(cache_sha256=manifest['embedding_sha256'], source_senses=eligible,
                          selection='required words followed by original source order',
                          selected_keys=len({w['key'] for w in db['words']}))
    result = write_db(output, db)
    print(json.dumps(result, ensure_ascii=True, indent=2))
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for flag in ['dictionary', 'cache', 'manifest', 'model', 'output']:
        p.add_argument('--'+flag, type=Path, required=True)
    p.add_argument('--limit', type=int, default=10000)
    p.add_argument('--include-word', action='append', default=[])
    args = p.parse_args()
    if args.limit < 1: p.error('--limit must be positive')
    inputs = {args.dictionary.resolve(), args.cache.resolve(), args.manifest.resolve()}
    if args.output.resolve() in inputs or Path(str(args.output)+'.json').resolve() in inputs:
        p.error('Output must not overwrite an input')
    try:
        export(args.dictionary, args.cache, args.manifest, args.model, args.output, args.limit, args.include_word)
    except (OSError, ValueError, KeyError, ImportError) as error:
        p.error(str(error))


if __name__ == '__main__': main()
