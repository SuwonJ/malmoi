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
    groups = result.stdout.strip().split('---')
    for case, group in zip(cases, groups):
        rows = [line.split('\t') for line in group.strip().splitlines()]
        actual = [(int(x[0]), int(x[1])) for x in rows]
        expected = [(x['row'], x['sense_id']) for x in case['top20']]
        if actual != expected:
            raise AssertionError((case['query'], actual, expected))
    if len([g for g in groups if g.strip()]) != len(cases):
        raise AssertionError('Missing query results')
    print(f'PASS: {len(cases)} full-corpus queries; no runtime PATH, model/index variables or Python packages')


if __name__ == '__main__':
    main()
