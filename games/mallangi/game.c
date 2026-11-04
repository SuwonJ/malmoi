
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "similarity.h"

// 표제어 비교: ASCII '-'와 '^' 제외
static int same_word(const char a[], const char b[]){
    while(1){
        while(*a == '-' || *a == '^'){
            a++;
        }

        while(*b == '-' || *b == '^'){
            b++;
        }

        if(*a != *b){
            return 0;
        }

        if(*a == '\0'){
            return 1;
        }

        a++;
        b++;
    }
}

// 이미 제출한 단어인지 검사
int is_used_word(const GameState *game, const char guess[]){
    for(int i = 0; i < game->usedCount; i++){
        if(same_word(game->usedWords[i], guess)){
            return 1;
        }
    }

    return 0;
}

// 정답 여부 검사
int is_correct_answer(const GameState *game, const char guess[]){
    return same_word(game->answer, guess);
}

// 중복 없는 카드 추첨
void draw_cards(int cards[], int cardCount, int wordCount){
    if(cards == NULL || cardCount < 0 ||
       wordCount <= 0 || cardCount > wordCount){
        return;
    }

    for(int i = 0; i < cardCount; i++){
        int pick;
        int duplicate;

        do{
            pick = rand() % wordCount;
            duplicate = 0;

            for(int j = 0; j < i; j++){
                if(cards[j] == pick){
                    duplicate = 1;
                    break;
                }
            }
        }while(duplicate);

        cards[i] = pick;
    }
}

// 오답 기록 및 남은 기회 차감
void record_wrong_guess(GameState *game, const char guess[]){
    if(game->finished ||
       game->usedCount >= MALLANGI_MAX_ATTEMPTS ||
       game->attempts <= 0){
        return;
    }

    strcpy(game->usedWords[game->usedCount], guess);

    game->usedCount++;
    game->attempts--;

    if(game->attempts == 0){
        game->finished = 1;
    }
}

// 정답 제출 통합 처리
GameResult submit_guess(GameState *game, const char guess[]){
    GameResult result = {
        .status = GUESS_ERROR,
        .similarity = 0.0,
        .engineStatus = MALMOI_OK
    };

    // 입력 검사
    if(game == NULL || guess == NULL ||
       guess[0] == '\0' ||
       strlen(guess) >= MALLANGI_WORD_BYTES){
        result.status = GUESS_INVALID;
        return result;
    }

    // 게임 종료 여부
    if(game->finished || game->attempts <= 0){
        result.status = GUESS_FINISHED;
        return result;
    }

    // 중복 단어 검사
    if(is_used_word(game, guess)){
        result.status = GUESS_DUPLICATE;
        return result;
    }

    // 정답 검사
    if(is_correct_answer(game, guess)){
        game->finished = 1;
        game->won = 1;

        result.status = GUESS_CORRECT;
        return result;
    }

    // 엔진 연결 확인
    if(game->engine == NULL){
        result.engineStatus = MALMOI_CONFIG;
        return result;
    }

    // 실제 코사인 유사도 계산
    float score = 0.0f;

    int status = game_similarity(
        game->engine,
        game->answer,
        guess,
        &score
    );

    // 미등록 단어 및 유효하지 않은 벡터
    if(status == MALMOI_NOT_FOUND ||
       status == MALMOI_ZERO_VECTOR){
        result.status = GUESS_UNKNOWN;
        return result;
    }

    // 기타 엔진 오류
    if(status != MALMOI_OK){
        result.engineStatus = status;
        return result;
    }

    // 정상적인 오답만 기록
    record_wrong_guess(game, guess);

    result.status = GUESS_WRONG;
    result.similarity = score;

    return result;
}
