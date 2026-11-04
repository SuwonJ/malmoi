#ifndef MALMOI_BRIDGE_H
#define MALMOI_BRIDGE_H
#include "malmoi.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t neighbors;   /* 단어마다 살펴볼 이웃 수 (기본 10) */
    size_t max_steps;   /* 경로 최대 칸 수 (기본 6) */
    size_t max_visits;  /* 확정할 최대 단어 수 (기본 500) */
} MalmoiBridgeOptions;

typedef struct {
    const char *word;   /* 엔진 소유 문자열, malmoi_close 전까지 유효 */
    const char *key;
    float score;        /* 앞 단어와의 유사도, 출발 단어는 1 */
} MalmoiStep;

void malmoi_bridge_default_options(MalmoiBridgeOptions *options);

/* 성공: MALMOI_OK, out[0]=출발 ... out[count-1]=도착
 * 연결 없음: MALMOI_OK + count=0 / 탐색 한도 초과: MALMOI_LIMIT
 * 없는 단어: MALMOI_NOT_FOUND / 결과 배열 부족: MALMOI_RANGE */
int malmoi_bridge(const Malmoi *engine, const char *from, const char *to,
                  const MalmoiBridgeOptions *options,
                  MalmoiStep *out, size_t capacity, size_t *count);

#ifdef __cplusplus
}
#endif
#endif