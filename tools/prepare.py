"""Deterministic MALMOI v1 writer and reference reader (Python stdlib only)."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import tempfile
import unicodedata

MAGIC = b'MALMOI\0\0'
HEADER = struct.Struct('<8s8I')
MAX_FILE_BYTES = 512 * 1024 * 1024
MAX_DIMENSION = 65536


def canonical(word):
    return unicodedata.normalize('NFC', word).replace('-', '').replace('^', '')


def text(value, *, empty=False):
    if not isinstance(value, str) or '\0' in value or (not empty and not value):
        raise ValueError('Expected nonempty UTF-8 text without NUL')
    value.encode('utf-8', errors='strict')
    return value


def json_text(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':'), allow_nan=False)


def space_descriptor(value):
    if value is None:
        return '', 0, False
    if not isinstance(value, dict):
        raise ValueError('Space descriptor must be an object or null')
    required = {'model_id', 'revision', 'dimension', 'pooling', 'normalization',
                'query_prefix', 'document_prefix', 'dtype'}
    if not required <= value.keys():
        raise ValueError(f'Missing space fields: {sorted(required - value.keys())}')
    for key in required - {'dimension'}:
        text(value[key], empty=key in {'query_prefix', 'document_prefix'})
    dim = value['dimension']
    if type(dim) is not int or not 1 <= dim <= MAX_DIMENSION:
        raise ValueError('Space dimension must be 1..65536')
    if value['normalization'] not in {'none', 'l2'} or value['dtype'] != 'float32':
        raise ValueError('Only float32 with normalization none/l2 is supported')
    return json_text(value), dim, value['normalization'] == 'l2'


def float_vector(values, dimension, normalized):
    if not isinstance(values, list) or len(values) != dimension:
        raise ValueError(f'Expected a vector with {dimension} elements')
    if any(type(v) not in (float, int) or not math.isfinite(v) for v in values):
        raise ValueError('Vector contains non-numeric or non-finite values')
    try:
        packed = struct.pack(f'<{dimension}f', *values)
    except (OverflowError, struct.error) as error:
        raise ValueError('Vector is outside float32 range') from error
    rounded = struct.unpack(f'<{dimension}f', packed)
    if any(not math.isfinite(v) for v in rounded):
        raise ValueError('Vector is outside float32 range')
    if dimension and normalized and abs(sum(v*v for v in rounded) - 1.0) > .0002:
        raise ValueError('Declared L2 vector is not unit length (zero is invalid)')
    return list(rounded)


def build(rows, spaces=None, *, dataset_version='local-v1', source_sha256=None):
    """No lexical grouping is inferred from spelling alone.

    Explicit lexeme_id groups senses; otherwise each source sense remains a
    separate word entry. This avoids silently merging homonyms in old JSONL.
    IDs are deterministic for the same selected input, not globally stable.
    """
    rows = list(rows)
    spaces = spaces or {'word': None, 'sense': None}
    if not isinstance(spaces, dict) or set(spaces) != {'word', 'sense'}:
        raise ValueError('Spaces must contain exactly word and sense entries')
    ws, dw, wn = space_descriptor(spaces.get('word'))
    ss, ds, sn = space_descriptor(spaces.get('sense'))
    groups, senses, seen = {}, [], set()
    for row in rows:
        if not isinstance(row, dict):
            raise ValueError('Each input row must be an object')
        source = row.get('id')
        if type(source) not in (str, int):
            raise ValueError('Each sense needs a stable source id')
        source = text(str(source))
        if source in seen:
            raise ValueError(f'Duplicate source id: {source}')
        seen.add(source)
        word, definition = text(row.get('word')), text(row.get('definition'))
        key = text(canonical(word))
        explicit = row.get('lexeme_id')
        if explicit is not None:
            if type(explicit) not in (str, int):
                raise ValueError('lexeme_id must be a string or integer')
            lexeme = 'lexeme:' + text(str(explicit))
        else:
            lexeme = 'sense:' + source
        wv = float_vector(row.get('word_vector', []), dw, wn)
        sv = float_vector(row.get('sense_vector', []), ds, sn)
        group = groups.get(lexeme)
        if group is not None:
            if group['key'] != key or group['vector'] != wv:
                raise ValueError(f'Inconsistent word key/vector for {lexeme}')
            group['word'] = min(group['word'], word)
        else:
            groups[lexeme] = {'lexeme_id': lexeme, 'word': word, 'key': key, 'vector': wv}
        senses.append({'source_id': source, 'lexeme_id': lexeme, 'definition': definition,
                       'pos': text(row.get('pos') or '', empty=True), 'vector': sv})
    words = [groups[k] for k in sorted(groups)]
    ids = {w['lexeme_id']: i for i, w in enumerate(words)}
    for i, word in enumerate(words):
        word['id'] = i
    senses.sort(key=lambda s: s['source_id'])
    for i, sense in enumerate(senses):
        sense['id'] = i
        sense['word_id'] = ids[sense.pop('lexeme_id')]
    if max(len(words), len(senses)) > 0xffffffff:
        raise ValueError('Too many records')
    logical = {'words': words, 'senses': senses, 'word_space': ws, 'sense_space': ss}
    metadata = {'dataset_version': text(dataset_version), 'format_version': 1,
                'prepare_version': 1, 'word_count': len(words), 'sense_count': len(senses),
                'key_policy': 'NFC; remove ASCII - and ^; preserve raw spelling',
                'id_policy': 'sorted lexeme/source keys; IDs local to dataset version',
                'synthetic': any(x.get('synthetic', False) for x in rows),
                'logical_sha256': hashlib.sha256(json_text(logical).encode()).hexdigest(),
                'source_sha256': source_sha256}
    return {**logical, 'metadata': metadata, 'word_dimension': dw, 'sense_dimension': ds,
            'flags': int(wn) | (int(sn) << 1)}


def encode(db):
    chunks = [HEADER.pack(MAGIC, 1, HEADER.size, len(db['words']), len(db['senses']),
                          db['word_dimension'], db['sense_dimension'], db['flags'], 0)]

    def string(value):
        data = text(value, empty=True).encode('utf-8')
        chunks.extend([struct.pack('<I', len(data)), data])

    string(json_text(db['metadata']))
    string(db['word_space'])
    string(db['sense_space'])
    for w in db['words']:
        for key in ('word', 'key', 'lexeme_id'):
            string(w[key])
        chunks.append(struct.pack(f"<{db['word_dimension']}f", *w['vector']))
    for s in db['senses']:
        chunks.append(struct.pack('<I', s['word_id']))
        for key in ('source_id', 'definition', 'pos'):
            string(s[key])
        chunks.append(struct.pack(f"<{db['sense_dimension']}f", *s['vector']))
    if sum(map(len, chunks)) > MAX_FILE_BYTES:
        raise ValueError('v1 files are limited to 512 MiB; split/select the input')
    return b''.join(chunks)


def read_db(path):
    """Independent stdlib reader for cross-language fixture verification."""
    path = Path(path)
    if path.stat().st_size > MAX_FILE_BYTES:
        raise ValueError('File too large')
    data = path.read_bytes()
    if len(data) < HEADER.size:
        raise ValueError('Truncated header')
    magic, version, header, nw, ns, dw, ds, flags, reserved = HEADER.unpack_from(data)
    if (magic != MAGIC or version != 1 or header != HEADER.size or flags & ~3 or reserved
            or max(dw, ds) > MAX_DIMENSION):
        raise ValueError('Invalid header')
    position = HEADER.size

    def u32():
        nonlocal position
        if len(data) - position < 4:
            raise ValueError('Truncated integer')
        value, = struct.unpack_from('<I', data, position)
        position += 4
        return value

    def string():
        nonlocal position
        n = u32()
        if n > len(data) - position:
            raise ValueError('Truncated string')
        result = data[position:position+n].decode('utf-8', errors='strict')
        position += n
        return text(result, empty=True)

    def vector(dim, norm):
        nonlocal position
        if dim * 4 > len(data) - position:
            raise ValueError('Truncated vector')
        result = list(struct.unpack_from(f'<{dim}f', data, position))
        position += dim * 4
        return float_vector(result, dim, norm)

    metadata, ws, ss = string(), string(), string()
    if not metadata or bool(dw) != bool(ws) or bool(ds) != bool(ss) or (not dw and flags & 1) or (not ds and flags & 2):
        raise ValueError('Invalid space descriptor')
    if nw * (12 + 4 * dw) + ns * (16 + 4 * ds) > len(data) - position:
        raise ValueError('Record counts exceed file size')
    words, senses = [], []
    for i in range(nw):
        raw, key, lexeme = string(), string(), string()
        if not all((raw, key, lexeme)):
            raise ValueError('Empty word field')
        words.append({'id': i, 'word': raw, 'key': key, 'lexeme_id': lexeme,
                      'vector': vector(dw, flags & 1)})
    for i in range(ns):
        wid, source, definition, pos = u32(), string(), string(), string()
        if wid >= nw or not source or not definition:
            raise ValueError('Invalid sense')
        senses.append({'id': i, 'word_id': wid, 'source_id': source, 'definition': definition,
                       'pos': pos, 'vector': vector(ds, flags & 2)})
    if position != len(data):
        raise ValueError('Trailing bytes')
    return {'words': words, 'senses': senses, 'word_space': ws, 'sense_space': ss,
            'metadata': json.loads(metadata), 'word_dimension': dw, 'sense_dimension': ds, 'flags': flags}


def atomic_write(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    name = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
            name = stream.name
            stream.write(data)
        os.replace(name, path)
    finally:
        if name and os.path.exists(name):
            os.unlink(name)


def write_db(path, db):
    payload = encode(db)
    atomic_write(path, payload)
    manifest = {**db['metadata'], 'file_sha256': hashlib.sha256(payload).hexdigest(),
                'bytes': len(payload), 'word_space': db['word_space'], 'sense_space': db['sense_space']}
    atomic_write(str(path) + '.json', (json.dumps(manifest, ensure_ascii=False, indent=2) + '\n').encode())
    return manifest


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--input', type=Path, required=True, help='research-compatible JSONL, with optional vector fields')
    p.add_argument('--spaces', type=Path, help='JSON object with word/sense descriptors (null = absent vectors)')
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--dataset-version', required=True)
    p.add_argument('--word-type', default='고유어', help='all disables the word_type filter')
    p.add_argument('--limit-words', type=int, help='Select first N sorted lexeme IDs, including all their senses')
    args = p.parse_args()
    if args.limit_words is not None and args.limit_words < 1:
        p.error('--limit-words must be positive')
    try:
        outputs = {args.output.resolve(), Path(str(args.output) + '.json').resolve()}
        inputs = {args.input.resolve()} | ({args.spaces.resolve()} if args.spaces else set())
        if outputs & inputs:
            raise ValueError('Output must not overwrite the input or space file')
        raw = args.input.read_bytes()
        rows = [json.loads(line) for line in raw.decode('utf-8').splitlines() if line.strip()]
        if not all(isinstance(row, dict) for row in rows):
            raise ValueError('Each input row must be an object')
        rows = [r for r in rows if args.word_type == 'all' or r.get('word_type') == args.word_type]
        spaces = json.loads(args.spaces.read_text(encoding='utf-8')) if args.spaces else None
        db = build(rows, spaces, dataset_version=args.dataset_version,
                   source_sha256=hashlib.sha256(raw).hexdigest())
        if args.limit_words is not None:
            selected = {w['lexeme_id'] for w in db['words'][:args.limit_words]}
            rows = [r for r in rows if ('lexeme:' + str(r['lexeme_id']) if r.get('lexeme_id') is not None
                                       else 'sense:' + str(r['id'])) in selected]
            db = build(rows, spaces, dataset_version=args.dataset_version,
                       source_sha256=hashlib.sha256(raw).hexdigest())
        manifest = write_db(args.output, db)
    except (OSError, ValueError, TypeError, UnicodeError) as error:
        p.error(str(error))
    print(json.dumps(manifest, ensure_ascii=True, indent=2))


if __name__ == '__main__':
    main()
