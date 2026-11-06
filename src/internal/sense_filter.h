#ifndef MALMOI_SENSE_FILTER_H
#define MALMOI_SENSE_FILTER_H
#include <stddef.h>
#include <string.h>

/* 뜻 걸러내기 규칙 (유사어, 단어 쌍 점수용).
 * 참조형 뜻: 다른 단어를 가리키기만 하는 뜻(방언, 옛말, 북한어), 노래, 영화,
 * 시, 소설 같은 작품 제목 뜻, 접사(비-, -껏). 이런 뜻끼리는 뜻풀이가 똑같아서
 * 서로 다른 단어인데도 점수가 1이 나와요. 그래서 단어에 다른 뜻이 있으면
 * 이 뜻들은 비교에서 빼요. 뜻풀이는 NUL로 끝나지 않으니 길이를 함께 받아요. */
static int malmoi_bytes_contain(const unsigned char *text, size_t length,
                                const char *pattern) {
    size_t n = strlen(pattern), i;
    if (!n || n > length) return 0;
    for (i = 0; i + n <= length; ++i) {
        if (text[i] == (unsigned char)pattern[0] && !memcmp(text + i, pattern, n)) return 1;
    }
    return 0;
}

static int malmoi_sense_is_reference(const unsigned char *word, size_t word_length,
                                     const unsigned char *definition, size_t definition_length) {
    static const char *const patterns[] = {
        "’의 방언", "’의 옛말", "’의 북한어",
        "작곡의 ", "감독이 만든", "지은 시", "지은 소설",
        NULL
    };
    size_t i;
    if (word_length && (word[0] == '-' || word[word_length - 1] == '-')) return 1;
    for (i = 0; patterns[i]; ++i) {
        if (malmoi_bytes_contain(definition, definition_length, patterns[i])) return 1;
    }
    return 0;
}
#endif