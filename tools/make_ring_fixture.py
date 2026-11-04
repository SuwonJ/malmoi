"""다리 놓기 테스트용 원형 사전: 12개 단어를 원 위에 30도 간격으로 배치."""
import argparse
import hashlib
import math
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from prepare import build, write_db

WORDS = ["비", "빗물", "시냇물", "강물", "바다", "파도",
         "바람", "구름", "하늘", "햇빛", "무지개", "소나기"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("build/fixtures/ring.mlm"))
    args = parser.parse_args()
    rows = []
    for i, word in enumerate(WORDS):
        angle = 2 * math.pi * i / len(WORDS)
        rows.append({"id": f"s{i:02d}", "lexeme_id": f"w{i:02d}", "word": word,
                     "definition": f"{word}의 테스트용 뜻풀이.", "word_type": "고유어",
                     "pos": "명사", "word_vector": [math.cos(angle), math.sin(angle)]})
    space = {"model_id": "ring-test", "revision": "1", "dimension": 2, "dtype": "float32",
             "pooling": "none", "normalization": "none",
             "query_prefix": "", "document_prefix": "", "synthetic": True}
    db = build(rows, {"word": space, "sense": None}, dataset_version="ring-12-v1",
               source_sha256=hashlib.sha256("|".join(WORDS).encode()).hexdigest())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    write_db(args.output, db)
    print(f"{len(rows)} words -> {args.output}")


if __name__ == "__main__":
    main()