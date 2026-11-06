/* sense_filter.h 규칙 테스트. 사전 인덱스 없이 바로 돌아가요. */
#include "../src/internal/sense_filter.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(x)                                                    \
    do {                                                            \
        if (!(x)) {                                                 \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);    \
            failures++;                                             \
        }                                                           \
    } while (0)

static int reference(const char *word, const char *definition) {
    return malmoi_sense_is_reference((const unsigned char *)word, strlen(word),
                                     (const unsigned char *)definition,
                                     strlen(definition));
}

int main(void) {
    const char text[] = "가을에 익는 열매";
    /* 1. 방언, 옛말, 북한어 뜻은 참조형 */
    CHECK(reference("감", "‘베’의 방언"));
    CHECK(reference("비", "‘베’의 방언(경상)."));
    CHECK(reference("가람", "‘강’의 옛말."));
    CHECK(reference("곽밥", "‘도시락’의 북한어."));
    /* 2. 작품 제목 뜻은 참조형 */
    CHECK(reference("별", "이병기가 작사하고 이교숙이 작곡의 가곡."));
    CHECK(reference("외출", "허진호 감독이 만든 영화."));
    CHECK(reference("님의 침묵", "한용운이 지은 시."));
    CHECK(reference("무정", "이광수가 지은 소설."));
    /* 3. 접사(앞이나 뒤가 '-')는 참조형 */
    CHECK(reference("비-", "‘아님’의 뜻을 더하는 접두사."));
    CHECK(reference("-껏", "‘그것이 닿는 데까지’의 뜻을 더하는 접미사."));
    /* 4. 보통 뜻은 참조형이 아님 */
    CHECK(!reference("비", "대기 중의 수증기가 물방울이 되어 떨어지는 현상."));
    CHECK(!reference("가랑-비", "가늘게 내리는 비."));
    CHECK(!reference("감", "감나무의 열매."));
    /* 5. 따옴표 없는 '방언'은 참조형이 아님 (방언을 설명하는 보통 뜻) */
    CHECK(!reference("사투리", "어느 한 지방에서만 쓰는 방언."));
    /* 6. 길이를 지켜요: 길이 밖에 있는 글자는 보지 않아요 */
    CHECK(malmoi_bytes_contain((const unsigned char *)text, strlen(text), "열매"));
    CHECK(!malmoi_bytes_contain((const unsigned char *)text, strlen("가을에"), "열매"));
    CHECK(!malmoi_bytes_contain((const unsigned char *)text, 0, "가"));
    CHECK(!malmoi_bytes_contain((const unsigned char *)text, strlen(text), ""));
    CHECK(!malmoi_sense_is_reference((const unsigned char *)"", 0,
                                     (const unsigned char *)"", 0));
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    puts("PASS sense filter rules (19 checks)");
    return 0;
}