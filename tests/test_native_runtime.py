"""Deployment smoke/parity check; standard library only, no Python engine."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def executable(path):
    p = Path(path).resolve()
    if os.name == 'nt' and not p.suffix:
        p = p.with_suffix('.exe')
    return p


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    parser.add_argument('--unicode', required=True)
    parser.add_argument('--cli', help='Also check backend selection and errors')
    args = parser.parse_args()
    cases = json.loads(Path(__file__).with_name('fixtures').joinpath('native_queries.json').read_text(encoding='utf-8'))
    env = {k: v for k, v in os.environ.items() if not k.startswith('MALMOI_') and not k.startswith('PYTHON')}
    # Children cannot locate Python or other executables through PATH.
    env['PATH'] = str(Path(tempfile.gettempdir()) / 'malmoi-no-executables')
    with tempfile.TemporaryDirectory() as cwd:
        subprocess.run([str(executable(args.unicode))], cwd=cwd, env=env, check=True)
        result = subprocess.run([str(executable(args.binary))], input='\n'.join(x['query'] for x in cases)+'\n',
                                encoding='utf-8', stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                cwd=cwd, env=env, check=True, timeout=180)
    explicit = dict(env, MALMOI_SEARCH_BACKEND='native-index')
    with tempfile.TemporaryDirectory() as cwd:
        again = subprocess.run([str(executable(args.binary))], input='\n'.join(x['query'] for x in cases)+'\n',
                               encoding='utf-8', stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               cwd=cwd, env=explicit, check=True, timeout=180)
    if result.stdout != again.stdout:
        raise AssertionError('Default and explicit native-index results differ')
    if args.cli:
        cli = str(executable(args.cli))
        with tempfile.TemporaryDirectory() as cwd:
            for backend in (None, 'native-index', 'native'):
                selected = dict(env)
                if backend: selected['MALMOI_SEARCH_BACKEND'] = backend
                if backend == 'native': selected['MALMOI_DATA'] = str(Path(__file__).resolve().parent/'fixtures'/'small.mlm')
                status = subprocess.run([cli, '--status'], cwd=cwd, env=selected, capture_output=True, encoding='utf-8', check=True)
                expected = 'lexical' if backend == 'native' else 'native-index'
                if f'reverse_backend={expected}' not in status.stdout or f"similar_backend={'stored-vectors' if backend == 'native' else 'definition-tfidf-bm25'}" not in status.stdout:
                    raise AssertionError(status.stdout)
            bad = Path(cwd)/'bad-index';bad.mkdir()
            (bad/'manifest.json').write_text('{"format": "incompatible"}')
            for path in (Path(cwd)/'missing-index', bad):
                failure = subprocess.run([cli, '--status'], cwd=cwd, env=dict(env, MALMOI_NATIVE_INDEX=str(path)), capture_output=True, encoding='utf-8')
                if not failure.returncode or 'no legacy fallback' not in failure.stderr or str(path) not in failure.stderr:
                    raise AssertionError((failure.returncode, failure.stdout, failure.stderr))
            failure = subprocess.run([cli, '--status'], cwd=cwd, env=dict(env, MALMOI_SEARCH_BACKEND='invalid'), capture_output=True)
            if not failure.returncode: raise AssertionError('Unknown backend accepted')
    groups = result.stdout.strip().split('---')
    for case, group in zip(cases, groups):
        rows = [line.split('\t') for line in group.strip().splitlines()]
        actual = [(int(x[0]), int(x[1])) for x in rows]
        expected = [(x['row'], x['sense_id']) for x in case['top20']]
        for row, item in zip(rows, case['top20']):
            if row[2] != item['word'] or abs(float(row[3])-item['score']) > 1.3e-7 or row[4] != item['definition'] or row[5] != item['word_type']:
                raise AssertionError((case['query'], row, item))
        if actual != expected:
            raise AssertionError((case['query'], actual, expected))
    if len([g for g in groups if g.strip()]) != len(cases):
        raise AssertionError('Missing query results')
    types = {x['word_type'] for c in cases for x in c['top20']}
    if not {'고유어','한자어','외래어','혼종어'} <= types: raise AssertionError(types)
    print(f'PASS: {len(cases)} full-corpus queries; no runtime PATH, model/index variables or Python packages')


if __name__ == '__main__':
    main()
