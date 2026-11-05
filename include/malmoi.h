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
    uint32_t word_id;  /* valid for this engine's dataset only */
    const char *word;  /* original spelling, read-only */
    const char *key;   /* normalized word */
    float score;       /* lexical search [0,1]; embedding search/similarity [-1,1] */
} MalmoiResult;

enum MalmoiStatus {
    MALMOI_OK=0, MALMOI_ARGUMENT, MALMOI_IO, MALMOI_FORMAT, MALMOI_MEMORY,
    MALMOI_RANGE, MALMOI_NOT_FOUND, MALMOI_AMBIGUOUS, MALMOI_SPACE,
    MALMOI_ZERO_VECTOR, MALMOI_LIMIT, MALMOI_UNSUPPORTED, MALMOI_CONFIG
};

/* Default mode is native C only. Finds data/full.mlm, executable-adjacent
 * data/dictionary.mlm, then working-directory data/dictionary.mlm. MALMOI_DATA may
 * select another UTF-8 path. No model, Python, ONNX or network is needed.
 * Optional E5 build: MALMOI_MODEL selects matching offline model assets and
 * enables semantic description search using embedded Python/ONNX Runtime.
 * Explicit model configuration errors never silently fall back to lexical.
 * Open once and reuse; close after all calls. Failure clears *out.
 * Dataset replacement takes effect on the next open, without caller changes.
 * Concurrent read calls are allowed; close must not race with calls. */
MALMOI_API int malmoi_open(Malmoi **out);
MALMOI_API void malmoi_close(Malmoi *engine);
/* Read-only strings owned by engine; NULL for a NULL engine. The data path
 * is the configured path (may be relative); mode is lexical or e5-semantic. */
MALMOI_API const char *malmoi_data_path(const Malmoi *engine);
MALMOI_API const char *malmoi_search_mode(const Malmoi *engine);

/* Enumerate dictionary word records in dataset order (not frequency order).
 * A normalized key may occur in more than one record, so sampling records
 * uniformly does not sample distinct words uniformly.
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
 * Default: definition character overlap cosine [0,1]. E5 mode: [-1,1].
 * Caller allocates out[k]. count is actual results, cleared on failure.
 * Returned strings belong to engine, valid until malmoi_close.
 * k=0/out=NULL is allowed. Text is limited to 8192 UTF-8 bytes. */
MALMOI_API int malmoi_search(const Malmoi *engine,const char *text,
                            MalmoiResult *out,size_t k,size_t *count);
/* Existing NFC word -> similar words, excluding its own normalized key. */
MALMOI_API int malmoi_similar(const Malmoi *engine,const char *word,
                             MalmoiResult *out,size_t k,size_t *count);
/* Two existing words -> max sense-pair cosine. Unknown words are errors. */
MALMOI_API int malmoi_similarity(const Malmoi *engine,const char *a,const char *b,float *score);
MALMOI_API const char *malmoi_error_string(int status);

#ifdef __cplusplus
}
#endif
#endif
