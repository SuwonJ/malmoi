"""Run the real C reader against Python fixtures and malformed byte streams."""
import argparse
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.make_fixture import fixture
from tools.prepare import build, canonical, encode, json_text, read_db, write_db

INSPECT = ROOT / 'build/core/inspect'


class CompatibilityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / 'test.mlm'
        self.rows, self.spaces = fixture(8)
        self.db = build(self.rows, self.spaces, dataset_version='test-v1')
        self.payload = encode(self.db)

    def c_read(self, payload, success=True):
        self.path.write_bytes(payload)
        result = subprocess.run([str(INSPECT), str(self.path)], capture_output=True,
                                encoding='utf-8', errors='strict')
        # A format error must never be a crash or a sanitizer failure.
        self.assertNotIn('Sanitizer', result.stderr)
        if success:
            self.assertEqual(result.returncode, 0, result.stderr)
            data = json.loads(result.stdout)
            data['metadata'] = json.loads(data.pop('metadata_json'))
            return data
        self.assertIn(result.returncode, (2, 3, 10), result.stderr)
        return None

    def test_every_field_and_float_matches_python(self):
        actual = self.c_read(self.payload)
        write_db(self.path, self.db)
        expected = read_db(self.path)
        for key in ('word_dimension', 'sense_dimension', 'metadata', 'word_space', 'sense_space'):
            self.assertEqual(actual[key], expected[key])
        for kind in ('words', 'senses'):
            for a, b in zip(actual[kind], expected[kind], strict=True):
                for key in a:
                    if key != 'vector':
                        self.assertEqual(a[key], b[key])
                for x, y in zip(a['vector'], b['vector'], strict=True):
                    self.assertAlmostEqual(x, y, delta=1e-7)
        self.assertEqual(actual['words'][0]['key'], '먼지잼')
        self.assertNotEqual(actual['words'][2]['id'], actual['words'][3]['id'])
        self.assertEqual(actual['words'][2]['key'], actual['words'][3]['key'])
        self.assertEqual(sum(s['word_id'] == 2 for s in actual['senses']), 2)

    def test_deterministic_ids_hashes_and_output(self):
        shuffled = list(reversed(self.rows))
        again = build(shuffled, self.spaces, dataset_version='test-v1')
        self.assertEqual(encode(again), self.payload)
        manifest = write_db(self.path, self.db)
        self.assertEqual(manifest['file_sha256'], hashlib.sha256(self.payload).hexdigest())
        self.assertEqual(encode(read_db(self.path)), self.payload)

    def test_checked_in_golden_fixture(self):
        path = ROOT / 'tests/fixtures/small.mlm'
        manifest = json.loads(path.with_suffix('.mlm.json').read_text(encoding='utf-8'))
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), manifest['file_sha256'])
        actual = self.c_read(path.read_bytes())
        self.assertEqual(len(actual['words']), 8)
        self.assertEqual(len(actual['senses']), 9)

    def test_thousand_word_dataset_has_identical_platform_hash(self):
        rows, spaces = fixture(1000)
        raw = ''.join(json_text(row)+'\n' for row in rows).encode('utf-8')
        db = build(rows, spaces, dataset_version='synthetic-1000-v1',
                   source_sha256=hashlib.sha256(raw).hexdigest())
        payload = encode(db)
        expected = json.loads((ROOT/'tests/fixtures/dev-1000.expected.json').read_text())
        self.assertEqual(hashlib.sha256(payload).hexdigest(), expected['file_sha256'])
        self.assertEqual(len(payload), expected['bytes'])
        actual = self.c_read(payload)
        self.assertEqual(actual['metadata'], db['metadata'])
        for kind in ('words', 'senses'):
            self.assertEqual(len(actual[kind]), len(db[kind]))
            for a, b in zip(actual[kind], db[kind], strict=True):
                self.assertEqual({k:v for k,v in a.items() if k != 'vector'},
                                 {k:v for k,v in b.items() if k != 'vector'})
                for x,y in zip(a['vector'], b['vector'], strict=True):
                    self.assertAlmostEqual(x,y,delta=1e-7)

    def test_metadata_only_and_empty_database(self):
        rows = [{k: v for k, v in row.items() if not k.endswith('_vector')} for row in self.rows]
        actual = self.c_read(encode(build(rows)))
        self.assertEqual(actual['word_dimension'], 0)
        self.assertEqual(actual['words'][0]['vector'], [])
        self.assertEqual(self.c_read(encode(build([])))['words'], [])

    def test_legacy_jsonl_never_merges_homonyms_without_identity(self):
        rows = [dict(id=1, word='눈', definition='시각 기관'), dict(id=2, word='눈', definition='강수')]
        actual = self.c_read(encode(build(rows)))
        self.assertEqual(len(actual['words']), 2)
        self.assertEqual([s['word_id'] for s in actual['senses']], [0, 1])
        self.assertEqual(canonical('먼지-잼^'), '먼지잼')

    def test_text_escaping_and_different_dimensions(self):
        rows, spaces = copy.deepcopy(self.rows), copy.deepcopy(self.spaces)
        rows[0]['definition'] = '인용 "문자"\t탭\n줄바꿈\\경로 😀'
        spaces['sense']['dimension'] = 3
        for row in rows:
            row['sense_vector'] = [0.25, -0.5, 0.75]
        actual = self.c_read(encode(build(rows, spaces)))
        self.assertEqual(actual['sense_dimension'], 3)
        self.assertIn(rows[0]['definition'], [s['definition'] for s in actual['senses']])

    def test_utf8_file_path(self):
        folder = Path(self.temp.name) / '한글 경로 😀'
        folder.mkdir()
        self.path = folder / '사전.mlm'
        self.assertEqual(len(self.c_read(self.payload)['words']), 8)

    def test_writer_rejects_invalid_vectors_and_ids(self):
        for mutation in ('nan', 'inf', 'large', 'dimension', 'duplicate', 'inconsistent', 'nul', 'zero-l2'):
            with self.subTest(mutation=mutation):
                rows, spaces = copy.deepcopy(self.rows), copy.deepcopy(self.spaces)
                if mutation == 'nan': rows[0]['word_vector'][0] = math.nan
                if mutation == 'inf': rows[0]['sense_vector'][0] = math.inf
                if mutation == 'large': rows[0]['word_vector'][0] = 1e100
                if mutation == 'dimension': rows[0]['word_vector'].pop()
                if mutation == 'duplicate': rows[1]['id'] = rows[0]['id']
                if mutation == 'inconsistent': rows[-1]['word_vector'][0] = .25
                if mutation == 'nul': rows[0]['word'] += '\0'
                if mutation == 'zero-l2': spaces['word']['normalization'] = 'l2'
                with self.assertRaises(ValueError): build(rows, spaces)

    def test_normalized_vectors_checked_in_both_languages(self):
        rows, spaces = fixture(8)
        spaces['word']['normalization'] = 'l2'
        rows[7]['word_vector'][0] = 1
        db = build(rows, spaces)
        self.c_read(encode(db))
        # Flip only the flag in the original, which includes a zero vector.
        bad = bytearray(self.payload)
        struct.pack_into('<I', bad, 32, 1)
        self.c_read(bad, False)

    def test_corruption_rejected_without_crashes(self):
        cases = [b'', self.payload[:39], self.payload + b'extra']
        for offset, value in ((0, b'WRONG!!!'), (8, struct.pack('<I', 2)),
                              (12, struct.pack('<I', 999)), (16, b'\xff'*4),
                              (24, b'\xff'*4), (32, struct.pack('<I', 4)),
                              (36, struct.pack('<I', 1)), (40, b'\xff'*4)):
            bad = bytearray(self.payload); bad[offset:offset+len(value)] = value; cases.append(bad)
        # Corrupt UTF-8 and NUL in the first metadata string.
        for value in (b'\xc0', b'\0'):
            bad = bytearray(self.payload); bad[44:45] = value; cases.append(bad)
        # Truncate at reproducible positions across headers, text and vectors.
        rng = random.Random(42)
        cases.extend(self.payload[:n] for n in rng.sample(range(40, len(self.payload)), 25))
        # Locate and corrupt the first vector and first sense word ID.
        position = 40
        for _ in range(3): position += 4 + struct.unpack_from('<I', self.payload, position)[0]
        for _ in range(3): position += 4 + struct.unpack_from('<I', self.payload, position)[0]
        bad = bytearray(self.payload); struct.pack_into('<f', bad, position, math.nan); cases.append(bad)
        position += 8 * 4
        for _ in range(7):
            for _ in range(3): position += 4 + struct.unpack_from('<I', self.payload, position)[0]
            position += 8 * 4
        bad = bytearray(self.payload); struct.pack_into('<I', bad, position, 999); cases.append(bad)
        for i, payload in enumerate(cases):
            with self.subTest(i=i):
                self.c_read(payload, False)
                self.path.write_bytes(payload)
                with self.assertRaises((ValueError, UnicodeError)): read_db(self.path)

    def test_cli_legacy_input_and_word_limit(self):
        source = Path(self.temp.name) / 'input.jsonl'
        spaces = Path(self.temp.name) / 'spaces.json'
        source.write_text(''.join(json.dumps(row, ensure_ascii=False)+'\n' for row in self.rows), encoding='utf-8')
        spaces.write_text(json.dumps(self.spaces), encoding='utf-8')
        result = subprocess.run([sys.executable, str(ROOT/'tools/prepare.py'), '--input', str(source),
                                 '--spaces', str(spaces), '--output', str(self.path),
                                 '--dataset-version', 'cli-v1', '--limit-words', '3'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        db = read_db(self.path)
        self.assertEqual(len(db['words']), 3)
        self.assertEqual(len(db['senses']), 4)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--inspect', type=Path, default=INSPECT)
    args, rest = parser.parse_known_args()
    INSPECT = args.inspect.resolve()
    if not INSPECT.is_file(): parser.error('Build the C inspect executable first')
    unittest.main(argv=[sys.argv[0], *rest], verbosity=2)
