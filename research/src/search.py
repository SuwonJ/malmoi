import numpy as np
from sklearn.preprocessing import normalize


def normalize_sparse(matrix):
    return normalize(
        matrix,
        norm="l2",
        axis=1,
        copy=False,
    )


def normalize_dense(matrix):
    return normalize(
        matrix,
        norm="l2",
        axis=1,
        copy=False,
    )


def sparse_cosine_scores(matrix, query):
    scores = matrix @ query.T
    return scores.toarray().ravel()


def dense_cosine_scores(matrix, query):
    return matrix @ query.ravel()


def hybrid_scores(
    char_scores,
    word_scores,
    char_weight=0.5,
):
    word_weight = 1.0 - char_weight

    return (
        char_weight * char_scores
        + word_weight * word_scores
    )


def top_k(scores, k=10):
    if k >= len(scores):
        return np.argsort(scores)[::-1]

    idx = np.argpartition(scores, -k)[-k:]
    return idx[np.argsort(scores[idx])[::-1]]
