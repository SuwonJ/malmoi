
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "game.h"
#include "similarity.h"

int main(void){
    // 테스트용 단어 데이터
    char words[6][20] = {
        "공항", "여권", "날개",
        "자동차", "기차", "사과"
    };

    double scores[6] = {
        0.78, 0.62, 0.71,
        0.34, 0.53, 0.08
    };

    // 실제 엔진 사용 여부
    int useRealEngine = 0;
    Malmoi *engine = NULL;

    // 게임 상태 초기화
    GameState game = {
        .answer = "비행기",
        .attempts = 5,
        .usedCount = 0,
        .mockWords = words,
        .mockScores = scores,
        .mockWordCount = 6,
        .engine = NULL
    };

    int cards[3];
    char guess[64];

    // 1. 실제 엔진 초기화
    if(useRealEngine == 1){
        int status = malmoi_open(&engine);

        if(status != MALMOI_OK){
            printf("엔진 열기 실패: %s\n",
                   malmoi_error_string(status));
            return 1;
        }

        game.engine = engine;
        printf("malmoi 엔진 연결 성공!\n");
    }

    // 2. 게임 시작
    printf("===== MALLANGI =====\n");
    printf("오늘의 비밀 단어를 맞혀보세요!\n");

    // 3. 카드 지급
    srand((unsigned int)time(NULL));

    draw_cards(cards, 3, 6);

    printf("\n[내 단어 카드]\n");

    for(int i = 0; i < 3; i++){
        int index = cards[i];

        printf("%d번 카드: %s\n", i + 1, words[index]);
        printf("유사도: %.2f\n\n", scores[index]);
    }

    // 4. 정답 제출 반복
    while(game.attempts > 0){
        printf("\n남은 정답 제출 횟수: %d\n", game.attempts);
        printf("정답을 입력하세요: ");

        if(scanf("%63s", guess) != 1){
            printf("입력을 종료합니다.\n");

            if(engine != NULL){
                malmoi_close(engine);
            }

            return 1;
        }

        // 5. 제출 결과 받기
        GameResult result = submit_guess(&game, guess);

        // 6. 결과에 따라 출력
        if(result.status == GUESS_CORRECT){
            printf("\n정답입니다!\n");
            break;
        }
        else if(result.status == GUESS_DUPLICATE){
            printf("이미 제출한 단어입니다.\n");
        }
        else if(result.status == GUESS_UNKNOWN){
            printf("등록되지 않은 단어입니다.\n");
        }
        else if(result.status == GUESS_INVALID){
            printf("올바르지 않은 입력입니다.\n");
        }
        else if(result.status == GUESS_WRONG){
            printf("틀렸습니다!\n");
            printf("정답과의 유사도: %.2f\n",
                   result.similarity);
        }
        else if(result.status == GUESS_FINISHED){
            printf("이미 종료된 게임입니다.\n");
            break;
        }
        else{
            printf("게임 처리 오류: %s\n",
                   malmoi_error_string(result.engineStatus));

            if(engine != NULL){
                malmoi_close(engine);
            }

            return 1;
        }
    }

    // 7. 게임 종료
    if(game.attempts == 0){
        printf("\n모든 기회를 사용했습니다.\n");
        printf("정답은 %s입니다.\n", game.answer);
    }

    // 8. 엔진 종료
    if(engine != NULL){
        malmoi_close(engine);
    }

    return 0;
}
