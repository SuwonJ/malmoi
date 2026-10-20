from sklearn.decomposition import TruncatedSVD


def make_svd(
    dimensions=128,
    random_state=42,
):
    return TruncatedSVD(
        n_components=dimensions,
        algorithm="randomized",
        random_state=random_state,
    )
