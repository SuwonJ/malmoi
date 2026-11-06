#ifndef MALMOI_H
#define MALMOI_H
#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(VECDB_BUILD_DLL)
#define MALMOI_API __declspec(dllexport)
#else
#define MALMOI_API
#endif
#ifdef __cplusplus
extern "C" {
#endif

typedef struct Malmoi Malmoi;
typedef struct {
    uint32_t word_id;  /* engine-local word ID, identical to malmoi_word_at index */
    const char *word;  /* original spelling, read-only */
    const char *key;   /* normalized word */
    float score;       /* native [0,1], E5 [-1,1], rank fusion [0,2] */
} MalmoiResult;

enum MalmoiStatus {
    MALMOI_OK=0, MALMOI_ARGUMENT, MALMOI_IO, MALMOI_FORMAT, MALMOI_MEMORY,
    MALMOI_RANGE, MALMOI_NOT_FOUND, MALMOI_AMBIGUOUS, MALMOI_SPACE,
    MALMOI_ZERO_VECTOR, MALMOI_LIMIT, MALMOI_UNSUPPORTED, MALMOI_CONFIG
};

/* Default mode is native C only. Finds data/full.mlm, executable-adjacent
 * data/dictionary.mlm, then working-directory data/dictionary.mlm in legacy mode. MALMOI_DATA may
 * select another UTF-8 path. CMake default uses executable-adjacent runtime/
 * for the native Kiwi index; all native APIs use it without opening MLM.
 * No Python, ONNX or runtime network is needed.
 * Optional E5 build: MALMOI_MODEL selects matching offline model assets and
 * enables semantic description search using embedded Python/ONNX Runtime.
 * Explicit model configuration errors never silently fall back to lexical.
 * Open once and reuse; close after all calls. Failure clears *out.
 * Dataset replacement takes effect on the next open, without caller changes.
 * Concurrent read calls are allowed; close must not race with calls. */
MALMOI_API int malmoi_open(Malmoi **out);
MALMOI_API void malmoi_close(Malmoi *engine);
/* Read-only strings owned by engine; NULL for a NULL engine. The data path
 * is the configured path (may be relative); mode is native-index, lexical or e5-semantic. */
MALMOI_API const char *malmoi_data_path(const Malmoi *engine);
MALMOI_API const char *malmoi_search_mode(const Malmoi *engine);

/* Enumerate dictionary word records in dataset order (not frequency order).
 * Native mode: one record per canonical key, sorted by UTF-8 bytes,
 * id == index; all senses of that key share this ID.
 * Legacy mode: duplicate keys may occur in separate MLM records.
 * word and key are read-only engine-owned pointers, valid until close.
 * Index must be less than malmoi_word_count; null arguments are errors. */
typedef struct {
    uint32_t id;
    const char *word;
    const char *key;
} MalmoiWord;
MALMOI_API size_t malmoi_word_count(const Malmoi *engine);
MALMOI_API int malmoi_word_at(const Malmoi *engine, size_t index, MalmoiWord *out);

/* NFC UTF-8 description -> words, ranked by the configured search mode.
 * CMake default: TF-IDF + Kiwi BM25 rank fusion [0,2].
 * Legacy lexical mode: [0,1]. E5 mode: [-1,1].
 * Caller allocates out[k]. count is actual results, cleared on failure.
 * Returned strings belong to engine, valid until malmoi_close.
 * Native reverse ordering is by sense fusion, then word deduplication;
 * word_id refers to the whole-index catalog (not the source sense ID).
 * k=0/out=NULL is allowed. Text is limited to 8192 UTF-8 bytes. */
MALMOI_API int malmoi_search(const Malmoi *engine,const char *text,
                            MalmoiResult *out,size_t k,size_t *count);
/* Native full-corpus search additionally exposes the source sense ID and row.
 * Strings are engine-owned until malmoi_close. Only the native-index backend
 * supports this function; scores are the same rank-fusion values as search. */
typedef struct {
    uint32_t row;
    int64_t sense_id;
    const char *word;
    const char *definition;
    const char *word_type;
    float score;
} MalmoiSenseResult;
MALMOI_API int malmoi_search_senses(const Malmoi *engine,const char *text,
                                   MalmoiSenseResult *out,size_t k,size_t *count);
/* Native sense selection. IDs are original dictionary sense IDs, scoped by word.
 * Returned strings live until close. Legacy backends return UNSUPPORTED.
 * A selected source sense is fixed; candidate/guess words use their best sense.
 * Native corpus/index version determines which sense IDs are available. */
typedef struct {
    uint32_t row, word_id;
    int64_t sense_id;
    const char *word, *definition;
} MalmoiSense;
MALMOI_API int malmoi_sense_count(const Malmoi *engine,const char *word,size_t *count);
MALMOI_API int malmoi_sense_at(const Malmoi *engine,const char *word,size_t index,MalmoiSense *out);
MALMOI_API int malmoi_similar_sense(const Malmoi *engine,const char *word,int64_t sense_id,MalmoiResult *out,size_t k,size_t *count);
MALMOI_API int malmoi_similarity_sense(const Malmoi *engine,const char *word,int64_t sense_id,const char *guess,float *score);
/* Native: TF-IDF + symmetric normalized Kiwi BM25, [0,1], excluding the
 * same normalized word. Score is exploratory and is not a probability. */
MALMOI_API int malmoi_similar(const Malmoi *engine,const char *word,
                             MalmoiResult *out,size_t k,size_t *count);
/* Two existing words -> maximum combined sense-pair score. Native: equal
 * definition TF-IDF cosine and normalized symmetric Kiwi BM25, [0,1];
 * if one shared component is unavailable, use the other; legacy: stored vectors. Same canonical word returns 1 if
 * any sense has a nonzero vector; all-zero words return ZERO_VECTOR. */
MALMOI_API int malmoi_similarity(const Malmoi *engine,const char *a,const char *b,float *score);
MALMOI_API const char *malmoi_error_string(int status);

#ifdef __cplusplus
}
#endif
#endif
