"""Download the pinned-revision BGE reranker ONNX assets; no remote Python code."""
import json
import urllib.request
from pathlib import Path

REPO = 'onnx-community/bge-reranker-v2-m3-ONNX'
OUT = Path(__file__).resolve().parents[1] / 'artifacts/models/bge-reranker-v2-m3'
OUT.mkdir(parents=True, exist_ok=True)
with urllib.request.urlopen(f'https://huggingface.co/api/models/{REPO}', timeout=60) as response:
    info = json.load(response)
revision = info['sha']
for name in ['tokenizer.json', 'tokenizer_config.json', 'config.json', 'onnx/model_quantized.onnx']:
    target = OUT / name
    target.parent.mkdir(parents=True, exist_ok=True)
    print(f'Downloading {name} @ {revision}', flush=True)
    with urllib.request.urlopen(f'https://huggingface.co/{REPO}/resolve/{revision}/{name}', timeout=120) as response:
        with target.with_suffix(target.suffix + '.partial').open('wb') as f:
            while chunk := response.read(1024 * 1024):
                f.write(chunk)
    target.with_suffix(target.suffix + '.partial').replace(target)
(OUT/'download.json').write_text(json.dumps({'repo':REPO,'revision':revision},indent=2))
print('Done:', OUT)
