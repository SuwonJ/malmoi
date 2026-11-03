#ifndef MALMOI_LEXICAL_H
#define MALMOI_LEXICAL_H
#include <stddef.h>
#include <stdint.h>
typedef struct {uint64_t *grams;size_t count;double inv_norm;} MalmoiGrams;
int malmoi_grams(const char *text,MalmoiGrams *out);
float malmoi_grams_cosine(const MalmoiGrams *a,const MalmoiGrams *b);
void malmoi_grams_free(MalmoiGrams *v);
#endif
