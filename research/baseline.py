import json
import time
from pathlib import Path

import numpy as np
from sklearn.feature_extraction.text import TfidfVectorizer
from sklearn.decomposition import TruncatedSVD
from sklearn.preprocessing import normalize


DATA = Path("dictionary.jsonl")
BENCH = Path("benchmark.json")

WORD_TYPE = "고유어"

NGRAM_RANGE = (2, 4)
MIN_DF = 2
MAX_FEATURES = 200_000

SVD_DIM = 128
RANDOM_STATE = 42


def load_dictionary():
    records = []

    with DATA.open(encoding="utf-8") as f:
        for line in f:
            row = json.loads(line)

            if row.get("word_type") != WORD_TYPE:
                continue

            definition = row.get("definition")
            word = row.get("word")

            if not definition or not word:
                continue

            records.append(row)

    return records


def load_benchmark():
    with BENCH.open(encoding="utf-8") as f:
        return json.load(f)


print("Loading dictionary...")
records = load_dictionary()

texts = [r["definition"] for r in records]

print(f"senses: {len(records):,}")


# ------------------------------------------------------------
# 1. TF-IDF
# ------------------------------------------------------------

print()
print("Building char n-gram TF-IDF...")

t0 = time.perf_counter()

vectorizer = TfidfVectorizer(
    analyzer="char",
    ngram_range=NGRAM_RANGE,
    min_df=MIN_DF,
    max_features=MAX_FEATURES,
    sublinear_tf=True,
    dtype=np.float32,
)

X = vectorizer.fit_transform(texts)

t1 = time.perf_counter()

print(f"TF-IDF shape: {X.shape}")
print(f"TF-IDF nnz: {X.nnz:,}")
print(f"TF-IDF time: {t1 - t0:.2f}s")

X_norm = normalize(X, norm="l2", axis=1, copy=False)

# ------------------------------------------------------------
# 2. Truncated SVD
# ------------------------------------------------------------

print()
print(f"Running TruncatedSVD({SVD_DIM})...")

svd = TruncatedSVD(
    n_components=SVD_DIM,
    algorithm="randomized",
    random_state=RANDOM_STATE,
)

t2 = time.perf_counter()

X_svd = svd.fit_transform(X)

t3 = time.perf_counter()

print(f"SVD shape: {X_svd.shape}")
print(f"SVD time: {t3 - t2:.2f}s")

explained = svd.explained_variance_ratio_.sum()

print(f"explained variance: {explained:.4f}")


# ------------------------------------------------------------
# 3. Normalize so dot product == cosine similarity
# ------------------------------------------------------------

X_svd = normalize(
    X_svd,
    norm="l2",
    axis=1,
    copy=False,
)

X_svd = X_svd.astype(np.float32)


# ------------------------------------------------------------
# 4. Benchmark
# ------------------------------------------------------------

benchmark = load_benchmark()

hits = {
    1: 0,
    5: 0,
    10: 0,
}

query_times = []
search_times = []


print()
print("=" * 70)
print("BENCHMARK")
print("=" * 70)


for case in benchmark:
    query = case["query"]
    expected = set(case["expected"])

    # query -> TF-IDF
    q0 = time.perf_counter()

    q_sparse = vectorizer.transform([query])
    q_sparse = normalize(q_sparse, norm="l2", axis=1)

    q1 = time.perf_counter()

    scores = (X_norm @ q_sparse.T).toarray().ravel()

    top10 = np.argpartition(scores, -10)[-10:]
    top10 = top10[np.argsort(scores[top10])[::-1]]

    q2 = time.perf_counter()

    query_times.append(q1 - q0)
    search_times.append(q2 - q1)

    words = [records[i]["word"] for i in top10]

    for k in hits:
        if expected.intersection(words[:k]):
            hits[k] += 1

    print()
    print(f'QUERY: "{query}"')
    print(f"EXPECTED: {', '.join(expected)}")

    for rank, idx in enumerate(top10, 1):
        row = records[idx]

        marker = "*" if row["word"] in expected else " "

        print(
            f"{marker}{rank:2}. "
            f"{row['word']:<15} "
            f"{scores[idx]:.4f}"
        )
        print(f"     {row['definition']}")


# ------------------------------------------------------------
# 5. Summary
# ------------------------------------------------------------

n = len(benchmark)

print()
print("=" * 70)
print("SUMMARY")
print("=" * 70)

for k in [1, 5, 10]:
    recall = hits[k] / n
    print(f"Recall@{k:<2}: {recall:.3f} ({hits[k]}/{n})")

print()

print(
    f"query transform mean: "
    f"{np.mean(query_times) * 1000:.2f} ms"
)

print(
    f"search mean:          "
    f"{np.mean(search_times) * 1000:.2f} ms"
)

print(
    f"total query mean:     "
    f"{(np.mean(query_times) + np.mean(search_times)) * 1000:.2f} ms"
)

print()

vector_bytes = X_svd.nbytes

print(
    f"dense vectors: "
    f"{vector_bytes / 1024 / 1024:.1f} MiB"
)

print(
    f"dimensions: {SVD_DIM}"
)
