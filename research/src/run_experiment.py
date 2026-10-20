import argparse
import csv
import json
import subprocess
import time
from datetime import datetime
from pathlib import Path

import numpy as np

from src.data import load_dictionary, load_benchmark
from src.vectorizers import (
    make_char_vectorizer,
    make_word_vectorizer,
)
from src.reducers import make_svd
from src.search import (
    normalize_sparse,
    normalize_dense,
    sparse_cosine_scores,
    dense_cosine_scores,
    hybrid_scores,
    top_k,
)
from src.evaluate import (
    evaluate_ranked_words,
    ndcg_at_k,
)


DATA_PATH = Path("data/processed/dictionary.jsonl")
BENCHMARK_PATH = Path("data/benchmark.json")
RESULTS_PATH = Path("experiments/results.csv")


def git_commit():
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            text=True,
        ).strip()
    except Exception:
        return "unknown"


def append_result(row):
    RESULTS_PATH.parent.mkdir(parents=True, exist_ok=True)

    exists = RESULTS_PATH.exists()

    fields = [
        "timestamp",
        "git_commit",
        "method",
        "dataset_size",
        "word_type",
        "char_weight",
        "dimensions",
        "features_char",
        "features_word",
        "recall1",
        "recall5",
        "recall10",
        "ndcg10",
        "query_ms",
        "search_ms",
        "total_ms",
        "build_seconds",
        "explained_variance",
    ]

    with RESULTS_PATH.open(
        "a",
        newline="",
        encoding="utf-8",
    ) as f:
        writer = csv.DictWriter(f, fieldnames=fields)

        if not exists:
            writer.writeheader()

        writer.writerow({
            key: row.get(key, "")
            for key in fields
        })


def print_results(
    query,
    expected,
    top,
    scores,
    records,
):
    print()
    print(f'QUERY: "{query}"')
    print(f"EXPECTED: {', '.join(expected)}")

    for rank, idx in enumerate(top, 1):
        row = records[idx]
        marker = "*" if row["word"] in expected else " "

        print(
            f"{marker}{rank:2}. "
            f"{row['word']:<18} "
            f"{scores[idx]:.4f}"
        )
        print(f"     {row['definition']}")


def main():
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--method",
        required=True,
        choices=[
            "char",
            "word",
            "hybrid",
            "char-lsa",
        ],
    )

    parser.add_argument(
        "--word-type",
        default="고유어",
    )

    parser.add_argument(
        "--char-weight",
        type=float,
        default=0.5,
    )

    parser.add_argument(
        "--dimensions",
        type=int,
        default=128,
    )

    parser.add_argument(
        "--top-k",
        type=int,
        default=10,
    )

    args = parser.parse_args()

    print("Loading dictionary...")

    records = load_dictionary(
        DATA_PATH,
        word_type=args.word_type,
    )

    texts = [
        row["definition"]
        for row in records
    ]

    benchmark = load_benchmark(BENCHMARK_PATH)

    print(f"senses: {len(records):,}")
    print(f"method: {args.method}")

    build_start = time.perf_counter()

    char_vectorizer = None
    word_vectorizer = None
    X_char = None
    X_word = None
    svd = None
    X_lsa = None

    features_char = ""
    features_word = ""
    explained_variance = ""

    if args.method in {
        "char",
        "hybrid",
        "char-lsa",
    }:
        print()
        print("Building char TF-IDF...")

        char_vectorizer = make_char_vectorizer()
        X_char = char_vectorizer.fit_transform(texts)

        features_char = X_char.shape[1]

        print(f"char shape: {X_char.shape}")
        print(f"char nnz: {X_char.nnz:,}")

    if args.method in {
        "word",
        "hybrid",
    }:
        print()
        print("Building word TF-IDF...")

        word_vectorizer = make_word_vectorizer()
        X_word = word_vectorizer.fit_transform(texts)

        features_word = X_word.shape[1]

        print(f"word shape: {X_word.shape}")
        print(f"word nnz: {X_word.nnz:,}")

    if args.method == "char":
        X_char = normalize_sparse(X_char)

    elif args.method == "word":
        X_word = normalize_sparse(X_word)

    elif args.method == "hybrid":
        X_char = normalize_sparse(X_char)
        X_word = normalize_sparse(X_word)

    elif args.method == "char-lsa":
        print()
        print(
            f"Running TruncatedSVD("
            f"{args.dimensions})..."
        )

        svd = make_svd(
            dimensions=args.dimensions,
        )

        X_lsa = svd.fit_transform(X_char)

        explained_variance = float(
            svd.explained_variance_ratio_.sum()
        )

        X_lsa = normalize_dense(X_lsa)
        X_lsa = X_lsa.astype(np.float32)

        print(f"LSA shape: {X_lsa.shape}")
        print(
            "explained variance: "
            f"{explained_variance:.4f}"
        )

    build_seconds = (
        time.perf_counter()
        - build_start
    )

    print()
    print(
        f"build time: "
        f"{build_seconds:.2f}s"
    )

    totals = {
        "recall1": 0,
        "recall5": 0,
        "recall10": 0,
    }

    query_times = []
    search_times = []
    ndcg_scores = []

    print()
    print("=" * 70)
    print("BENCHMARK")
    print("=" * 70)

    for case in benchmark:
        query = case["query"]
        relevance = case["relevance"]
        expected = [
            word
            for word, score in relevance.items()
            if score >= 3
        ]

        q0 = time.perf_counter()

        if args.method == "char":
            q_char = char_vectorizer.transform(
                [query]
            )
            q_char = normalize_sparse(q_char)

        elif args.method == "word":
            q_word = word_vectorizer.transform(
                [query]
            )
            q_word = normalize_sparse(q_word)

        elif args.method == "hybrid":
            q_char = char_vectorizer.transform(
                [query]
            )
            q_char = normalize_sparse(q_char)

            q_word = word_vectorizer.transform(
                [query]
            )
            q_word = normalize_sparse(q_word)

        elif args.method == "char-lsa":
            q_char = char_vectorizer.transform(
                [query]
            )

            q_lsa = svd.transform(q_char)
            q_lsa = normalize_dense(q_lsa)

        q1 = time.perf_counter()

        if args.method == "char":
            scores = sparse_cosine_scores(
                X_char,
                q_char,
            )

        elif args.method == "word":
            scores = sparse_cosine_scores(
                X_word,
                q_word,
            )

        elif args.method == "hybrid":
            char_scores = sparse_cosine_scores(
                X_char,
                q_char,
            )

            word_scores = sparse_cosine_scores(
                X_word,
                q_word,
            )

            scores = hybrid_scores(
                char_scores,
                word_scores,
                char_weight=args.char_weight,
            )

        elif args.method == "char-lsa":
            scores = dense_cosine_scores(
                X_lsa,
                q_lsa[0],
            )

        top = top_k(
            scores,
            k=args.top_k,
        )

        q2 = time.perf_counter()

        query_times.append(q1 - q0)
        search_times.append(q2 - q1)

        words = [
            records[idx]["word"]
            for idx in top
        ]
        ndcg10 = ndcg_at_k(
            words,
            relevance,
            k=10,
        )

        ndcg_scores.append(ndcg10)


        metrics = evaluate_ranked_words(
            words,
            expected,
        )

        for key in totals:
            totals[key] += metrics[key]

        print_results(
            query,
            expected,
            top,
            scores,
            records,
        )

    n = len(benchmark)

    recalls = {
        key: totals[key] / n
        for key in totals
    }
    mean_ndcg10 = float(np.mean(ndcg_scores))

    query_ms = (
        np.mean(query_times) * 1000
    )

    search_ms = (
        np.mean(search_times) * 1000
    )

    total_ms = query_ms + search_ms

    print()
    print("=" * 70)
    print("SUMMARY")
    print("=" * 70)

    print(
        f"Recall@1 : "
        f"{recalls['recall1']:.3f}"
    )
    print(
        f"Recall@5 : "
        f"{recalls['recall5']:.3f}"
    )
    print(
        f"Recall@10: "
        f"{recalls['recall10']:.3f}"
    )
    print(
        f"nDCG@10 : "
        f"{mean_ndcg10:.3f}"
    )

    print()
    print(
        f"query transform mean: "
        f"{query_ms:.2f} ms"
    )
    print(
        f"search mean:          "
        f"{search_ms:.2f} ms"
    )
    print(
        f"total query mean:     "
        f"{total_ms:.2f} ms"
    )

    append_result({
        "timestamp": datetime.now().isoformat(
            timespec="seconds"
        ),
        "git_commit": git_commit(),
        "method": args.method,
        "dataset_size": len(records),
        "word_type": args.word_type,
        "char_weight": (
            args.char_weight
            if args.method == "hybrid"
            else ""
        ),
        "dimensions": (
            args.dimensions
            if args.method == "char-lsa"
            else ""
        ),
        "features_char": features_char,
        "features_word": features_word,
        "recall1": recalls["recall1"],
        "recall5": recalls["recall5"],
        "recall10": recalls["recall10"],
        "ndcg10": round(mean_ndcg10, 6),
        "query_ms": round(query_ms, 3),
        "search_ms": round(search_ms, 3),
        "total_ms": round(total_ms, 3),
        "build_seconds": round(
            build_seconds,
            3,
        ),
        "explained_variance": (
            round(
                explained_variance,
                6,
            )
            if explained_variance != ""
            else ""
        ),
    })

    print()
    print(
        f"saved result -> "
        f"{RESULTS_PATH}"
    )



if __name__ == "__main__":
    main()
