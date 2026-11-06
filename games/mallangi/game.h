
#ifndef MALLANGI_GAME_H
#define MALLANGI_GAME_H

#include "malmoi.h"

#define MALLANGI_MAX_ATTEMPTS 5
#define MALLANGI_WORD_BYTES 64

#define MALLANGI_CARD_COUNT 3

typedef struct {
    char word[MALLANGI_WORD_BYTES];
    float similarity;
} MallangiCard;

typedef struct {
    char answer[MALLANGI_WORD_BYTES];
    int64_t answer_sense_id;
    int answer_has_sense;
    int attempts;

    char usedWords[MALLANGI_MAX_ATTEMPTS][MALLANGI_WORD_BYTES];
    int usedCount;

    int finished;
    int won;

    const Malmoi *engine;

    // STEP 14-3: 플레이어 카드
    MallangiCard cards[MALLANGI_CARD_COUNT];
    int cardCount;
} GameState;

typedef enum{
    GUESS_CORRECT,
    GUESS_WRONG,
    GUESS_DUPLICATE,
    GUESS_UNKNOWN,
    GUESS_INVALID,
    GUESS_FINISHED,
    GUESS_ERROR
} GuessStatus;

typedef struct{
    GuessStatus status;
    double similarity;
    int engineStatus;
} GameResult;

int assign_player_cards(
    GameState *game,
    const MalmoiResult hints[],
    size_t hintCount
);

/* Explicit native answer sense. On failure the existing game is unchanged. */
int game_start_sense(GameState *game,const Malmoi *engine,const char *answer,int64_t sense_id);
/* Compatibility: native selects the first nonzero source sense and stores its ID;
 * legacy retains word-based scoring. Prefer game_start_sense for daily answers. */
int game_start(
    GameState *game,
    const Malmoi *engine,
    const char *answer
);

int is_used_word(const GameState *game, const char guess[]);
int is_correct_answer(const GameState *game, const char guess[]);

void draw_cards(int cards[], int cardCount, int wordCount);

void record_wrong_guess(GameState *game, const char guess[]);

GameResult submit_guess(GameState *game, const char guess[]);

#endif
    