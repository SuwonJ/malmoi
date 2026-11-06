#ifndef MALMOI_SHA256_H
#define MALMOI_SHA256_H
#include <stddef.h>
#include <stdint.h>
typedef struct { uint32_t h[8]; uint64_t bytes; unsigned char block[64]; size_t used; } MalmoiSha256;
void malmoi_sha256_init(MalmoiSha256 *s);
void malmoi_sha256_update(MalmoiSha256 *s,const void *data,size_t n);
void malmoi_sha256_final(MalmoiSha256 *s,unsigned char out[32]);
#endif
