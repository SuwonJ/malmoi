#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "daily.h"

#define DAILY_MAX_WORDS 1024
#define DAILY_WORD_BYTES 64

static const char *fallback[] = {"나무", "하늘", "바람", "바다", "꽃"};
static char words[DAILY_MAX_WORDS][DAILY_WORD_BYTES];
static int word_count;

int daily_index_kst(time_t now, int count) {
    if (count <= 0) return -1;
    int64_t seconds = (int64_t)now + 9 * 60 * 60;
    int64_t day = seconds / 86400;
    if (seconds < 0 && seconds % 86400) --day;
    int index = (int)(day % count);
    return index < 0 ? index + count : index;
}

static int load_words(const char *path) {
    FILE *file = fopen(path, "rb");
    char line[256];
    int count = 0, first = 1;
    if (!file) return 0;
    while (fgets(line, sizeof line, file)) {
        if (!strchr(line, '\n') && !feof(file)) goto invalid;
        char *word = line;
        if (first && strncmp(word, "\xef\xbb\xbf", 3) == 0) word += 3;
        first = 0;
        while (*word == ' ' || *word == '\t') ++word;
        size_t length = strlen(word);
        while (length && strchr(" \t\r\n", word[length - 1])) word[--length] = '\0';
        if (!length || *word == '#') continue;
        if (length >= DAILY_WORD_BYTES || count == DAILY_MAX_WORDS
                || strpbrk(word, " \t\r\n")) goto invalid;
        for (int i = 0; i < count; ++i)
            if (strcmp(words[i], word) == 0) goto invalid;
        memcpy(words[count++], word, length + 1);
    }
    if (ferror(file) || !count) goto invalid;
    fclose(file);
    word_count = count;
    return 1;
invalid:
    fclose(file);
    return 0;
}

const char *daily_answer_kst(time_t now) {
    const char *path = getenv("MALLANGI_WORDS");
    if (path && *path) {
        if (!word_count && !load_words(path)) {
            fprintf(stderr, "정답 후보 파일을 읽을 수 없습니다: %s\n", path);
            return NULL;
        }
        return words[daily_index_kst(now, word_count)];
    }
    return fallback[daily_index_kst(now, (int)(sizeof fallback / sizeof *fallback))];
}
