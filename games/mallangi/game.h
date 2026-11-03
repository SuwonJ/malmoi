
#ifndef MALLANGI_GAME_H
#define MALLANGI_GAME_H

#include "malmoi.h"

// 게임 상태 구조체
typedef struct{
    char answer[64];
    int attempts;
    char usedWords[5][64];
    int usedCount;

    // Mock 데이터
    char (*mockWords)[20];
    const double *mockScores;
    int mockWordCount;

    // 실제 유사도 엔진
    const Malmoi *engine;
} GameState;

// 단어 제출 결과의 종류
typedef enum{
    GUESS_CORRECT,
    GUESS_WRONG,
    GUESS_DUPLICATE,
    GUESS_UNKNOWN,
    GUESS_INVALID,
    GUESS_FINISHED,
    GUESS_ERROR
} GuessStatus;

// 단어 제출 결과
typedef struct{
    GuessStatus status;
    double similarity;
    int engineStatus;
} GameResult;

// 중복 단어 검사
int is_used_word(const GameState *game, const char guess[]);

// 랜덤 카드 추첨
void draw_cards(int cards[], int cardCount, int wordCount);

// 정답 여부 검사
int is_correct_answer(const GameState *game, const char guess[]);

// 오답 기록 및 기회 차감
void record_wrong_guess(GameState *game, const char guess[]);

// 단어 제출 통합 처리
GameResult submit_guess(GameState *game, const char guess[]);

#endif
