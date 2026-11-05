#ifndef WORDGAME_H
#define WORDGAME_H

#include "malmoi.h"

/* ===== 규칙 (조정 가능) ===== */
#define START_LIFE    3
#define TURN_SECONDS  30
#define RANK_LIMIT    5000   /* 기준 단어의 5000위 유사도를 하한값으로 사용 */

#define PLAYERS       2
#define WORD_BYTES    64
#define MAX_RECORDS   128    /* 최대 제출 수 (도달 시 LIFE 비교) */

typedef enum {
    SUBMIT_PASS,       /* 유사도 ≥ 하한값 */
    SUBMIT_FAIL,       /* 유사도 < 하한값, LIFE -1 */
    SUBMIT_BAD_FORMAT, /* 한글 음절 외 문자 */
    SUBMIT_HAS_BASE,   /* 기준 단어와 같거나 포함 */
    SUBMIT_DUPLICATE,  /* 이미 제출된 단어 */
    SUBMIT_UNKNOWN,    /* 사전에 없는 단어 */
    SUBMIT_ERROR       /* 엔진 오류 또는 종료된 게임 */
} SubmitStatus;

typedef enum { END_NONE, END_LIFE, END_TIMEOUT, END_LIMIT } EndReason;

typedef struct {
    int player;
    char word[WORD_BYTES];
    float score;
    int rank;      /* 순위권 밖이면 0 */
    int passed;
} Record;

typedef struct {
    const Malmoi *engine;
    char base[WORD_BYTES];
    float threshold;
    MalmoiResult *ranking;   /* 기준 단어 유사 상위 목록 */
    size_t rankingCount;
    int life[PLAYERS];
    int success[PLAYERS];
    int turn;
    Record records[MAX_RECORDS];
    int recordCount;
    int finished;
    int winner;              /* 0/1, 무승부 -1 */
    EndReason endReason;
} Game;

/* word_check.c — 단어 검사 */
void make_key(const char *word, char *key);
int  is_hangul_word(const char *word);
int  is_used_word(const Game *g, const char *key);
int  find_rank(const Game *g, const char *key);

/* game.c — 게임 진행·판정 */
int  game_init(Game *g, const Malmoi *engine, const char *base);
void game_free(Game *g);
SubmitStatus game_submit(Game *g, const char *word, Record **out);
void game_timeout(Game *g);

/* ui.c — 콘솔 입출력 */
int  read_line(char *buf, size_t cap);
void print_status(const Game *g);
void print_submit(SubmitStatus s, const Record *r);
void print_result(const Game *g);

#endif
