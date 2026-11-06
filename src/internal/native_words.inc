/* Whole-corpus word catalog and document views over the existing CSC postings.
 * doc_posts stores only posting positions (uint32), never copied TF-IDF values.
 * Catalog IDs: bytewise UTF-8 canonical key order, one ID per unique key.
 */
typedef struct {
  const unsigned char *key;
  uint32_t length, row;
} CatalogEntry;
static int catalog_order(const void *aa, const void *bb) {
  const CatalogEntry *a = aa, *b = bb;
  size_t n = a->length < b->length ? a->length : b->length;
  int c = memcmp(a->key, b->key, n);
  if (c)
    return c;
  if (a->length != b->length)
    return (a->length > b->length) - (a->length < b->length);
  return (a->row > b->row) - (a->row < b->row);
}
static void native_catalog_free(MalmoiNative *m) {
  size_t i;
  for (i = 0; i < m->word_count; i++) {
    free(m->words[i].key);
    free(m->words[i].word);
  }
  free(m->words);
  free(m->word_rows);
  free(m->row_words);
  free(m->doc_ptr);
  free(m->doc_posts);
  free(m->doc_inv);
  free(m->bm_doc_ptr);
  free(m->bm_doc_posts);
  free(m->bm_self);
  for (i = 0; i < 4; i++)
    free(m->similar_cache[i].scores);
  free(m->row_reference);
  free(m->word_meaning);
}
/* 뜻마다 참조형인지 표시하고, 단어마다 참조형이 아닌 유효한 뜻의 수를 세요.
 * 뜻 하나의 저장 순서: 고정 24바이트, 키, 표제어, 뜻풀이, 어종. */
static int native_sense_flags_build(MalmoiNative *m) {
  uint64_t i;
  m->row_reference = calloc((size_t)m->ndocs ? (size_t)m->ndocs : 1, 1);
  m->word_meaning = calloc(m->word_count ? m->word_count : 1, sizeof(*m->word_meaning));
  if (!m->row_reference || !m->word_meaning)
    return MALMOI_MEMORY;
  for (i = 0; i < m->ndocs; i++) {
    const unsigned char *p = m->blob + get64(m->offsets + 8 * i);
    uint32_t key = get32(p + 8), word = get32(p + 12), definition = get32(p + 16);
    m->row_reference[i] = (unsigned char)malmoi_sense_is_reference(
        p + 24 + key, word, p + 24 + key + word, definition);
    if (!m->row_reference[i] && (m->doc_inv[i] > 0 || m->bm_self[i] > 0))
      m->word_meaning[m->row_words[i]]++;
  }
  return MALMOI_OK;
}
static int native_catalog_build(MalmoiNative *m) {
  CatalogEntry *e = NULL;
  uint64_t *cursor = NULL;
  uint64_t i, t, z;
  int rc = MALMOI_MEMORY;
  if (m->chr.nnz > UINT32_MAX || m->bm.nnz > UINT32_MAX)
    return MALMOI_LIMIT;
  e = malloc((size_t)m->ndocs * sizeof(*e));
  m->word_rows = malloc((size_t)m->ndocs * sizeof(*m->word_rows));
  m->row_words = malloc((size_t)m->ndocs * sizeof(*m->row_words));
  m->words = calloc((size_t)m->ndocs, sizeof(*m->words));
  m->doc_ptr = calloc((size_t)m->ndocs + 1, sizeof(*m->doc_ptr));
  m->doc_inv = calloc((size_t)m->ndocs, sizeof(*m->doc_inv));
  if (!e || !m->word_rows || !m->row_words || !m->words || !m->doc_ptr ||
      !m->doc_inv)
    goto done;
  for (i = 0; i < m->ndocs; i++) {
    uint64_t a = get64(m->offsets + 8 * i), b = get64(m->offsets + 8 * (i + 1)),
             total = 24;
    const unsigned char *p = m->blob + a;
    uint32_t n[4];
    unsigned j;
    for (j = 0; j < 4; j++) {
      n[j] = get32(p + 8 + 4 * j);
      total += n[j];
    }
    if (total != b - a || !n[0] || memchr(p + 24, 0, n[0] + n[1])) {
      rc = MALMOI_FORMAT;
      goto done;
    }
    e[i].key = p + 24;
    e[i].length = n[0];
    e[i].row = (uint32_t)i;
  }
  qsort(e, (size_t)m->ndocs, sizeof(*e), catalog_order);
  for (i = 0; i < m->ndocs; i++) {
    NativeWord *w;
    const unsigned char *p = m->blob + get64(m->offsets + 8 * e[i].row);
    if (!i || e[i].length != e[i - 1].length ||
        memcmp(e[i].key, e[i - 1].key, e[i].length)) {
      w = &m->words[m->word_count++];
      w->start = (uint32_t)i;
      w->key = copy_n(e[i].key, e[i].length);
      w->word = copy_n(p + 24 + e[i].length, get32(p + 12));
      if (!w->key || !w->word)
        goto done;
    }
    w = &m->words[m->word_count - 1];
    w->count++;
    m->word_rows[i] = e[i].row;
    m->row_words[e[i].row] = (uint32_t)(m->word_count - 1);
  }
  for (t = 0; t < m->chr.nterms; t++)
    for (z = get64(m->chr.pointers + 8 * t);
         z < get64(m->chr.pointers + 8 * (t + 1)); z++) {
      uint32_t row = get32(m->chr.documents + 4 * z);
      double v = getfloat(m->chr.values + 4 * z);
      if (row >= m->ndocs || !isfinite(v) || v < 0) {
        rc = MALMOI_FORMAT;
        goto done;
      }
      m->doc_ptr[row + 1]++;
      m->doc_inv[row] += v * v;
    }
  for (i = 0; i < m->ndocs; i++) {
    m->doc_ptr[i + 1] += m->doc_ptr[i];
    if (m->doc_inv[i] > 0) {
      m->doc_inv[i] = 1 / sqrt(m->doc_inv[i]);
      m->words[m->row_words[i]].has_vector = 1;
    }
  }
  m->doc_posts = malloc((size_t)m->chr.nnz * sizeof(*m->doc_posts));
  cursor = malloc((size_t)m->ndocs * sizeof(*cursor));
  if (!m->doc_posts || !cursor)
    goto done;
  memcpy(cursor, m->doc_ptr, (size_t)m->ndocs * sizeof(*cursor));
  for (z = 0; z < m->chr.nnz; z++) {
    uint32_t row = get32(m->chr.documents + 4 * z);
    m->doc_posts[cursor[row]++] = (uint32_t)z;
  }
  m->bm_doc_ptr = calloc((size_t)m->ndocs + 1, sizeof(*m->bm_doc_ptr));
  m->bm_self = calloc((size_t)m->ndocs, sizeof(*m->bm_self));
  m->bm_doc_posts = malloc((size_t)m->bm.nnz * sizeof(*m->bm_doc_posts));
  if (!m->bm_doc_ptr || !m->bm_self || (m->bm.nnz && !m->bm_doc_posts))
    goto done;
  for (z = 0; z < m->bm.nnz; z++) {
    uint32_t row = get32(m->bm.documents + 4 * z);
    double value = getfloat(m->bm.values + 4 * z);
    if (row >= m->ndocs || !isfinite(value) || value < 0) {
      rc = MALMOI_FORMAT;
      goto done;
    }
    m->bm_doc_ptr[row + 1]++;
    m->bm_self[row] += value;
  }
  for (i = 0; i < m->ndocs; i++) {
    m->bm_doc_ptr[i + 1] += m->bm_doc_ptr[i];
    if (m->bm_self[i] > 0)
      m->words[m->row_words[i]].has_vector = 1;
  }
  memcpy(cursor, m->bm_doc_ptr, (size_t)m->ndocs * sizeof(*cursor));
  for (z = 0; z < m->bm.nnz; z++) {
    uint32_t row = get32(m->bm.documents + 4 * z);
    m->bm_doc_posts[cursor[row]++] = (uint32_t)z;
  }
  rc = native_sense_flags_build(m);
  if (rc)
    goto done;
  rc = MALMOI_OK;
done:
  free(e);
  free(cursor);
  return rc;
}
static int native_locate(MalmoiNative *m, const char *text, uint32_t *out) {
  char *key;
  size_t i, j = 0, lo = 0, hi = m->word_count, n;
  int32_t count;
  UChar32 *valid;
  if (!text || !*text)
    return MALMOI_ARGUMENT;
  n = strlen(text);
  if (n > MAX_QUERY)
    return MALMOI_LIMIT;
  valid = lower_codepoints(text, &count);
  if (!valid)
    return MALMOI_ARGUMENT;
  for (i = 0; i < (size_t)count; i++)
    if (valid[i] != 32 && valid[i] != 9 && valid[i] != 10 && valid[i] != 13)
      break;
  free(valid);
  if (i == (size_t)count)
    return MALMOI_ARGUMENT;
  key = malloc(n + 1);
  if (!key)
    return MALMOI_MEMORY;
  for (i = 0; i < n; i++)
    if (text[i] != '-' && text[i] != '^')
      key[j++] = text[i];
  key[j] = 0;
  if (!j) {
    free(key);
    return MALMOI_ARGUMENT;
  }
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (strcmp(m->words[mid].key, key) < 0)
      lo = mid + 1;
    else
      hi = mid;
  }
  i = lo;
  if (i < m->word_count && !strcmp(m->words[i].key, key)) {
    *out = (uint32_t)i;
    free(key);
    return MALMOI_OK;
  }
  free(key);
  return MALMOI_NOT_FOUND;
}
size_t malmoi_native_word_count(const MalmoiNative *m) {
  return m ? m->word_count : 0;
}
int malmoi_native_word_at(const MalmoiNative *m, size_t i, MalmoiWord *out) {
  if (!out)
    return MALMOI_ARGUMENT;
  out->id = UINT32_MAX;
  out->word = out->key = NULL;
  if (!m)
    return MALMOI_ARGUMENT;
  if (i >= m->word_count)
    return MALMOI_RANGE;
  out->id = (uint32_t)i;
  out->word = m->words[i].word;
  out->key = m->words[i].key;
  return MALMOI_OK;
}
int malmoi_native_word_id(MalmoiNative *m, const char *key, uint32_t *out) {
  if (!m || !out)
    return MALMOI_ARGUMENT;
  return native_locate(m, key, out);
}
static uint32_t posting_term(const SparseIndex *x, uint32_t position) {
  uint64_t lo = 0, hi = x->nterms;
  while (lo < hi) {
    uint64_t mid = lo + (hi - lo) / 2;
    if (get64(x->pointers + 8 * (mid + 1)) <= position)
      lo = mid + 1;
    else
      hi = mid;
  }
  return (uint32_t)lo;
}
static double doc_cosine(MalmoiNative *m, uint32_t a, uint32_t b) {
  uint64_t i = m->doc_ptr[a], j = m->doc_ptr[b], ie = m->doc_ptr[a + 1],
           je = m->doc_ptr[b + 1];
  double dot = 0;
  while (i < ie && j < je) {
    uint32_t x = m->doc_posts[i], y = m->doc_posts[j],
             tx = posting_term(&m->chr, x), ty = posting_term(&m->chr, y);
    if (tx < ty)
      i++;
    else if (tx > ty)
      j++;
    else {
      dot += (double)getfloat(m->chr.values + 4 * x) *
             getfloat(m->chr.values + 4 * y);
      i++;
      j++;
    }
  }
  dot *= m->doc_inv[a] * m->doc_inv[b];
  return dot > 1 ? 1 : dot;
}
/* BM25 is directional. Query terms are the source document's distinct Kiwi
 * terms, as in the existing BM25 search. Normalize each direction by the
 * query's self score, clip each ratio to [0,1], then average both directions.
 * Equal TF-IDF/BM25 mixing is an initial baseline, not a fitted parameter.
 * If a common component is unavailable, use the available component alone. */
static int doc_valid(const MalmoiNative *m, uint32_t row) {
  return m->doc_inv[row] > 0 || m->bm_self[row] > 0;
}
/* 비교에 쓸 뜻인가? 유효해야 하고, 단어에 의미 뜻이 있으면 참조형은 빼요.
 * 의미 뜻이 하나도 없는 단어(방언만 있는 단어 등)는 모든 뜻을 써요. */
static int row_usable(const MalmoiNative *m, uint32_t row) {
  if (!doc_valid(m, row))
    return 0;
  return !m->word_meaning[m->row_words[row]] || !m->row_reference[row];
}
/* 두 뜻의 뜻풀이가 글자 하나까지 똑같은가? */
static int same_definition(const MalmoiNative *m, uint32_t a, uint32_t b) {
  const unsigned char *p = m->blob + get64(m->offsets + 8 * a);
  const unsigned char *q = m->blob + get64(m->offsets + 8 * b);
  uint32_t pn = get32(p + 16), qn = get32(q + 16);
  return pn == qn && !memcmp(p + 24 + get32(p + 8) + get32(p + 12),
                             q + 24 + get32(q + 8) + get32(q + 12), pn);
}
static double combine_components(const MalmoiNative *m, uint32_t a, uint32_t b,
                                 double cosine, double forward,
                                 double reverse) {
  return malmoi_hybrid_score(cosine, m->doc_inv[a] > 0 && m->doc_inv[b] > 0,
                             forward, reverse, m->bm_self[a], m->bm_self[b]);
}
static double doc_hybrid(MalmoiNative *m, uint32_t a, uint32_t b) {
  uint64_t i = m->bm_doc_ptr[a], j = m->bm_doc_ptr[b],
           ie = m->bm_doc_ptr[a + 1], je = m->bm_doc_ptr[b + 1];
  double forward = 0, reverse = 0, cosine = 0;
  if (m->doc_inv[a] > 0 && m->doc_inv[b] > 0)
    cosine = doc_cosine(m, a, b);
  while (i < ie && j < je) {
    uint32_t x = m->bm_doc_posts[i], y = m->bm_doc_posts[j],
             tx = posting_term(&m->bm, x), ty = posting_term(&m->bm, y);
    if (tx < ty)
      i++;
    else if (tx > ty)
      j++;
    else {
      forward += getfloat(m->bm.values + 4 * y);
      reverse += getfloat(m->bm.values + 4 * x);
      i++;
      j++;
    }
  }
  return combine_components(m, a, b, cosine, forward, reverse);
}
int malmoi_native_similarity(MalmoiNative *m, const char *a, const char *b,
                             float *out) {
  uint32_t x, y;
  size_t i, j, slot;
  double best = -1, meaning = -1;
  int rc;
  if (!out)
    return MALMOI_ARGUMENT;
  *out = 0;
  if (!m)
    return MALMOI_ARGUMENT;
  rc = native_locate(m, a, &x);
  if (rc)
    return rc;
  rc = native_locate(m, b, &y);
  if (rc)
    return rc;
  if (x > y) {
    uint32_t swap = x;
    x = y;
    y = swap;
  }
  if (pthread_mutex_lock(&m->lock))
    return MALMOI_IO;
  slot = ((uint64_t)x * 2654435761U + y) % 256;
  if (m->pair_cache[slot].valid && m->pair_cache[slot].a == x &&
      m->pair_cache[slot].b == y) {
    *out = m->pair_cache[slot].score;
    pthread_mutex_unlock(&m->lock);
    return MALMOI_OK;
  }
  for (i = m->words[x].start; i < (size_t)m->words[x].start + m->words[x].count;
       i++)
    for (j = m->words[y].start;
         j < (size_t)m->words[y].start + m->words[y].count; j++) {
      uint32_t r = m->word_rows[i], s = m->word_rows[j];
      if (doc_valid(m, r) && doc_valid(m, s)) {
        double v = doc_hybrid(m, r, s);
        if (v > best)
          best = v;
        /* 뜻 기준 점수: 참조형 뜻과 똑같은 뜻풀이 짝을 빼고 구해요. */
        if (x != y && row_usable(m, r) && row_usable(m, s) &&
            !same_definition(m, r, s) && v > meaning)
          meaning = v;
      }
    }
  /* 뺄 짝을 빼고 남는 짝이 있으면 그 점수를 쓰고, 없으면 기존 점수를 써요. */
  if (meaning >= 0)
    best = meaning;
  if (best < 0)
    rc = MALMOI_ZERO_VECTOR;
  else {
    *out = (float)best;
    m->pair_cache[slot].a = x;
    m->pair_cache[slot].b = y;
    m->pair_cache[slot].score = *out;
    m->pair_cache[slot].valid = 1;
    rc = MALMOI_OK;
  }
  pthread_mutex_unlock(&m->lock);
  return rc;
}
static int word_hit_order(const void *aa, const void *bb) {
  const Hit *a = aa, *b = bb;
  if (a->score > b->score)
    return -1;
  if (a->score < b->score)
    return 1;
  return (a->row > b->row) - (a->row < b->row);
}
static int native_similar_selected(MalmoiNative *m, const char *word,
                                   uint32_t selected_row, MalmoiResult *out,
                                   size_t k, size_t *count) {
  uint32_t source;
  size_t slot, i, j, n = 0;
  float *best = NULL;
  double *scratch = NULL, *bm_forward = NULL, *bm_reverse = NULL;
  Hit *hits = NULL;
  int rc, nonzero = 0;
  if (!count)
    return MALMOI_ARGUMENT;
  *count = 0;
  if (!m || (!out && k))
    return MALMOI_ARGUMENT;
  rc = native_locate(m, word, &source);
  if (rc)
    return rc;
  if (pthread_mutex_lock(&m->lock))
    return MALMOI_IO;
  for (i = m->words[source].start;
       i < (size_t)m->words[source].start + m->words[source].count; i++)
    if ((selected_row == UINT32_MAX || m->word_rows[i] == selected_row) &&
        doc_valid(m, m->word_rows[i]))
      nonzero = 1;
  if (!nonzero) {
    rc = MALMOI_ZERO_VECTOR;
    goto end;
  }
  if (!k) {
    rc = MALMOI_OK;
    goto end;
  }
  for (slot = 0; slot < 4; slot++)
    if (m->similar_cache[slot].valid && m->similar_cache[slot].word == source &&
        m->similar_cache[slot].sense_row == selected_row)
      break;
  if (slot == 4) {
    slot = m->similar_next++ % 4;
    best = calloc(m->word_count, sizeof(*best));
    scratch = calloc((size_t)m->ndocs, sizeof(*scratch));
    bm_forward = calloc((size_t)m->ndocs, sizeof(*bm_forward));
    bm_reverse = calloc((size_t)m->ndocs, sizeof(*bm_reverse));
    if (!best || !scratch || !bm_forward || !bm_reverse) {
      free(best);
      rc = MALMOI_MEMORY;
      goto end;
    }
    for (i = m->words[source].start;
         i < (size_t)m->words[source].start + m->words[source].count; i++) {
      uint32_t row = m->word_rows[i];
      uint64_t p;
      if ((selected_row != UINT32_MAX && row != selected_row) ||
          !doc_valid(m, row))
        continue;
      /* 뜻을 고르지 않았으면 원래 단어의 참조형 뜻은 기준으로 쓰지 않아요. */
      if (selected_row == UINT32_MAX && !row_usable(m, row))
        continue;
      memset(scratch, 0, (size_t)m->ndocs * sizeof(*scratch));
      memset(bm_forward, 0, (size_t)m->ndocs * sizeof(*bm_forward));
      memset(bm_reverse, 0, (size_t)m->ndocs * sizeof(*bm_reverse));
      for (p = m->doc_ptr[row]; p < m->doc_ptr[row + 1]; p++) {
        uint32_t position = m->doc_posts[p],
                 term = posting_term(&m->chr, position);
        double v = getfloat(m->chr.values + 4 * position);
        uint64_t z;
        for (z = get64(m->chr.pointers + 8 * term);
             z < get64(m->chr.pointers + 8 * (term + 1)); z++) {
          uint32_t r = get32(m->chr.documents + 4 * z);
          scratch[r] += v * getfloat(m->chr.values + 4 * z);
        }
      }
      for (p = m->bm_doc_ptr[row]; p < m->bm_doc_ptr[row + 1]; p++) {
        uint32_t position = m->bm_doc_posts[p],
                 term = posting_term(&m->bm, position);
        double source_value = getfloat(m->bm.values + 4 * position);
        uint64_t z;
        for (z = get64(m->bm.pointers + 8 * term);
             z < get64(m->bm.pointers + 8 * (term + 1)); z++) {
          uint32_t r = get32(m->bm.documents + 4 * z);
          bm_forward[r] += getfloat(m->bm.values + 4 * z);
          bm_reverse[r] += source_value;
        }
      }
      for (j = 0; j < m->ndocs; j++) {
        double cosine = scratch[j] * m->doc_inv[row] * m->doc_inv[j];
        double score = combine_components(m, row, (uint32_t)j, cosine,
                                          bm_forward[j], bm_reverse[j]);
        uint32_t id = m->row_words[j];
        if (score > 1)
          score = 1;
        /* 후보 단어의 참조형 뜻, 원래 뜻과 뜻풀이가 똑같은 뜻은 빼요.
         * 똑같은 뜻풀이는 점수가 거의 1이라 높은 점수일 때만 비교해요. */
        if (!row_usable(m, (uint32_t)j) ||
            (score > 0.99 && same_definition(m, row, (uint32_t)j)))
          continue;
        if (score > best[id])
          best[id] = (float)score;
      }
    }
    free(m->similar_cache[slot].scores);
    m->similar_cache[slot].scores = best;
    m->similar_cache[slot].word = source;
    m->similar_cache[slot].sense_row = selected_row;
    m->similar_cache[slot].valid = 1;
  }
  best = m->similar_cache[slot].scores;
  hits = malloc(m->word_count * sizeof(*hits));
  if (!hits) {
    rc = MALMOI_MEMORY;
    goto end;
  }
  for (i = 0; i < m->word_count; i++)
    if (i != source && m->words[i].has_vector && m->word_meaning[i]) {
      hits[n].row = (uint32_t)i;
      hits[n++].score = best[i];
    }
  qsort(hits, n, sizeof(*hits), word_hit_order);
  if (n > k)
    n = k;
  for (i = 0; i < n; i++) {
    uint32_t id = hits[i].row;
    out[i].word_id = id;
    out[i].word = m->words[id].word;
    out[i].key = m->words[id].key;
    out[i].score = hits[i].score;
  }
  *count = n;
  rc = MALMOI_OK;
end:
  free(scratch);
  free(bm_forward);
  free(bm_reverse);
  free(hits);
  pthread_mutex_unlock(&m->lock);
  return rc;
}

int malmoi_native_similar(MalmoiNative *m, const char *word, MalmoiResult *out,
                          size_t k, size_t *count) {
  return native_similar_selected(m, word, UINT32_MAX, out, k, count);
}
int malmoi_native_sense_count(MalmoiNative *m, const char *word,
                              size_t *count) {
  uint32_t id;
  int rc;
  if (!count)
    return MALMOI_ARGUMENT;
  *count = 0;
  if (!m)
    return MALMOI_ARGUMENT;
  rc = native_locate(m, word, &id);
  if (rc)
    return rc;
  *count = m->words[id].count;
  return MALMOI_OK;
}
int malmoi_native_sense_at(MalmoiNative *m, const char *word, size_t index,
                           MalmoiSense *out) {
  uint32_t id, row;
  const unsigned char *p;
  uint32_t a, b, n;
  int rc;
  Owned *node;
  if (!out)
    return MALMOI_ARGUMENT;
  memset(out, 0, sizeof(*out));
  out->row = out->word_id = UINT32_MAX;
  if (!m)
    return MALMOI_ARGUMENT;
  rc = native_locate(m, word, &id);
  if (rc)
    return rc;
  if (index >= m->words[id].count)
    return MALMOI_RANGE;
  row = m->word_rows[m->words[id].start + index];
  p = m->blob + get64(m->offsets + 8 * row);
  a = get32(p + 8);
  b = get32(p + 12);
  n = get32(p + 16);
  if (pthread_mutex_lock(&m->lock))
    return MALMOI_IO;
  node = m->by_row[row];
  if (!node) {
    node = calloc(1, sizeof(*node));
    if (!node) {
      pthread_mutex_unlock(&m->lock);
      return MALMOI_MEMORY;
    }
    node->word = copy_n(p + 24, a);
    node->definition = copy_n(p + 24 + a + b, n);
    node->type = copy_n(p + 24 + a + b + n, get32(p + 20));
    if (!node->word || !node->definition || !node->type) {
      free_owned(node);
      pthread_mutex_unlock(&m->lock);
      return MALMOI_MEMORY;
    }
    node->next = m->owned;
    m->owned = node;
    m->by_row[row] = node;
  }
  out->row = row;
  out->word_id = id;
  out->sense_id = (int64_t)get64(p);
  out->word = m->words[id].word;
  out->definition = node->definition;
  pthread_mutex_unlock(&m->lock);
  return MALMOI_OK;
}
static int native_resolve_sense(MalmoiNative *m, const char *word, int64_t sid,
                                uint32_t *row) {
  uint32_t id;
  size_t i;
  int rc = native_locate(m, word, &id);
  if (rc)
    return rc;
  *row = UINT32_MAX;
  for (i = m->words[id].start;
       i < (size_t)m->words[id].start + m->words[id].count; i++) {
    uint32_t r = m->word_rows[i];
    const unsigned char *p = m->blob + get64(m->offsets + 8 * r);
    if ((int64_t)get64(p) == sid) {
      if (*row != UINT32_MAX) {
        const unsigned char *q = m->blob + get64(m->offsets + 8 * (*row));
        uint32_t pa = get32(p + 8) + get32(p + 12),
                 qa = get32(q + 8) + get32(q + 12), pn = get32(p + 16),
                 qn = get32(q + 16);
        if (pn != qn || memcmp(p + 24 + pa, q + 24 + qa, pn))
          return MALMOI_AMBIGUOUS;
      } else
        *row = r;
    }
  }
  return *row == UINT32_MAX ? MALMOI_NOT_FOUND : MALMOI_OK;
}
int malmoi_native_similar_sense(MalmoiNative *m, const char *word, int64_t sid,
                                MalmoiResult *out, size_t k, size_t *count) {
  uint32_t row;
  int rc;
  if (!count)
    return MALMOI_ARGUMENT;
  *count = 0;
  if (!m || (!out && k))
    return MALMOI_ARGUMENT;
  rc = native_resolve_sense(m, word, sid, &row);
  if (rc)
    return rc;
  return native_similar_selected(m, word, row, out, k, count);
}
int malmoi_native_similarity_sense(MalmoiNative *m, const char *word,
                                   int64_t sid, const char *guess, float *out) {
  uint32_t row, id;
  size_t i, slot;
  double best = -1, meaning = -1;
  int rc;
  if (!out)
    return MALMOI_ARGUMENT;
  *out = 0;
  if (!m)
    return MALMOI_ARGUMENT;
  rc = native_resolve_sense(m, word, sid, &row);
  if (rc)
    return rc;
  rc = native_locate(m, guess, &id);
  if (rc)
    return rc;
  if (!doc_valid(m, row))
    return MALMOI_ZERO_VECTOR;
  if (pthread_mutex_lock(&m->lock))
    return MALMOI_IO;
  slot = ((uint64_t)row * 2654435761U + id) % 256;
  if (m->sense_pair_cache[slot].valid && m->sense_pair_cache[slot].row == row &&
      m->sense_pair_cache[slot].guess == id) {
    *out = m->sense_pair_cache[slot].score;
    pthread_mutex_unlock(&m->lock);
    return MALMOI_OK;
  }
  for (i = m->words[id].start;
       i < (size_t)m->words[id].start + m->words[id].count; i++) {
    uint32_t target = m->word_rows[i];
    if (doc_valid(m, target)) {
      double v = doc_hybrid(m, row, target);
      if (v > best)
        best = v;
        if (m->row_words[row] != id && row_usable(m, target) &&
          !same_definition(m, row, target) && v > meaning)
        meaning = v;
    }
  }
  if (meaning >= 0)
    best = meaning;
  if (best < 0)
    rc = MALMOI_ZERO_VECTOR;
  else {
    *out = (float)best;
    m->sense_pair_cache[slot].row = row;
    m->sense_pair_cache[slot].guess = id;
    m->sense_pair_cache[slot].score = *out;
    m->sense_pair_cache[slot].valid = 1;
    rc = MALMOI_OK;
  }
  pthread_mutex_unlock(&m->lock);
  return rc;
}
