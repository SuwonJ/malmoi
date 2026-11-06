#ifndef MALLANGI_SIMILARITY_H
#define MALLANGI_SIMILARITY_H

#include "malmoi.h"

int game_similarity(
    const Malmoi *engine,
    const char *answer,
    const char *guess,
    float *score
);

int game_similarity_sense(const Malmoi *engine,const char *answer,int64_t sense_id,const char *guess,float *score);

#endif