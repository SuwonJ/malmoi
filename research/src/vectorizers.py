import numpy as np
from sklearn.feature_extraction.text import TfidfVectorizer


def make_char_vectorizer(
    ngram_range=(2, 4),
    min_df=2,
    max_features=200_000,
):
    return TfidfVectorizer(
        analyzer="char",
        ngram_range=ngram_range,
        min_df=min_df,
        max_features=max_features,
        sublinear_tf=True,
        dtype=np.float32,
    )


def make_word_vectorizer(
    ngram_range=(1, 2),
    min_df=2,
    max_features=200_000,
):
    return TfidfVectorizer(
        analyzer="word",
        ngram_range=ngram_range,
        min_df=min_df,
        max_features=max_features,
        sublinear_tf=True,
        dtype=np.float32,
    )
