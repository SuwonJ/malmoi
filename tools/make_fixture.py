"""Create synthetic v1 development data; no third-party dictionary or model."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from prepare import atomic_write, build, json_text, write_db


def fixture(count=1000):
    if count < 8:
        raise ValueError('Fixture needs at least 8 words')
    names = ['먼지-잼', '이슬비', '눈', '눈', '서성거리다', '꾸물거리다', '어슴푸레하다', '영벡터']
    definitions = ['먼지를 살짝 적실 만큼 내리는 비.', '작은 물방울로 조용히 내리는 비.',
                   '사물을 바라보는 몸의 기관.', '하늘에서 내려 쌓이는 흰 결정.',
                   '주변을 떠나지 못하고 왔다 갔다 하다.', '일을 곧바로 하지 않고 시간을 끌다.',
                   '빛이나 모습이 희미하게 보이다.', '벡터 예외 처리를 확인하는 단어.']
    rows = []
    for i in range(count):
        word = names[i] if i < len(names) else f'예제단어{i:04d}'
        vector = [0.0] * 8
        vector[i % 7] = 1.0
        if i == 1:
            vector = [0.8, 0.6, 0, 0, 0, 0, 0, 0]
        if i == 7:
            vector = [0.0] * 8
        rows.append({'id': f's{i:04d}', 'lexeme_id': f'w{i:04d}', 'word': word,
                     'definition': definitions[i] if i < len(definitions) else f'개발용 예제 {i:04d}의 설명.',
                     'word_type': '고유어', 'pos': '예제', 'sense_type': '일반어',
                     'word_vector': vector, 'sense_vector': vector[:], 'synthetic': True})
    # Explicit polysemy shares a word ID, while the two 눈 lexemes remain distinct.
    extra = dict(rows[2], id='s-extra', definition='물건의 작은 구멍을 비유하는 예제 뜻풀이.',
                 word_vector=rows[2]['word_vector'][:], sense_vector=rows[2]['sense_vector'][:])
    rows.append(extra)
    descriptor = {'model_id': 'malmoi-synthetic-8', 'revision': 'fixture-v1', 'dimension': 8,
                  'dtype': 'float32', 'pooling': 'fixed-fixture', 'normalization': 'none',
                  'query_prefix': '', 'document_prefix': '', 'synthetic': True}
    spaces = {'word': {**descriptor, 'purpose': 'word-game'},
              'sense': {**descriptor, 'purpose': 'reverse-definition'}}
    return rows, spaces


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--count', type=int, default=1000)
    p.add_argument('--output', type=Path, default=Path('build/fixtures/dev-1000.mlm'))
    p.add_argument('--export-input', action='store_true', help='Also write generated JSONL and space descriptors')
    args = p.parse_args()
    try:
        rows, spaces = fixture(args.count)
        raw = ''.join(json_text(row) + '\n' for row in rows).encode('utf-8')
        db = build(rows, spaces, dataset_version=f'synthetic-{args.count}-v1',
                   source_sha256=hashlib.sha256(raw).hexdigest())
        manifest = write_db(args.output, db)
        if args.export_input:
            atomic_write(str(args.output) + '.jsonl', raw)
            atomic_write(str(args.output) + '.spaces.json', (json_text(spaces) + '\n').encode())
            candidates = {'dataset_version': manifest['dataset_version'],
                          'file_sha256': manifest['file_sha256'], 'word_space': manifest['word_space'],
                          'word_ids': [w['id'] for w in db['words'] if any(w['vector'])]}
            atomic_write(str(args.output) + '.candidates.json', (json_text(candidates) + '\n').encode())
    except (OSError, ValueError) as error:
        p.error(str(error))
    print(f"{len(db['words'])} words / {len(db['senses'])} senses -> {args.output}")
    print(f"sha256={manifest['file_sha256']} (synthetic; not a quality benchmark)")


if __name__ == '__main__':
    main()
