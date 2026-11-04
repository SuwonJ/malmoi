#ifndef MALMOI_E5_ENCODER_H
#define MALMOI_E5_ENCODER_H
#include "search_backend.h"
typedef struct MalmoiE5 MalmoiE5;
int malmoi_e5_open(MalmoiE5 **out, const char *database, const char *model,
                   const char *space, MalmoiEncoder *encoder);
void malmoi_e5_close(MalmoiE5 *encoder);
#endif
