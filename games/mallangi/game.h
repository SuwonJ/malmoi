
#ifndef MALLANGI_GAME_H
#define MALLANGI_GAME_H

#include "malmoi.h"

#define MALLANGI_MAX_ATTEMPTS 5
#define MALLANGI_WORD_BYTES 64

typedef struct{
    char answer[MALLANGI_WORD_BYTES];
    int attempts;
    char usedWords[MALLANGI_MAX_ATTEMPTS][MALLANGI_WORD_BYTES];
    int usedCount;
    int finished;
    int won;
    const Malmoi *engine;
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

int is_used_word(const GameState *game, const char guess[]);
int is_correct_answer(const GameState *game, const char guess[]);

void draw_cards(int cards[], int cardCount, int wordCount);

void record_wrong_guess(GameState *game, const char guess[]);

GameResult submit_guess(GameState *game, const char guess[]);

#endif
    