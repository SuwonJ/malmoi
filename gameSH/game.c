#include "wordgame.h"
#include <stdlib.h>
#include <string.h>

static void finish(Game *g, int winner, EndReason reason){
    g->finished = 1;
    g->winner = winner;
    g->endReason = reason;
}

/* 기준 단어의 상위 RANK_LIMIT개를 구해 마지막 점수를 하한값으로 */
int game_init(Game *g, const Malmoi *engine, const char *base){
    size_t count = 0;
    int status;

    if(strlen(base) >= WORD_BYTES) return MALMOI_ARGUMENT;
    memset(g, 0, sizeof(*g));
    g->engine = engine;
    g->winner = -1;
    strcpy(g->base, base);
    for(int i = 0; i < PLAYERS; i++) g->life[i] = START_LIFE;

    g->ranking = malloc(RANK_LIMIT * sizeof(*g->ranking));
    if(g->ranking == NULL) return MALMOI_MEMORY;

    status = malmoi_similar(engine, base, g->ranking, RANK_LIMIT, &count);
    if(status == MALMOI_OK && count == 0) status = MALMOI_NOT_FOUND;
    if(status != MALMOI_OK){ game_free(g); return status; }

    g->rankingCount = count;
    g->threshold = g->ranking[count - 1].score;
    return MALMOI_OK;
}

void game_free(Game *g){
    free(g->ranking);
    g->ranking = NULL;
    g->rankingCount = 0;
}

/* 검사 → 유사도 → 기록 → LIFE/턴 처리. PASS/FAIL이면 *out에 기록 */
SubmitStatus game_submit(Game *g, const char *word, Record **out){
    char key[WORD_BYTES], baseKey[WORD_BYTES];
    float score;
    Record *r;
    int status;

    *out = NULL;
    if(g->finished) return SUBMIT_ERROR;
    if(!is_hangul_word(word)) return SUBMIT_BAD_FORMAT;

    make_key(word, key);
    make_key(g->base, baseKey);
    if(strstr(key, baseKey)) return SUBMIT_HAS_BASE;   /* 노래 → 노래하다 차단 */
    if(is_used_word(g, key)) return SUBMIT_DUPLICATE;

    status = malmoi_similarity(g->engine, g->base, word, &score);
    if(status == MALMOI_NOT_FOUND || status == MALMOI_ZERO_VECTOR) return SUBMIT_UNKNOWN;
    if(status != MALMOI_OK) return SUBMIT_ERROR;

    r = &g->records[g->recordCount++];
    r->player = g->turn;
    strcpy(r->word, word);
    r->score = score;
    r->rank = find_rank(g, key);
    r->passed = score >= g->threshold;
    *out = r;

    if(r->passed) g->success[g->turn]++;
    else if(--g->life[g->turn] == 0){
        finish(g, 1 - g->turn, END_LIFE);
        return SUBMIT_FAIL;
    }

    if(g->recordCount >= MAX_RECORDS){
        int w = (g->life[0] == g->life[1]) ? -1 : (g->life[0] > g->life[1] ? 0 : 1);
        finish(g, w, END_LIMIT);
    }else{
        g->turn = 1 - g->turn;
    }
    return r->passed ? SUBMIT_PASS : SUBMIT_FAIL;
}

void game_timeout(Game *g){
    if(!g->finished) finish(g, 1 - g->turn, END_TIMEOUT);
}
