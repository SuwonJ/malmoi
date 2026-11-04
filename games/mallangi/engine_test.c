
#include <stdio.h>
#include "similarity.h"

int main(void){
    Malmoi *engine = NULL;
    float score = 0.0f;

    // 1. 엔진 열기
    int status = malmoi_open(&engine);

    if(status != MALMOI_OK){
        printf("엔진 열기 실패: %s\n", malmoi_error_string(status));
        return 1;
    }

    printf("엔진 열기 성공!\n");

    // 2. 유사도 계산
    status = game_similarity(engine, "먼지잼", "이슬비", &score);

    if(status == MALMOI_OK){
        printf("유사도 계산 성공!\n");
        printf("코사인 유사도: %.6f\n", score);
    }
    else{
        printf("유사도 계산 실패: %s\n", malmoi_error_string(status));
    }

    // 3. 엔진 닫기
    malmoi_close(engine);

    printf("엔진 종료 완료!\n");

    return 0;
}
