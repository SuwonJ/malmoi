#include "malmoi.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    Malmoi *engine = NULL;
    MalmoiWord word;
    size_t total, i;
    int rc = malmoi_open(&engine);
    assert(rc == MALMOI_OK && engine);

    total = malmoi_word_count(engine);
    assert(total > 0);
    assert(malmoi_word_count(NULL) == 0);
    for (i = 0; i < total; i++) {
        rc = malmoi_word_at(engine, i, &word);
        assert(rc == MALMOI_OK);
        assert(word.id == i);
        assert(word.word && word.word[0]);
        assert(word.key && word.key[0]);
    }
    rc = malmoi_word_at(engine, total, &word);
    assert(rc == MALMOI_RANGE);
    assert(word.word == NULL && word.key == NULL && word.id == UINT32_MAX);
    assert(malmoi_word_at(NULL, 0, &word) == MALMOI_ARGUMENT);
    assert(malmoi_word_at(engine, 0, NULL) == MALMOI_ARGUMENT);

    malmoi_close(engine);
    return 0;
}
