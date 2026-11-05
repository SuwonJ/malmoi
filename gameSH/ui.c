#include "wordgame.h"
#include <stdio.h>
#include <string.h>

/* 한 줄 입력. 너무 긴 입력은 버리고 빈 문자열 */
int read_line(char *buf, size_t cap){
    size_t len;
    int ch;
    if(!fgets(buf, (int)cap, stdin)) return 0;
    len = strcspn(buf, "\r\n");
    if(buf[len] == '\0' && len == cap - 1){
        while((ch = getchar()) != '\n' && ch != EOF){ }
        len = 0;
    }
    buf[len] = '\0';
    return 1;
}

/* 한글 2칸 기준 폭 맞춤 출력 */
static void print_padded(const char *s, int width){
    int cols = 0;
    for(const unsigned char *p = (const unsigned char *)s; *p; p++)
        if((*p & 0xC0) != 0x80) cols += (*p >= 0x80) ? 2 : 1;
    fputs(s, stdout);
    for(; cols < width; cols++) putchar(' ');
}

void print_status(const Game *g){
    printf("\n------------------------------------------\n");
    printf(" 기준 단어: [%s]   하한 유사도: %.4f\n", g->base, (double)g->threshold);
    printf(" P1 LIFE %d  |  P2 LIFE %d\n", g->life[0], g->life[1]);
    printf("------------------------------------------\n");
}

void print_submit(SubmitStatus s, const Record *r){
    switch(s){
    case SUBMIT_PASS:
    case SUBMIT_FAIL:
        printf("  유사도 %.4f", (double)r->score);
        if(r->rank) printf(" (%d위)", r->rank);
        puts(s == SUBMIT_PASS ? " → 성공!" : " → 미달, LIFE -1");
        break;
    case SUBMIT_BAD_FORMAT: puts("  한글 단어만 입력하세요."); break;
    case SUBMIT_HAS_BASE:   puts("  기준 단어(또는 이를 포함한 단어)는 쓸 수 없습니다."); break;
    case SUBMIT_DUPLICATE:  puts("  이미 제출된 단어입니다."); break;
    case SUBMIT_UNKNOWN:    puts("  사전에 없는 단어입니다."); break;
    default:                puts("  처리 오류"); break;
    }
}

void print_result(const Game *g){
    static const char *reason[] = { "", "LIFE 소진", "시간 초과", "최대 제출 수 도달" };

    printf("\n===== 결과 =====\n");
    if(g->winner < 0) printf("무승부 (%s)\n", reason[g->endReason]);
    else printf("P%d 승리! (P%d %s)\n", g->winner + 1, 2 - g->winner, reason[g->endReason]);

    printf("\n===== 게임 기록 =====\n");
    for(int i = 0; i < g->recordCount; i++){
        const Record *r = &g->records[i];
        printf("%3d. P%d  ", i + 1, r->player + 1);
        print_padded(r->word, 16);
        printf("  %.4f  ", (double)r->score);
        if(r->rank) printf("%5d위  ", r->rank); else printf("  순위밖  ");
        puts(r->passed ? "성공" : "실패");
    }
    for(int p = 0; p < PLAYERS; p++)
        printf("P%d: 성공 %d회, 남은 LIFE %d\n", p + 1, g->success[p], g->life[p]);
}
