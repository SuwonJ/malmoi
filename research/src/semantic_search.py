"""Local ONNX E5 encoder and Korean morphology retrieval helpers."""
import numpy as np
from sklearn.feature_extraction.text import CountVectorizer, TfidfVectorizer

CONTENT_TAGS = {'NNG', 'NNP', 'NR', 'VV', 'VA', 'VX', 'MAG', 'MAJ', 'SL', 'SN', 'XR'}


def content_text(tokens):
    # Keep verbs/adjectives/adverbs (including negation); omit particles/endings.
    return ' '.join(t.form for t in tokens if t.tag.split('-')[0] in CONTENT_TAGS)


class MorphIndex:
    def __init__(self, texts, kind):
        self.kind = kind
        cls = CountVectorizer if kind == 'bm25' else TfidfVectorizer
        options = dict(tokenizer=str.split, token_pattern=None, lowercase=False,
                       ngram_range=(1, 2), min_df=1, max_features=200_000, dtype=np.float32)
        if kind != 'bm25':
            options['sublinear_tf'] = True
        self.vectorizer = cls(**options)
        x = self.vectorizer.fit_transform(texts).tocsr()
        if kind == 'bm25':
            lengths = np.asarray(x.sum(axis=1)).ravel()
            df = np.bincount(x.indices, minlength=x.shape[1])
            idf = np.log1p((x.shape[0] - df + .5) / (df + .5))
            norm = 1.2 * (.25 + .75 * lengths / max(float(lengths.mean()), 1e-9))
            x.data = (x.data * 2.2 / (x.data + np.repeat(norm, np.diff(x.indptr))) * idf[x.indices]).astype(np.float32)
        self.index = x.T.tocsr()

    def score_tokens(self, text):
        q = self.vectorizer.transform([text])
        if self.kind == 'bm25':
            q.data[:] = 1
        return (q @ self.index).toarray().ravel()


def rrf(score_arrays, depth=1000, constant=60):
    """Sense-level RRF; positive candidates only, stable ties."""
    out = np.zeros_like(score_arrays[0], dtype=np.float32)
    for scores in score_arrays:
        ids = np.flatnonzero(scores > 0)
        ids = ids[np.lexsort((ids, -scores[ids]))][:depth]
        out[ids] += 1. / (constant + np.arange(1, len(ids) + 1))
    return out


class E5Encoder:
    def __init__(self, model_dir, threads=1, max_length=128):
        import onnxruntime as ort
        from tokenizers import Tokenizer
        self.tokenizer = Tokenizer.from_file(str(model_dir / 'tokenizer.json'))
        self.tokenizer.enable_truncation(max_length=max_length)
        self.tokenizer.enable_padding(pad_id=1, pad_token='<pad>')
        opts = ort.SessionOptions()
        opts.intra_op_num_threads = threads
        opts.inter_op_num_threads = 1
        self.session = ort.InferenceSession(str(model_dir / 'onnx/model_quantized.onnx'),
                                            sess_options=opts, providers=['CPUExecutionProvider'])
        self.input_names = {x.name for x in self.session.get_inputs()}

    def encode(self, texts, prefix, batch_size=16):
        batches = []
        for start in range(0, len(texts), batch_size):
            tokens = self.tokenizer.encode_batch([prefix + t for t in texts[start:start + batch_size]])
            mask = np.asarray([t.attention_mask for t in tokens], dtype=np.int64)
            feed = {'input_ids': np.asarray([t.ids for t in tokens], dtype=np.int64),
                    'attention_mask': mask,
                    'token_type_ids': np.asarray([t.type_ids for t in tokens], dtype=np.int64)}
            hidden = self.session.run(None, {k: v for k, v in feed.items() if k in self.input_names})[0]
            pooled = (hidden * mask[..., None]).sum(axis=1) / mask.sum(axis=1)[:, None]
            pooled /= np.maximum(np.linalg.norm(pooled, axis=1, keepdims=True), 1e-12)
            batches.append(pooled.astype(np.float32))
        return np.concatenate(batches)
