#ifndef MALLANGI_SIMILARITY_H
#define MALLANGI_SIMILARITY_H

#include "malmoi.h"

int game_similarity(
    const Malmoi *engine,
    const char *answer,
    const char *guess,
    float *score
);

#endif