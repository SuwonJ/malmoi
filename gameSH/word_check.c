#include "wordgame.h"
#include <string.h>

/* '-', '^' 제거한 정규화 키 */
void make_key(const char *word, char *key){
    size_t n = 0;
    for(; *word && n + 1 < WORD_BYTES; word++)
        if(*word != '-' && *word != '^') key[n++] = *word;
    key[n] = '\0';
}

/* 한글 음절(U+AC00~U+D7A3)로만 구성되었는지 */
int is_hangul_word(const char *word){
    const unsigned char *p = (const unsigned char *)word;
    if(*p == '\0' || strlen(word) >= WORD_BYTES) return 0;
    for(; *p; p += 3){
        unsigned int cp;
        if((p[0] & 0xF0) != 0xE0 || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) return 0;
        cp = ((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
        if(cp < 0xAC00 || cp > 0xD7A3) return 0;
    }
    return 1;
}

int is_used_word(const Game *g, const char *key){
    char used[WORD_BYTES];
    for(int i = 0; i < g->recordCount; i++){
        make_key(g->records[i].word, used);
        if(strcmp(used, key) == 0) return 1;
    }
    return 0;
}

/* 상위 목록에서 순위 검색 (없으면 0) */
int find_rank(const Game *g, const char *key){
    for(size_t i = 0; i < g->rankingCount; i++)
        if(strcmp(g->ranking[i].key, key) == 0) return (int)i + 1;
    return 0;
}
