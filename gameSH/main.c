#include "wordgame.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* 기준 단어 후보 (유사도 분포를 확인해 선정) */
static const char *BASE_POOL[] = {
    "어머니", "아버지", "저녁", "아침", "가을", "나그네", "노래", "이야기",
    "바다", "눈물", "웃음", "고양이", "밥", "강아지", "동무", "마을"
};
#define BASE_POOL_SIZE ((int)(sizeof(BASE_POOL) / sizeof(BASE_POOL[0])))

/* 한 턴 진행. 입력 종료(EOF) 시 0 */
static int play_turn(Game *g){
    char word[WORD_BYTES * 2];
    int player = g->turn;
    time_t start = time(NULL);

    print_status(g);
    while(g->turn == player && !g->finished){
        int left = TURN_SECONDS - (int)difftime(time(NULL), start);
        SubmitStatus s;
        Record *r;

        if(left <= 0){ game_timeout(g); break; }
        printf("P%d 차례 (남은 시간 %d초) > ", player + 1, left);
        fflush(stdout);
        if(!read_line(word, sizeof(word))) return 0;

        if(difftime(time(NULL), start) > TURN_SECONDS){   /* 입력 완료 시점 기준 */
            puts("시간 초과!");
            game_timeout(g);
            break;
        }
        s = game_submit(g, word, &r);
        print_submit(s, r);
    }
    return 1;
}

int main(void){
    Malmoi *engine = NULL;
    Game game;
    char line[16];
    int status;

    puts("사전 불러오는 중...");
    status = malmoi_open(&engine);
    if(status != MALMOI_OK){
        fprintf(stderr, "사전 열기 실패: %s\n", malmoi_error_string(status));
        return 1;
    }
    srand((unsigned int)time(NULL));

    do{
        /* 테스트용: DUEL_BASE 환경 변수로 기준 단어 고정 */
        const char *base = getenv("DUEL_BASE");
        if(base == NULL || base[0] == '\0') base = BASE_POOL[rand() % BASE_POOL_SIZE];

        puts("기준 단어 준비 중...");
        status = game_init(&game, engine, base);
        if(status != MALMOI_OK){
            fprintf(stderr, "기준 단어 '%s' 준비 실패: %s\n", base, malmoi_error_string(status));
            break;
        }

        printf("\n===== 단어 유사도 대결 =====\n");
        printf("LIFE %d, 턴당 %d초, 기준 단어와 상위 %d위 이내면 성공\n",
               START_LIFE, TURN_SECONDS, RANK_LIMIT);

        while(!game.finished){
            if(!play_turn(&game)){ game_free(&game); malmoi_close(engine); return 0; }
        }
        print_result(&game);
        game_free(&game);

        printf("\n다시 하시겠습니까? (y/n) > ");
        fflush(stdout);
    }while(read_line(line, sizeof(line)) && (line[0] == 'y' || line[0] == 'Y'));

    malmoi_close(engine);
    return 0;
}
