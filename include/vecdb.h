#ifndef MALMOI_VECDB_H
#define MALMOI_VECDB_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(VECDB_BUILD_DLL)
#define VECDB_API __declspec(dllexport)
#else
#define VECDB_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VecDB VecDB;

enum VecDBStatus {
    VECDB_OK = 0, VECDB_ARGUMENT, VECDB_IO, VECDB_FORMAT, VECDB_MEMORY,
    VECDB_RANGE, VECDB_NOT_FOUND, VECDB_AMBIGUOUS, VECDB_SPACE,
    VECDB_ZERO_VECTOR, VECDB_LIMIT, VECDB_UNSUPPORTED
};

typedef struct { uint32_t id; float score; } MalmoiHit;
typedef struct {
    uint32_t id;
    const char *word;
    const char *key;
    const char *lexeme_id;
} VecDBWord;
typedef struct {
    uint32_t id;
    uint32_t word_id;
    const char *source_id;
    const char *definition;
    const char *pos;
} VecDBSense;
typedef struct {
    const float *data;
    size_t dimension;
    const char *space;
} MalmoiVector;

/* All returned views are read-only and valid until vecdb_close(db).
 * On failure, pointer/view outputs are cleared; IDs become UINT32_MAX.
 * path is UTF-8 (converted to a wide path on Windows).
 * Files are limited to 512 MiB in v1. No mmap/POSIX dependency. */
VECDB_API int vecdb_open(VecDB **out, const char *path);
VECDB_API void vecdb_close(VecDB *db);
VECDB_API const char *vecdb_status_string(int status);
VECDB_API size_t vecdb_word_count(const VecDB *db);
VECDB_API size_t vecdb_sense_count(const VecDB *db);
VECDB_API size_t vecdb_word_dimension(const VecDB *db);
VECDB_API size_t vecdb_sense_dimension(const VecDB *db);
VECDB_API const char *vecdb_metadata(const VecDB *db);
VECDB_API const char *vecdb_word_space(const VecDB *db);
VECDB_API const char *vecdb_sense_space(const VecDB *db);
VECDB_API int vecdb_word(const VecDB *db, uint32_t id, VecDBWord *out);
VECDB_API int vecdb_sense(const VecDB *db, uint32_t id, VecDBSense *out);
VECDB_API int vecdb_word_vector(const VecDB *db, uint32_t id, MalmoiVector *out);
VECDB_API int vecdb_sense_vector(const VecDB *db, uint32_t id, MalmoiVector *out);

/* key must be a valid UTF-8 NFC search key prepared by tools/prepare.py.
 * No Unicode normalization library is required by the C core.
 * find_words sets count to the TOTAL matches, including on VECDB_LIMIT.
 * capacity=0/out=NULL supports a count-only query. Single lookup rejects
 * homonyms with VECDB_AMBIGUOUS instead of choosing one silently. */
VECDB_API int vecdb_find_word(const VecDB *db, const char *key, uint32_t *out_id);
VECDB_API int vecdb_find_words(const VecDB *db, const char *key,
                              uint32_t *out, size_t capacity, size_t *count);

/* Same dimension AND byte-identical space descriptor are required.
 * cosine handles unnormalized vectors; zero norms return ZERO_VECTOR.
 * dot is a raw inner product, not automatically a cosine/probability. */
VECDB_API int vecdb_dot(const MalmoiVector *a, const MalmoiVector *b, float *out);
VECDB_API int vecdb_cosine(const MalmoiVector *a, const MalmoiVector *b, float *out);

#ifdef __cplusplus
}
#endif
#endif
