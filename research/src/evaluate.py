import math


def hit_at_k(words, expected, k):
    expected = set(expected)
    return bool(expected.intersection(words[:k]))


def evaluate_ranked_words(words, expected):
    return {
        "recall1": int(hit_at_k(words, expected, 1)),
        "recall5": int(hit_at_k(words, expected, 5)),
        "recall10": int(hit_at_k(words, expected, 10)),
    }


def ndcg_at_k(words, relevance, k=10):
    def dcg(rels):
        return sum(
            (2 ** rel - 1) / math.log2(i + 2)
            for i, rel in enumerate(rels)
        )

    observed = [
        relevance.get(word, 0)
        for word in words[:k]
    ]

    ideal = sorted(
        relevance.values(),
        reverse=True,
    )[:k]

    actual_dcg = dcg(observed)
    ideal_dcg = dcg(ideal)

    if ideal_dcg == 0:
        return 0.0

    return actual_dcg / ideal_dcg
