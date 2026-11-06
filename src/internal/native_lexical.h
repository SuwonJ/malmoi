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
size_t malmoi_native_word_count(const MalmoiNative *engine);
int malmoi_native_word_at(const MalmoiNative *engine,size_t index,MalmoiWord *out);
int malmoi_native_word_id(MalmoiNative *engine,const char *key,uint32_t *out);
int malmoi_native_similar(MalmoiNative *engine,const char *word,MalmoiResult *out,size_t k,size_t *count);
int malmoi_native_similarity(MalmoiNative *engine,const char *a,const char *b,float *score);
int malmoi_native_sense_count(MalmoiNative *engine,const char *word,size_t *count);
int malmoi_native_sense_at(MalmoiNative *engine,const char *word,size_t index,MalmoiSense *out);
int malmoi_native_similar_sense(MalmoiNative *engine,const char *word,int64_t sense_id,MalmoiResult *out,size_t k,size_t *count);
int malmoi_native_similarity_sense(MalmoiNative *engine,const char *word,int64_t sense_id,const char *guess,float *score);
void malmoi_native_close(MalmoiNative *engine);
#endif
