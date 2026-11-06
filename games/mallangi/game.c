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

/* Trim only boundary Unicode whitespace. Keep internal spaces and punctuation.
 * Validate UTF-8 before matching, scoring, and recording the same canonical input.
 */
static int guess_space(uint32_t c){return(c>=9&&c<=13)||(c>=28&&c<=32)||c==0x85||c==0xa0||c==0x1680||(c>=0x2000&&c<=0x200a)||c==0x2028||c==0x2029||c==0x202f||c==0x205f||c==0x3000;}
static int clean_guess(const char *text,char out[MALLANGI_WORD_BYTES]){
    size_t i=0,first=0,last=0;int content=0;
    if(!text)return 0;
    while(text[i]){size_t start=i;uint32_t cp=(unsigned char)text[i++],minimum=0;unsigned need=0,j;
        if(i>8192)return 0;
        if(cp>=0xc2&&cp<=0xdf){cp&=31;need=1;minimum=0x80;}
        else if(cp>=0xe0&&cp<=0xef){cp&=15;need=2;minimum=0x800;}
        else if(cp>=0xf0&&cp<=0xf4){cp&=7;need=3;minimum=0x10000;}
        else if(cp>=0x80)return 0;
        for(j=0;j<need;j++){unsigned char c=(unsigned char)text[i];if((c&0xc0)!=0x80)return 0;cp=(cp<<6)|(c&63);i++;if(i>8192)return 0;}
        if(cp<minimum||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return 0;
        if(!guess_space(cp)){if(!content)first=start;content=1;last=i;}
    }
    if(!content||last-first>=MALLANGI_WORD_BYTES)return 0;
    memcpy(out,text+first,last-first);out[last-first]=0;return 1;
}

// 정답 제출 통합 처리
GameResult submit_guess(GameState *game, const char guess[]){
    GameResult result = {
        .status = GUESS_ERROR,
        .similarity = 0.0,
        .engineStatus = MALMOI_OK
    };

    char cleaned[MALLANGI_WORD_BYTES];
    // Normalize once before every comparison, lookup, duplicate check and record.
    if(game == NULL || !clean_guess(guess,cleaned)){
        result.status = GUESS_INVALID;
        return result;
    }

    guess=cleaned;

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

    // A native game must be initialized with a fixed answer sense.
    if(game->engine && !strcmp(malmoi_search_mode(game->engine),"native-index") && !game->answer_has_sense){
        result.engineStatus=MALMOI_CONFIG;
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

    int status = game->answer_has_sense
        ? game_similarity_sense(game->engine,game->answer,game->answer_sense_id,guess,&score)
        : game_similarity(game->engine,game->answer,guess,&score);

    // 미등록 단어 및 유효하지 않은 벡터
    if(status == MALMOI_NOT_FOUND ||
       status == MALMOI_ZERO_VECTOR){
        result.status = GUESS_UNKNOWN;
        return result;
    }

    if(status == MALMOI_ARGUMENT || status == MALMOI_LIMIT){
        result.status=GUESS_INVALID;
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




static int start_selected(
    GameState *game,
    const Malmoi *engine,
    const char *answer, int has_sense, int64_t sense_id
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
    newGame.answer_has_sense=has_sense;
    newGame.answer_sense_id=sense_id;

    newGame.attempts = MALLANGI_MAX_ATTEMPTS;
    newGame.engine = engine;

    // 정답과 유사한 단어 후보 검색
    status = has_sense
        ? malmoi_similar_sense(engine,answer,sense_id,hints,MALLANGI_HINT_POOL_SIZE,&hintCount)
        : malmoi_similar(engine,answer,hints,MALLANGI_HINT_POOL_SIZE,&hintCount);

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

int game_start_sense(GameState *game,const Malmoi *engine,const char *answer,int64_t sense_id){return start_selected(game,engine,answer,1,sense_id);}
int game_start(GameState *game,const Malmoi *engine,const char *answer){size_t n,i;int status;if(!game||!engine||!answer)return 0;status=malmoi_sense_count(engine,answer,&n);if(status==MALMOI_UNSUPPORTED)return start_selected(game,engine,answer,0,0);if(status)return 0;for(i=0;i<n;i++){MalmoiSense sense;float score;if(malmoi_sense_at(engine,answer,i,&sense))return 0;status=malmoi_similarity_sense(engine,answer,sense.sense_id,answer,&score);if(status==MALMOI_ZERO_VECTOR)continue;if(status)return 0;return game_start_sense(game,engine,answer,sense.sense_id);}return 0;}
