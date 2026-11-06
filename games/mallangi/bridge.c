#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "game.h"
#include "daily.h"

static Malmoi *engine;
void *mallangi_create(void) { return calloc(1, sizeof(GameState)); }
void mallangi_destroy(void *game) { free(game); }
static char response[8192];

static int open_engine(void) {
    if (engine) return MALMOI_OK;
    int status = malmoi_open(&engine);
    if (status == MALMOI_OK) srand((unsigned)time(NULL));
    return status;
}

// Validate the whole 32-entry pool used by game_start(), not only a lucky draw.
int mallangi_validate_daily(const char *word) {
    if (!word || !*word || strlen(word) >= MALLANGI_WORD_BYTES) return MALMOI_ARGUMENT;
    int status = open_engine();
    if (status != MALMOI_OK) return status;
    GameState probe={0};
    if(!game_start(&probe,engine,word))return MALMOI_RANGE;
    MalmoiResult hints[32];
    size_t count=0;
    status=probe.answer_has_sense?malmoi_similar_sense(engine,word,probe.answer_sense_id,hints,32,&count):malmoi_similar(engine,word,hints,32,&count);
    if (status != MALMOI_OK) return status;
    if (count < MALLANGI_CARD_COUNT) return MALMOI_RANGE;
    for (size_t i = 0; i < count; ++i)
        if (!hints[i].word || !*hints[i].word
                || strlen(hints[i].word) >= MALLANGI_WORD_BYTES || !isfinite(hints[i].score))
            return MALMOI_RANGE;
    return MALMOI_OK;
}

// These functions are called only by the persistence layer, never by HTTP routes.
const char *mallangi_saved_answer(void *handle) {
    return handle ? ((GameState *)handle)->answer : NULL;
}

int mallangi_restore_sense(void *handle, const char *answer, int has_sense, int64_t sense_id,
                     const char **words, const float *scores, int count) {
    if ((has_sense!=0 && has_sense!=1) || !handle || !answer || !words || !scores || count != MALLANGI_CARD_COUNT)
        return 0;
    if (open_engine() != MALMOI_OK) return 0;
    GameState restored = {0};
    float self_score;
    if (!*answer || strlen(answer) >= MALLANGI_WORD_BYTES
            || (has_sense ? malmoi_similarity_sense(engine,answer,sense_id,answer,&self_score) : malmoi_similarity(engine,answer,answer,&self_score)) != MALMOI_OK
            || (!strcmp(malmoi_search_mode(engine),"native-index") && !has_sense))
        return 0;
    strcpy(restored.answer, answer);
    restored.answer_has_sense=has_sense;restored.answer_sense_id=sense_id;
    restored.attempts = MALLANGI_MAX_ATTEMPTS;
    restored.engine = engine;
    restored.cardCount = count;
    for (int i = 0; i < count; ++i) {
        float score;
        if (!words[i] || !*words[i] || strlen(words[i]) >= MALLANGI_WORD_BYTES
                || !isfinite(scores[i]) || scores[i] < -1 || scores[i] > 1)
            return 0;
        if ((has_sense?malmoi_similarity_sense(engine,answer,sense_id,words[i],&score):malmoi_similarity(engine,answer,words[i],&score)) != MALMOI_OK
                || fabsf(score - scores[i]) > 0.0001f)
            return 0;
        strcpy(restored.cards[i].word, words[i]);
        restored.cards[i].similarity = scores[i];
    }
    *(GameState *)handle = restored;
    return 1;
}

int64_t mallangi_saved_sense_id(void *handle){return handle?((GameState*)handle)->answer_sense_id:0;}
int mallangi_saved_has_sense(void *handle){return handle?((GameState*)handle)->answer_has_sense:0;}
/* Word-only restoration cannot infer an old game's intended sense. */
int mallangi_restore(void *handle,const char *answer,const char **words,const float *scores,int count){
    if(open_engine()!=MALMOI_OK||!strcmp(malmoi_search_mode(engine),"native-index"))return 0;
    return mallangi_restore_sense(handle,answer,0,0,words,scores,count);
}

static void json_string(FILE *out, const char *value) {
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        if (*p == '"' || *p == '\\') fputc('\\', out);
        if (*p < 32) fprintf(out, "\\u%04x", *p);
        else fputc(*p, out);
    }
    fputc('"', out);
}

// HTTPServer serializes access to the shared response buffer.
const char *mallangi_request(void *handle, const char *guess) {
    GameState *game = handle;
    if (!game) return "{\"error\":\"invalid session\"}";
    if (!engine) {
        int status = open_engine();
        if (status != MALMOI_OK) {
            snprintf(response, sizeof response, "{\"error\":\"malmoi_open failed\",\"code\":%d}", status);
            return response;
        }
    }
    GameResult result = {0};
    if (!guess) {
        const char *answer = getenv("MALLANGI_ANSWER");
        if (!answer || !*answer) answer = daily_answer_kst(time(NULL));
        const char *sense_text=getenv("MALLANGI_SENSE_ID");int started;
        if(sense_text&&*sense_text){char *end;long long sid;errno=0;sid=strtoll(sense_text,&end,10);started=(!errno&&!*end)?game_start_sense(game,engine,answer,(int64_t)sid):0;}
        else started=game_start(game,engine,answer);
        if (!started) {
            return "{\"error\":\"game_start failed: check answer and dataset\"}";
        }
    } else if (*guess) {
        result = submit_guess(game, guess);
    }
    FILE *out = fmemopen(response, sizeof response, "w");
    if (!out) return "{\"error\":\"response allocation failed\"}";
    static const char *statuses[] = {
        "correct", "wrong", "duplicate", "unknown", "invalid", "finished", "error"
    };
    fprintf(out, "{\"status\":\"%s\",\"remaining\":%d,\"finished\":%s,\"won\":%s,\"score\":%.8f,\"engineStatus\":%d,\"cards\":[",
        guess && *guess ? statuses[result.status] : "started", game->attempts,
        game->finished ? "true" : "false", game->won ? "true" : "false",
        isfinite(result.similarity) ? result.similarity : 0.0, result.engineStatus);
    for (int i = 0; i < game->cardCount; ++i) {
        if (i) fputc(',', out);
        fputs("{\"word\":", out);
        json_string(out, game->cards[i].word);
        fprintf(out, ",\"score\":%.8f}", isfinite(game->cards[i].similarity) ? game->cards[i].similarity : 0.0);
    }
    fputs("]", out);
    if (game->finished) {
        fputs(",\"answer\":", out);
        json_string(out, game->answer);
        if(game->answer_has_sense)fprintf(out,",\"answerSenseId\":%lld",(long long)game->answer_sense_id);
    }
    fputs("}", out);
    fclose(out);
    return response;
}

// A room owns a template game whose answer is fixed when the room is created.
const char *mallangi_join(void *handle, void *room_handle) {
    GameState *room = room_handle;
    if (!handle || !room || !engine || !room->answer[0])
        return "{\"error\":\"invalid room\"}";
    if (!(room->answer_has_sense?game_start_sense(handle,engine,room->answer,room->answer_sense_id):game_start(handle,engine,room->answer)))
        return "{\"error\":\"room game initialization failed\"}";
    return mallangi_request(handle, "");
}
