#include "malmoi_bridge.h"
#include <stdio.h>
#include <string.h>

/* 원형 사전(build/fixtures/ring.mlm)으로 다리 놓기를 검사해요.
 * 단어 12개가 원 위에 30도 간격으로 놓여 있어서, 이웃 2개는 항상 양옆 단어예요.
 *        비
 *  소나기    빗물
 * 무지개        시냇물
 * 햇빛            강물
 *  하늘        바다
 *    구름    파도
 *        바람
 */

static int failures = 0;

/* 조건이 거짓이면 파일 이름, 줄 번호, 조건을 출력하고 실패 수를 하나 늘려요. */
#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

/* 모든 테스트가 쓰는 옵션: 이웃 2개(양옆 단어)만 보기 */
static MalmoiBridgeOptions ring_options(void) {
    MalmoiBridgeOptions options;
    malmoi_bridge_default_options(&options);
    options.neighbors = 2;
    return options;
}

/* 경로의 단어(key)가 expected와 순서까지 똑같은지 확인해요. */
static int same_path(const MalmoiStep *path, size_t count,
                     const char *const *expected, size_t expected_count) {
    size_t i;
    if (count != expected_count) {
        return 0;
    }
    for (i = 0; i < count; ++i) {
        if (strcmp(path[i].key, expected[i]) != 0) {
            return 0;
        }
    }
    return 1;
}

/* 1. 기본 경로: 비 → 바다는 시계 방향 4칸 */
static void test_basic_path(const Malmoi *engine) {
    MalmoiBridgeOptions options = ring_options();
    MalmoiStep path[16];
    size_t count = 0;
    const char *expected[] = {"비", "빗물", "시냇물", "강물", "바다"};
    CHECK(malmoi_bridge(engine, "비", "바다", &options, path, 16, &count) == MALMOI_OK);
    CHECK(same_path(path, count, expected, 5));
}

/* 2. 반대 방향: 비 → 하늘은 반시계 방향 4칸 */
static void test_other_side(const Malmoi *engine) {
    MalmoiBridgeOptions options = ring_options();
    MalmoiStep path[16];
    size_t count = 0;
    const char *expected[] = {"비", "소나기", "무지개", "햇빛", "하늘"};
    CHECK(malmoi_bridge(engine, "비", "하늘", &options, path, 16, &count) == MALMOI_OK);
    CHECK(same_path(path, count, expected, 5));
}

/* 3. 바로 옆 단어: 2칸, 두 번째 칸의 유사도는 cos 30도 = 약 0.866 */
static void test_adjacent(const Malmoi *engine) {
    MalmoiBridgeOptions options = ring_options();
    MalmoiStep path[16];
    size_t count = 0;
    const char *expected[] = {"비", "빗물"};
    CHECK(malmoi_bridge(engine, "비", "빗물", &options, path, 16, &count) == MALMOI_OK);
    CHECK(same_path(path, count, expected, 2));
    CHECK(count == 2 && path[1].score > 0.86f && path[1].score < 0.87f);
}

/* 4. 같은 단어: 출발 = 도착이면 1칸짜리 경로, 출발 칸의 점수는 1 */
static void test_same_word(const Malmoi *engine) {
    MalmoiBridgeOptions options = ring_options();
    MalmoiStep path[16];
    size_t count = 0;
    CHECK(malmoi_bridge(engine, "비", "비", &options, path, 16, &count) == MALMOI_OK);
    CHECK(count == 1);
    CHECK(count == 1 && strcmp(path[0].key, "비") == 0 && path[0].score == 1.0f);
}

/* 5. 정반대 단어: 비 → 바람은 어느 쪽으로 가도 6칸이라, 칸 수만 확인 */
static void test_opposite(const Malmoi *engine) {
    MalmoiBridgeOptions options = ring_options();
    MalmoiStep path[16];
    size_t count = 0;
    CHECK(malmoi_bridge(engine, "비", "바람", &options, path, 16, &count) == MALMOI_OK);
    CHECK(count == 7);
}

/* 6. 사전에 없는 단어: 오류를 그대로 알려 주고 count는 0 */
static void test_unknown_word(const Malmoi *engine) {
    MalmoiBridgeOptions options = ring_options();
    MalmoiStep path[16];
    size_t count = 99;
    CHECK(malmoi_bridge(engine, "비", "고양이", &options, path, 16, &count) == MALMOI_NOT_FOUND);
    CHECK(count == 0);
}

/* 7. 칸 수 제한: 최대 2칸이면 4칸 떨어진 바다에 못 감 → 오류 아님, count 0 */
static void test_max_steps(const Malmoi *engine) {
    MalmoiBridgeOptions options = ring_options();
    MalmoiStep path[16];
    size_t count = 99;
    options.max_steps = 2;
    CHECK(malmoi_bridge(engine, "비", "바다", &options, path, 16, &count) == MALMOI_OK);
    CHECK(count == 0);
}

/* 8. 탐색 한도: 단어 3개만 확정할 수 있으면 6칸 떨어진 바람까지 못 감 → LIMIT */
static void test_max_visits(const Malmoi *engine) {
    MalmoiBridgeOptions options = ring_options();
    MalmoiStep path[16];
    size_t count = 99;
    options.max_visits = 3;
    CHECK(malmoi_bridge(engine, "비", "바람", &options, path, 16, &count) == MALMOI_LIMIT);
    CHECK(count == 0);
}

/* 9. 결과 배열 부족: 경로는 5칸인데 배열이 3칸 → RANGE */
static void test_small_capacity(const Malmoi *engine) {
    MalmoiBridgeOptions options = ring_options();
    MalmoiStep path[3];
    size_t count = 99;
    CHECK(malmoi_bridge(engine, "비", "바다", &options, path, 3, &count) == MALMOI_RANGE);
    CHECK(count == 0);
}

/* 10. 잘못된 인자: engine이나 count가 NULL이면 ARGUMENT */
static void test_null_arguments(const Malmoi *engine) {
    MalmoiBridgeOptions options = ring_options();
    MalmoiStep path[16];
    size_t count = 99;
    CHECK(malmoi_bridge(NULL, "비", "바다", &options, path, 16, &count) == MALMOI_ARGUMENT);
    CHECK(count == 0);
    CHECK(malmoi_bridge(engine, "비", "바다", &options, path, 16, NULL) == MALMOI_ARGUMENT);
    CHECK(malmoi_bridge(engine, NULL, "바다", &options, path, 16, &count) == MALMOI_ARGUMENT);
}

/* 11. 옵션 NULL: 기본값(이웃 10개)으로도 비 → 바다를 찾아야 해요 */
static void test_default_options(const Malmoi *engine) {
    MalmoiStep path[16];
    size_t count = 0;
    CHECK(malmoi_bridge(engine, "비", "바다", NULL, path, 16, &count) == MALMOI_OK);
    CHECK(count >= 2 && strcmp(path[count - 1].key, "바다") == 0);
}

int main(void) {
    Malmoi *engine = NULL;
    if (malmoi_open(&engine) != MALMOI_OK) {
        fprintf(stderr, "MALMOI_DATA로 ring.mlm을 지정하세요\n");
        return 1;
    }
    test_basic_path(engine);
    test_other_side(engine);
    test_adjacent(engine);
    test_same_word(engine);
    test_opposite(engine);
    test_unknown_word(engine);
    test_max_steps(engine);
    test_max_visits(engine);
    test_small_capacity(engine);
    test_null_arguments(engine);
    test_default_options(engine);
    malmoi_close(engine);
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("bridge tests passed (11 tests)\n");
    return 0;
}