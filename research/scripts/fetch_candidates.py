"""Fetch revision-pinned KURE/Qwen ONNX weights and original pooling metadata."""
import hashlib
import json
import urllib.request
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
MODELS={
 'kure-v1-int8':('LaraAI-Labs/KURE-v1-onnx-int8', ['model.onnx','tokenizer.json','config.json']),
 'qwen3-embedding-0.6b-int8':('onnx-community/Qwen3-Embedding-0.6B-ONNX',['onnx/model_int8.onnx','tokenizer.json','config.json']),
}
for name,(repo,files) in MODELS.items():
 out=ROOT/'artifacts/models'/name; out.mkdir(parents=True,exist_ok=True)
 with urllib.request.urlopen('https://huggingface.co/api/models/'+repo,timeout=60) as r: info=json.load(r)
 revision=info['sha']; hashes={}
 old=json.loads((out/'download.json').read_text()) if (out/'download.json').exists() else {}
 for file in files:
  target=out/file; target.parent.mkdir(parents=True,exist_ok=True)
  if not (target.exists() and old.get('revision')==revision):
   print(f'Download {name}/{file} @ {revision}',flush=True)
   with urllib.request.urlopen(f'https://huggingface.co/{repo}/resolve/{revision}/{file}',timeout=180) as r:
    with target.with_suffix(target.suffix+'.partial').open('wb') as f:
     while block:=r.read(1024*1024): f.write(block)
   target.with_suffix(target.suffix+'.partial').replace(target)
  hashes[file]=hashlib.sha256(target.read_bytes()).hexdigest()
 (out/'download.json').write_text(json.dumps({'repo':repo,'revision':revision,'sha256':hashes},indent=2))
 if name.startswith('kure'):
  with urllib.request.urlopen('https://huggingface.co/nlpai-lab/KURE-v1/resolve/8b418a58414668e75532ed045c22d9ca018ae2b2/1_Pooling/config.json') as r:
   (out/'original_pooling.json').write_bytes(r.read())
 print('Ready:',name,flush=True)
