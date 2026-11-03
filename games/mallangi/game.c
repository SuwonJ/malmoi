
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "similarity.h"

// 1. 중복 단어 검사
int is_used_word(const GameState *game, const char guess[]){
    for(int i = 0; i < game->usedCount; i++){
        if(strcmp(guess, game->usedWords[i]) == 0){
            return 1;
        }
    }

    return 0;
}

// 2. 랜덤 카드 추첨
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
        }while(duplicate == 1);

        cards[i] = pick;
    }
}

// 3. 정답 여부 확인
int is_correct_answer(const GameState *game, const char guess[]){
    return strcmp(game->answer, guess) == 0;
}

// 4. 오답 기록 및 제출 횟수 감소
void record_wrong_guess(GameState *game, const char guess[]){
    if(game->usedCount >= 5 || game->attempts <= 0){
        return;
    }

    strcpy(game->usedWords[game->usedCount], guess);

    game->usedCount++;
    game->attempts--;
}

// 5. 정답 제출 통합 처리
GameResult submit_guess(GameState *game, const char guess[]){
    GameResult result = {
        .status = GUESS_ERROR,
        .similarity = 0.0,
        .engineStatus = MALMOI_OK
    };

    // 입력 검사
    if(game == NULL || guess == NULL){
        result.status = GUESS_INVALID;
        return result;
    }

    if(guess[0] == '\0' || strlen(guess) >= 64){
        result.status = GUESS_INVALID;
        return result;
    }

    // 이미 종료된 게임인지 확인
    if(game->attempts <= 0){
        result.status = GUESS_FINISHED;
        return result;
    }

    // 중복 검사
    if(is_used_word(game, guess)){
        result.status = GUESS_DUPLICATE;
        return result;
    }

    // 정답 검사
    if(is_correct_answer(game, guess)){
        result.status = GUESS_CORRECT;
        return result;
    }

    // 실제 엔진 사용
    if(game->engine != NULL){
        float score = 0.0f;

        int status = game_similarity(
            game->engine,
            game->answer,
            guess,
            &score
        );

        if(status == MALMOI_NOT_FOUND){
            result.status = GUESS_UNKNOWN;
            return result;
        }

        if(status != MALMOI_OK){
            result.status = GUESS_ERROR;
            result.engineStatus = status;
            return result;
        }

        result.similarity = score;
    }
    else{
        // Mock 데이터 사용
        int found = 0;

        if(game->mockWords == NULL ||
           game->mockScores == NULL ||
           game->mockWordCount <= 0){
            result.status = GUESS_ERROR;
            result.engineStatus = MALMOI_CONFIG;
            return result;
        }

        for(int i = 0; i < game->mockWordCount; i++){
            if(strcmp(guess, game->mockWords[i]) == 0){
                result.similarity = game->mockScores[i];
                found = 1;
                break;
            }
        }

        if(found == 0){
            result.status = GUESS_UNKNOWN;
            return result;
        }
    }

    // 정상적인 오답만 기록
    record_wrong_guess(game, guess);

    result.status = GUESS_WRONG;

    return result;
}
