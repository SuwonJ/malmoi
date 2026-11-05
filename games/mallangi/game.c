#include <limits.h>
#include <stdlib.h>
#include <string.h>
#define MALLANGI_HINT_POOL_SIZE 32
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



int assign_player_cards(
    GameState *game,
    const MalmoiResult hints[],
    size_t hintCount
){
    int selected[MALLANGI_CARD_COUNT];

    if(game == NULL || hints == NULL){
        return 0;
    }

    // 이미 지급한 카드는 다시 뽑지 않는다.
    if(game->cardCount == MALLANGI_CARD_COUNT){
        return 1;
    }

    if(game->cardCount != 0){
        return 0;
    }

    // 카드 후보가 3개보다 적으면 지급 불가
    if(hintCount < MALLANGI_CARD_COUNT || hintCount > INT_MAX){
        return 0;
    }

    // 기존 카드 추첨 함수 재사용
    draw_cards(
        selected,
        MALLANGI_CARD_COUNT,
        (int)hintCount
    );

    // 단어가 저장 공간에 들어가는지 검사
    for(int i = 0; i < MALLANGI_CARD_COUNT; i++){
        const char *word = hints[selected[i]].word;

        if(word == NULL || strlen(word) >= MALLANGI_WORD_BYTES){
            return 0;
        }
    }

    // 선택한 카드 정보를 게임 상태에 저장
    for(int i = 0; i < MALLANGI_CARD_COUNT; i++){
        int index = selected[i];

        strcpy(game->cards[i].word, hints[index].word);
        game->cards[i].similarity = hints[index].score;
    }

    game->cardCount = MALLANGI_CARD_COUNT;

    return 1;
}




int game_start(
    GameState *game,
    const Malmoi *engine,
    const char *answer
) {
    GameState newGame = {0};
    MalmoiResult hints[MALLANGI_HINT_POOL_SIZE];

    size_t hintCount = 0;
    int status;

    // 입력값 검사
    if(game == NULL || engine == NULL || answer == NULL) {
        return 0;
    }

    if(answer[0] == '\0' ||
       strlen(answer) >= MALLANGI_WORD_BYTES) {
        return 0;
    }

    // 새로운 게임 상태 준비
    strcpy(newGame.answer, answer);

    newGame.attempts = MALLANGI_MAX_ATTEMPTS;
    newGame.engine = engine;

    // 정답과 유사한 단어 후보 검색
    status = malmoi_similar(
        engine,
        newGame.answer,
        hints,
        MALLANGI_HINT_POOL_SIZE,
        &hintCount
    );

    if(status != MALMOI_OK) {
        return 0;
    }

    // 플레이어 카드 3장 지급
    if(!assign_player_cards(&newGame, hints, hintCount)) {
        return 0;
    }

    // 모든 준비가 성공한 경우 게임 상태 저장
    *game = newGame;

    return 1;
}
