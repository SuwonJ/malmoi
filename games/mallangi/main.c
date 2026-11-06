
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "malmoi.h"
#include "game.h"
#include "daily.h"

#define INPUT_SIZE 256

int main(void) {
    Malmoi *engine = NULL;
    GameState game = {0};

    char guess[INPUT_SIZE];
    int status;

    // 카드 무작위 추첨을 위한 시드 설정
    srand((unsigned int)time(NULL));

    // 오늘의 정답 설정
    const char *answer = getenv("MALLANGI_ANSWER");

    if(answer == NULL || answer[0] == '\0') {
        answer = daily_answer_kst(time(NULL));
    }

    // malmoi 엔진 실행
    status = malmoi_open(&engine);

    if(status != MALMOI_OK) {
        printf("malmoi 엔진 실행 실패: %d\n", status);
        return 1;
    }

    // 게임 시작 및 카드 지급
    const char *sense_text=getenv("MALLANGI_SENSE_ID");
    int started;
    if(sense_text&&*sense_text){char *end;long long sid;errno=0;sid=strtoll(sense_text,&end,10);started=(!errno&&!*end)?game_start_sense(&game,engine,answer,(int64_t)sid):0;}
    else started=game_start(&game,engine,answer);
    if(!started) {
        printf("게임 초기화에 실패했습니다.\n");
        malmoi_close(engine);
        return 1;
    }

    printf("===== MALLANGI =====\n");
    printf("오늘의 비밀 단어를 맞혀보세요!\n");

    // 플레이어 카드 출력
    printf("\n[내 단어 카드]\n");

    for(int i = 0; i < game.cardCount; i++) {
        printf("%d번 카드: %s\n",
               i + 1,
               game.cards[i].word);

        printf("유사도: %.2f\n\n",
               game.cards[i].similarity);
    }

    // 게임 진행
    while(!game.finished) {
        printf("\n남은 정답 제출 횟수: %d\n", game.attempts);
        printf("정답을 입력하세요: ");
        fflush(stdout);

        if(fgets(guess, sizeof(guess), stdin) == NULL) {
            printf("\n입력이 종료되었습니다.\n");
            break;
        }

        // 개행 문자 제거
        char *newline = strchr(guess, '\n');

        if(newline != NULL) {
            *newline = '\0';
        } else {
            int ch;

            while((ch = getchar()) != '\n' && ch != EOF) {
            }

            printf("입력한 단어가 너무 깁니다.\n");
            continue;
        }

        if(guess[0] == '\0') {
            printf("단어를 입력해주세요.\n");
            continue;
        }

        if(strlen(guess) >= MALLANGI_WORD_BYTES) {
            printf("입력한 단어가 너무 깁니다.\n");
            continue;
        }

        // 정답 제출
        GameResult result = submit_guess(&game, guess);

        switch(result.status) {
            case GUESS_CORRECT:
                printf("\n정답입니다!\n");
                printf("비밀 단어: %s\n", game.answer);
                printf("축하합니다! 게임에서 승리했습니다.\n");
                break;

            case GUESS_WRONG:
                printf("틀렸습니다!\n");
                printf("정답과의 유사도: %.2f\n",
                       result.similarity);
                break;

            case GUESS_DUPLICATE:
                printf("이미 제출한 단어입니다.\n");
                printf("제출 횟수는 차감되지 않습니다.\n");
                break;

            case GUESS_UNKNOWN:
                printf("등록되지 않았거나 유효한 벡터가 없는 단어입니다.\n");
                break;

            case GUESS_INVALID:
                printf("올바른 단어를 입력해주세요.\n");
                break;

            case GUESS_FINISHED:
                printf("이미 종료된 게임입니다.\n");
                break;

            case GUESS_ERROR:
                printf("유사도 계산 중 오류가 발생했습니다: %d\n",
                       result.engineStatus);
                break;

            default:
                printf("알 수 없는 오류가 발생했습니다.\n");
                break;
        }
    }

    // 패배 결과 출력
    if(game.finished && !game.won) {
        printf("\n모든 기회를 사용했습니다.\n");
        printf("정답은 %s입니다.\n", game.answer);
    }

    // 엔진 종료
    malmoi_close(engine);

    return 0;
}
