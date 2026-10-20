import argparse
import json
from pathlib import Path
from collections import Counter

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, default=ROOT / 'dictjson')
parser.add_argument('--output', type=Path, default=ROOT / 'data/processed/dictionary.jsonl')
args = parser.parse_args()
SRC, OUT = args.source, args.output
paths = sorted(SRC.glob('*.json'))
if not paths:
    parser.error(f'No JSON files found in {SRC}')
OUT.parent.mkdir(parents=True, exist_ok=True)

word_types = Counter()
pos_counts = Counter()
written = 0

with OUT.open("w", encoding="utf-8") as out:
    for path in paths:
        print(f"reading {path.name}")

        with path.open(encoding="utf-8") as f:
            data = json.load(f)

        for item in data["channel"]["item"]:
            wordinfo = item.get("wordinfo", {})
            senseinfo = item.get("senseinfo", {})

            word = wordinfo.get("word")
            definition = senseinfo.get("definition")
            word_type = wordinfo.get("word_type")
            pos = senseinfo.get("pos")

            word_types[word_type] += 1
            pos_counts[pos] += 1

            if not word or not definition:
                continue

            record = {
                "id": item.get("target_code"),
                "word": word,
                "definition": definition,
                "word_type": word_type,
                "pos": pos,
                "sense_type": senseinfo.get("type"),
            }

            out.write(
                json.dumps(record, ensure_ascii=False) + "\n"
            )
            written += 1

print()
print(f"written: {written:,}")
print(f"word types: {word_types}")
print(f"POS: {pos_counts}")
