"""Compare KURE and Qwen on the identical lexical/E5 candidate pool."""
import argparse
import json
import subprocess
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from src.encoder_comparison_report import build


def launch(kind,base):
    runs=ROOT/'experiments/runs';before=set(runs.glob('*'))
    subprocess.run([sys.executable,'-m','src.encoder_screening','--kind',kind,'--base',str(base.resolve())],cwd=ROOT,check=True)
    found=[p for p in set(runs.glob('*'))-before if json.loads((p/'manifest.json').read_text())['status']=='completed']
    if len(found)!=1:raise RuntimeError('Could not identify completed run')
    return found[0]


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--base',type=Path,help='Advanced retrieval run with score_arrays.npz; required for new screenings')
    p.add_argument('--reuse-kure',type=Path);p.add_argument('--reuse-qwen',type=Path)
    a=p.parse_args()
    if not a.base and not (a.reuse_kure and a.reuse_qwen):
        p.error('--base is required unless both --reuse-kure and --reuse-qwen are supplied')
    build((a.reuse_kure or launch('kure',a.base)).resolve(),(a.reuse_qwen or launch('qwen',a.base)).resolve())
