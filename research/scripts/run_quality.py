"""Run full retrieval + reranking, then write one consolidated quality report."""
import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from src.quality_report import build_report


def launch(module,*arguments):
    runs=ROOT/'experiments/runs'
    before=set(runs.glob('*'))
    subprocess.run([sys.executable,'-m',module,*map(str,arguments)],cwd=ROOT,check=True)
    created=set(runs.glob('*'))-before
    complete=[r for r in created if (r/'manifest.json').exists() and json.loads((r/'manifest.json').read_text())['status']=='completed']
    if len(complete)!=1:
        raise RuntimeError(f'Expected exactly one completed run, found {complete}')
    return complete[0]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reuse-retrieval',type=Path)
    parser.add_argument('--reuse-rerank',type=Path)
    args=parser.parse_args()
    if args.reuse_rerank and not args.reuse_retrieval:
        parser.error('--reuse-rerank requires --reuse-retrieval')
    retrieval=args.reuse_retrieval or launch('src.advanced_experiments')
    rerank=args.reuse_rerank or launch('src.rerank_experiments','--base',retrieval)
    build_report(retrieval.resolve(),rerank.resolve())


if __name__=='__main__':
    main()
