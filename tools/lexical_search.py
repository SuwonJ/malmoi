"""Reusable character TF-IDF search with a term-to-document sparse index; no models."""
import argparse
import json
from pathlib import Path
import unicodedata


def canonical(text):
    return unicodedata.normalize('NFC', text).replace('-', '').replace('^', '')


def load_records(path, word_type='고유어'):
    records = []
    with Path(path).open(encoding='utf-8') as stream:
        for line in stream:
            if not line.strip():
                continue
            row = json.loads(line)
            if word_type != 'all' and row.get('word_type') != word_type:
                continue
            if isinstance(row.get('word'), str) and row['word'] and isinstance(row.get('definition'), str) and row['definition']:
                records.append(row)
    if not records:
        raise ValueError('No usable dictionary records after filtering')
    return records


class LexicalSearch:
    """Build once, reuse search(). Same research char TF-IDF and posting-list scoring.

    min_df defaults to 1 for small development dictionaries (research used 2).
    n-grams are 2..4, sublinear TF, float32, L2 normalization.
    """
    def __init__(self, records, min_df=1, max_features=200_000):
        import numpy as np
        from sklearn.feature_extraction.text import TfidfVectorizer
        if min_df < 1 or max_features < 1:
            raise ValueError('min_df and max_features must be positive')
        if not records:
            raise ValueError('No dictionary records')
        self.records = list(records)
        self.np = np
        self.keys = [canonical(row['word']) for row in self.records]
        self.vectorizer = TfidfVectorizer(analyzer='char', ngram_range=(2, 4),
                                         min_df=min_df, max_features=max_features,
                                         sublinear_tf=True, dtype=np.float32)
        matrix = self.vectorizer.fit_transform(
            unicodedata.normalize('NFC', row['definition']) for row in self.records)
        self.index = matrix.T.tocsr()

    def search(self, query, k=10):
        if not isinstance(query, str) or not query.strip():
            raise ValueError('Query must be nonempty text')
        if type(k) is not int or k < 0:
            raise ValueError('k must be a nonnegative integer')
        if not k:
            return []
        q = self.vectorizer.transform([unicodedata.normalize('NFC', query)])
        # Sparse multiplication visits postings for query terms only.
        scores = (q @ self.index).toarray().ravel()
        ids = self.np.flatnonzero(scores > 0)
        ids = ids[self.np.lexsort((ids, -scores[ids]))]
        seen, result = set(), []
        for index in ids:
            index = int(index)
            if self.keys[index] in seen:
                continue
            seen.add(self.keys[index])
            row = self.records[index]
            result.append(dict(word=row['word'], key=self.keys[index],
                               source_id=row.get('id'), definition=row['definition'],
                               score=float(scores[index])))
            if len(result) == k:
                break
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dictionary', type=Path, required=True)
    parser.add_argument('--query', action='append', required=True)
    parser.add_argument('--word-type', default='고유어', help='all disables filtering')
    parser.add_argument('--top-k', type=int, default=10)
    parser.add_argument('--min-df', type=int, default=1)
    parser.add_argument('--max-features', type=int, default=200_000)
    args = parser.parse_args()
    if args.top_k < 1:
        parser.error('--top-k must be positive')
    try:
        search = LexicalSearch(load_records(args.dictionary, args.word_type), args.min_df, args.max_features)
        for query in args.query:
            print(json.dumps(dict(query=query, results=search.search(query, args.top_k)), ensure_ascii=False))
    except ImportError:
        parser.error('Install optional tools dependencies: python -m pip install -r tools/requirements.txt')
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
