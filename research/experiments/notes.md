# Retrieval experiments

Goal: return useful Korean reverse-dictionary results on a low-end mobile device within 5 seconds.

## Dataset

- Total senses: 1,215,000
- Native Korean word senses: 266,506
- Current experiments use only `word_type == "고유어"`
- Retrieval text: dictionary definition only

## M0-B — raw char TF-IDF

Parameters:

- analyzer: char
- ngram range: 2–4
- max features: 200,000
- min df: 2

Observed:

- Exact Recall@1: 0.00
- Exact Recall@5: 0.00
- Exact Recall@10: 0.00
- Query transform: 0.87 ms
- Search: 61.07 ms
- Total: 61.93 ms

Qualitative result:
- Lexical semantic neighborhood was generally reasonable.
- "어슴푸레하다" appeared at rank 10.
- For "시간을 끄는 행동", close expressions such as "미루적미루적" ranked highly.
- Exact benchmark targets are probably too narrow for judging semantic usefulness alone.

## M0-C — char TF-IDF + LSA-128

Parameters:

- char ngram: 2–4
- TF-IDF max features: 200,000
- SVD dimensions: 128

Observed:

- explained variance: 0.1534
- Exact Recall@1: 0.00
- Exact Recall@5: 0.00
- Exact Recall@10: 0.00
- Query transform: 28.06 ms
- Search: 7.16 ms
- Total: 35.22 ms
- Dense vectors: 130.1 MiB

Qualitative result:
- Retrieval quality degraded substantially compared with raw char TF-IDF.
- Common phrasing such as "조금", "하지 못하고", and "자꾸" dominated results.
- LSA-128 currently appears too lossy.
