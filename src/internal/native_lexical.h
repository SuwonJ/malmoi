#ifndef MALMOI_NATIVE_LEXICAL_H
#define MALMOI_NATIVE_LEXICAL_H
#include "malmoi.h"
#include <stdint.h>
typedef struct MalmoiNative MalmoiNative;
typedef struct {
    uint32_t row;
    int64_t sense_id;
    const char *word;
    const char *definition;
    const char *word_type;
    float score;
} MalmoiNativeHit;
int malmoi_native_open(MalmoiNative **out, const char *index_dir, const char *model_dir);
int malmoi_native_search(MalmoiNative *engine, const char *query, MalmoiNativeHit *out, size_t k, size_t *count);
void malmoi_native_close(MalmoiNative *engine);
#endif
