import json
from pathlib import Path


def load_dictionary(
    path: str | Path,
    word_type: str | None = None,
):
    path = Path(path)
    records = []

    with path.open(encoding="utf-8") as f:
        for line in f:
            row = json.loads(line)

            if word_type is not None:
                if row.get("word_type") != word_type:
                    continue

            if not row.get("word"):
                continue

            if not row.get("definition"):
                continue

            records.append(row)

    return records


def load_benchmark(path: str | Path):
    with Path(path).open(encoding="utf-8") as f:
        return json.load(f)
