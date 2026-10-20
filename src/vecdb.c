#include "vecdb.h"
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#define MAX_FILE_BYTES (512UL * 1024UL * 1024UL)
#define MAX_DIMENSION 65536U

struct VecDB {
    size_t nw, ns, dw, ds;
    unsigned flags;
    char *metadata, *word_space, *sense_space;
    VecDBWord *words;
    VecDBSense *senses;
    float *wv, *sv;
};
typedef struct { const unsigned char *data; size_t size, pos; } Reader;

static int utf8(const unsigned char *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        uint32_t cp;
        unsigned need, j;
        unsigned char c = s[i++];
        if (c == 0) return 0;
        if (c < 0x80) continue;
        if (c >= 0xc2 && c <= 0xdf) { cp = c & 0x1fU; need = 1; }
        else if (c >= 0xe0 && c <= 0xef) { cp = c & 0x0fU; need = 2; }
        else if (c >= 0xf0 && c <= 0xf4) { cp = c & 0x07U; need = 3; }
        else return 0;
        if (need > n - i) return 0;
        for (j = 0; j < need; ++j) {
            c = s[i++];
            if ((c & 0xc0U) != 0x80U) return 0;
            cp = (cp << 6) | (c & 0x3fU);
        }
        if ((need == 1 && cp < 0x80) || (need == 2 && cp < 0x800) ||
            (need == 3 && cp < 0x10000) || cp > 0x10ffff ||
            (cp >= 0xd800 && cp <= 0xdfff)) return 0;
    }
    return 1;
}

static int u32(Reader *r, uint32_t *out) {
    const unsigned char *p;
    if (r->size - r->pos < 4) return VECDB_FORMAT;
    p = r->data + r->pos; r->pos += 4;
    *out = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return VECDB_OK;
}

static int string(Reader *r, const char **out) {
    uint32_t n;
    char *s;
    *out = NULL;
    if (u32(r, &n) || n > r->size - r->pos ||
        !utf8(r->data + r->pos, n)) return VECDB_FORMAT;
    s = (char *)malloc((size_t)n + 1);
    if (!s) return VECDB_MEMORY;
    memcpy(s, r->data + r->pos, n); s[n] = '\0'; r->pos += n;
    *out = s;
    return VECDB_OK;
}

static int owned_string(Reader *r, char **out) {
    const char *s = NULL;
    int rc = string(r, &s);
    *out = (char *)s;
    return rc;
}

static int vector(Reader *r, float *out, size_t dim, int normalized) {
    size_t i;
    double norm = 0.0;
    for (i = 0; i < dim; ++i) {
        uint32_t bits;
        if (u32(r, &bits)) return VECDB_FORMAT;
        memcpy(out + i, &bits, 4);
        if (!isfinite(out[i])) return VECDB_FORMAT;
        norm += (double)out[i] * out[i];
    }
    if (dim && normalized && fabs(norm - 1.0) > 0.0002) return VECDB_FORMAT;
    return VECDB_OK;
}

void vecdb_close(VecDB *db) {
    size_t i;
    if (!db) return;
    if (db->words) for (i = 0; i < db->nw; ++i) {
        free((void *)db->words[i].word); free((void *)db->words[i].key);
        free((void *)db->words[i].lexeme_id);
    }
    if (db->senses) for (i = 0; i < db->ns; ++i) {
        free((void *)db->senses[i].source_id); free((void *)db->senses[i].definition);
        free((void *)db->senses[i].pos);
    }
    free(db->words); free(db->senses); free(db->wv); free(db->sv);
    free(db->metadata); free(db->word_space); free(db->sense_space); free(db);
}

static int decode(VecDB *db, const unsigned char *data, size_t size) {
    Reader r = {data, size, 8};
    uint32_t h[8];
    size_t i, minimum, remaining;
    int rc;
    if (size < 40 || memcmp(data, "MALMOI\0\0", 8)) return VECDB_FORMAT;
    for (i = 0; i < 8; ++i) if (u32(&r, &h[i])) return VECDB_FORMAT;
    if (h[0] != 1 || h[1] != 40 || (h[6] & ~3U) || h[7] ||
        h[4] > MAX_DIMENSION || h[5] > MAX_DIMENSION) return VECDB_FORMAT;
    db->nw = h[2]; db->ns = h[3]; db->dw = h[4]; db->ds = h[5]; db->flags = h[6];
    /* Minimum encoded record sizes bound counts/products BEFORE allocation. */
    remaining = size - r.pos;
    minimum = 12 + 4 * db->dw;
    if (db->nw > remaining / minimum) return VECDB_FORMAT;
    remaining -= db->nw * minimum;
    minimum = 16 + 4 * db->ds;
    if (db->ns > remaining / minimum ||
        db->nw > SIZE_MAX / sizeof(VecDBWord) || db->ns > SIZE_MAX / sizeof(VecDBSense))
        return VECDB_FORMAT;
    if ((rc = owned_string(&r, &db->metadata)) != VECDB_OK ||
        (rc = owned_string(&r, &db->word_space)) != VECDB_OK ||
        (rc = owned_string(&r, &db->sense_space)) != VECDB_OK) return rc;
    if (!db->metadata[0] || (!!db->dw != !!db->word_space[0]) ||
        (!!db->ds != !!db->sense_space[0]) ||
        (!db->dw && (db->flags & 1U)) || (!db->ds && (db->flags & 2U))) return VECDB_FORMAT;
    remaining = size - r.pos;
    minimum = 12 + 4 * db->dw;
    if (db->nw > remaining / minimum) return VECDB_FORMAT;
    remaining -= db->nw * minimum;
    minimum = 16 + 4 * db->ds;
    if (db->ns > remaining / minimum) return VECDB_FORMAT;
    if (db->nw) db->words = (VecDBWord *)calloc(db->nw, sizeof(VecDBWord));
    if (db->ns) db->senses = (VecDBSense *)calloc(db->ns, sizeof(VecDBSense));
    if (db->nw && db->dw) db->wv = (float *)calloc(db->nw * db->dw, sizeof(float));
    if (db->ns && db->ds) db->sv = (float *)calloc(db->ns * db->ds, sizeof(float));
    if ((db->nw && !db->words) || (db->ns && !db->senses) ||
        (db->nw && db->dw && !db->wv) || (db->ns && db->ds && !db->sv)) return VECDB_MEMORY;
    for (i = 0; i < db->nw; ++i) {
        VecDBWord *w = &db->words[i]; w->id = (uint32_t)i;
        if ((rc = string(&r, &w->word)) != VECDB_OK || (rc = string(&r, &w->key)) != VECDB_OK ||
            (rc = string(&r, &w->lexeme_id)) != VECDB_OK) return rc;
        if (!w->word[0] || !w->key[0] || !w->lexeme_id[0]) return VECDB_FORMAT;
        if (db->dw && (rc = vector(&r, db->wv + i * db->dw, db->dw, (db->flags & 1U) != 0)) != VECDB_OK) return rc;
    }
    for (i = 0; i < db->ns; ++i) {
        VecDBSense *s = &db->senses[i]; s->id = (uint32_t)i;
        if (u32(&r, &s->word_id) || s->word_id >= db->nw) return VECDB_FORMAT;
        if ((rc = string(&r, &s->source_id)) != VECDB_OK || (rc = string(&r, &s->definition)) != VECDB_OK ||
            (rc = string(&r, &s->pos)) != VECDB_OK) return rc;
        if (!s->source_id[0] || !s->definition[0]) return VECDB_FORMAT;
        if (db->ds && (rc = vector(&r, db->sv + i * db->ds, db->ds, (db->flags & 2U) != 0)) != VECDB_OK) return rc;
    }
    return r.pos == r.size ? VECDB_OK : VECDB_FORMAT;
}

int vecdb_open(VecDB **out, const char *path) {
    FILE *f;
    long length;
    unsigned char *bytes;
    VecDB *db;
    size_t float_bytes = sizeof(float);
    int rc;
    if (!out) return VECDB_ARGUMENT;
    *out = NULL;
    if (!path || !path[0]) return VECDB_ARGUMENT;
#if CHAR_BIT != 8 || FLT_RADIX != 2 || FLT_MANT_DIG != 24 || FLT_MAX_EXP != 128
    return VECDB_UNSUPPORTED;
#endif
    if (float_bytes != 4) return VECDB_UNSUPPORTED;
#ifdef _WIN32
    {
        int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
        wchar_t *wide;
        if (!n) return VECDB_ARGUMENT;
        wide = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
        if (!wide) return VECDB_MEMORY;
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, n)) {
            free(wide); return VECDB_ARGUMENT;
        }
#ifdef _MSC_VER
        if (_wfopen_s(&f, wide, L"rb") != 0) f = NULL;
#else
        f = _wfopen(wide, L"rb");
#endif
        free(wide);
    }
#else
    f = fopen(path, "rb");
#endif
    if (!f) return VECDB_IO;
    if (fseek(f, 0, SEEK_END) || (length = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) {
        fclose(f); return VECDB_IO;
    }
    if ((unsigned long)length > MAX_FILE_BYTES) { fclose(f); return VECDB_LIMIT; }
    if (length < 40) { fclose(f); return VECDB_FORMAT; }
    bytes = (unsigned char *)malloc((size_t)length);
    if (!bytes) { fclose(f); return VECDB_MEMORY; }
    if (fread(bytes, 1, (size_t)length, f) != (size_t)length) {
        free(bytes); fclose(f); return VECDB_IO;
    }
    rc = fgetc(f);
    if (rc != EOF || ferror(f)) { free(bytes); fclose(f); return VECDB_IO; }
    if (fclose(f)) { free(bytes); return VECDB_IO; }
    db = (VecDB *)calloc(1, sizeof(VecDB));
    if (!db) { free(bytes); return VECDB_MEMORY; }
    rc = decode(db, bytes, (size_t)length); free(bytes);
    if (rc) { vecdb_close(db); return rc; }
    *out = db; return VECDB_OK;
}

size_t vecdb_word_count(const VecDB *db) { return db ? db->nw : 0; }
size_t vecdb_sense_count(const VecDB *db) { return db ? db->ns : 0; }
size_t vecdb_word_dimension(const VecDB *db) { return db ? db->dw : 0; }
size_t vecdb_sense_dimension(const VecDB *db) { return db ? db->ds : 0; }
const char *vecdb_metadata(const VecDB *db) { return db ? db->metadata : NULL; }
const char *vecdb_word_space(const VecDB *db) { return db ? db->word_space : NULL; }
const char *vecdb_sense_space(const VecDB *db) { return db ? db->sense_space : NULL; }

int vecdb_word(const VecDB *db, uint32_t id, VecDBWord *out) {
    if (!out) return VECDB_ARGUMENT;
    memset(out, 0, sizeof(*out)); out->id = UINT32_MAX;
    if (!db) return VECDB_ARGUMENT;
    if (id >= db->nw) return VECDB_RANGE;
    *out = db->words[id]; return VECDB_OK;
}
int vecdb_sense(const VecDB *db, uint32_t id, VecDBSense *out) {
    if (!out) return VECDB_ARGUMENT;
    memset(out, 0, sizeof(*out)); out->id = out->word_id = UINT32_MAX;
    if (!db) return VECDB_ARGUMENT;
    if (id >= db->ns) return VECDB_RANGE;
    *out = db->senses[id]; return VECDB_OK;
}
int vecdb_word_vector(const VecDB *db, uint32_t id, MalmoiVector *out) {
    if (!out) return VECDB_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (!db) return VECDB_ARGUMENT;
    if (id >= db->nw) return VECDB_RANGE;
    if (!db->dw) return VECDB_UNSUPPORTED;
    out->data = db->wv + id * db->dw; out->dimension = db->dw; out->space = db->word_space;
    return VECDB_OK;
}
int vecdb_sense_vector(const VecDB *db, uint32_t id, MalmoiVector *out) {
    if (!out) return VECDB_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (!db) return VECDB_ARGUMENT;
    if (id >= db->ns) return VECDB_RANGE;
    if (!db->ds) return VECDB_UNSUPPORTED;
    out->data = db->sv + id * db->ds; out->dimension = db->ds; out->space = db->sense_space;
    return VECDB_OK;
}

int vecdb_find_words(const VecDB *db, const char *key, uint32_t *out, size_t capacity, size_t *count) {
    size_t i;
    if (!count) return VECDB_ARGUMENT;
    *count = 0;
    if (!db || !key || !key[0] || (capacity && !out) ||
        !utf8((const unsigned char *)key, strlen(key))) return VECDB_ARGUMENT;
    for (i = 0; i < db->nw; ++i) if (!strcmp(db->words[i].key, key)) {
        if (*count < capacity) out[*count] = (uint32_t)i;
        ++*count;
    }
    if (!*count) return VECDB_NOT_FOUND;
    return *count > capacity ? VECDB_LIMIT : VECDB_OK;
}
int vecdb_find_word(const VecDB *db, const char *key, uint32_t *out_id) {
    size_t count;
    uint32_t first = UINT32_MAX;
    int rc;
    if (!out_id) return VECDB_ARGUMENT;
    *out_id = UINT32_MAX;
    rc = vecdb_find_words(db, key, &first, 1, &count);
    if (count > 1) return VECDB_AMBIGUOUS;
    if (!rc) *out_id = first;
    return rc;
}

static int compare(const MalmoiVector *a, const MalmoiVector *b, float *out, int cosine) {
    size_t i;
    double dot = 0.0, na = 0.0, nb = 0.0, value;
    if (!out) return VECDB_ARGUMENT;
    *out = 0.0f;
    if (!a || !b || !a->data || !b->data || !a->space || !b->space ||
        !a->space[0] || !b->space[0] || !a->dimension || !b->dimension) return VECDB_ARGUMENT;
    if (a->dimension != b->dimension || strcmp(a->space, b->space)) return VECDB_SPACE;
    for (i = 0; i < a->dimension; ++i) {
        double x = a->data[i], y = b->data[i];
        if (!isfinite(x) || !isfinite(y)) return VECDB_ARGUMENT;
        dot += x * y; na += x * x; nb += y * y;
    }
    if (cosine && (na == 0 || nb == 0)) return VECDB_ZERO_VECTOR;
    value = cosine ? dot / sqrt(na) / sqrt(nb) : dot;
    if (!isfinite(value) || value > FLT_MAX || value < -FLT_MAX) return VECDB_RANGE;
    if (cosine) { if (value > 1.0) value = 1.0; if (value < -1.0) value = -1.0; }
    *out = (float)value; return VECDB_OK;
}
int vecdb_dot(const MalmoiVector *a, const MalmoiVector *b, float *out) { return compare(a,b,out,0); }
int vecdb_cosine(const MalmoiVector *a, const MalmoiVector *b, float *out) { return compare(a,b,out,1); }

const char *vecdb_status_string(int status) {
    static const char *const names[] = {"ok", "invalid argument", "I/O error", "invalid format",
        "out of memory", "out of range", "not found", "ambiguous word", "incompatible vector space",
        "zero vector", "limit exceeded", "unsupported"};
    return status >= 0 && (size_t)status < sizeof(names)/sizeof(names[0]) ? names[status] : "unknown status";
}
