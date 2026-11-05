
#ifndef MALLANGI_DAILY_H
#define MALLANGI_DAILY_H

#include <time.h>

// 한국 날짜 기준 정답 인덱스 계산
int daily_index_kst(time_t now, int wordCount);

// 한국 날짜 기준 오늘의 정답 반환
const char *daily_answer_kst(time_t now);

#endif
