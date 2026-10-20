#include "vecdb.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    VecDB *db = NULL;
    VecDBWord w;
    VecDBSense s;
    MalmoiVector a, b, query;
    uint32_t id, ids[2];
    size_t count, i;
    float score, values[8] = {1,0,0,0,0,0,0,0};
    assert(argc == 2);
    assert(vecdb_open(NULL, argv[1]) == VECDB_ARGUMENT);
    assert(vecdb_open(&db, NULL) == VECDB_ARGUMENT && db == NULL);
    assert(vecdb_word_count(NULL) == 0);
    assert(vecdb_word(NULL, 0, &w) == VECDB_ARGUMENT && w.id == UINT32_MAX);
    assert(vecdb_find_word(NULL, "key", &id) == VECDB_ARGUMENT && id == UINT32_MAX);
    vecdb_close(NULL);
    for (i = 0; i < 50; ++i) {
        assert(vecdb_open(&db, argv[1]) == VECDB_OK);
        assert(vecdb_word_count(db) == 1000 && vecdb_sense_count(db) == 1001);
        assert(vecdb_word_dimension(db) == 8 && vecdb_sense_dimension(db) == 8);
        assert(vecdb_word(db, 0, &w) == VECDB_OK && w.id == 0);
        assert(vecdb_word(db, UINT32_MAX, &w) == VECDB_RANGE && w.word == NULL);
        assert(vecdb_sense(db, UINT32_MAX, &s) == VECDB_RANGE);
        /* Homonyms use different word IDs; explicit polysemy shares one ID. */
        assert(vecdb_word(db, 2, &w) == VECDB_OK);
        assert(vecdb_find_word(db, w.key, &id) == VECDB_AMBIGUOUS && id == UINT32_MAX);
        assert(vecdb_find_words(db, w.key, ids, 2, &count) == VECDB_OK && count == 2);
        assert(ids[0] == 2 && ids[1] == 3);
        assert(vecdb_find_words(db, w.key, NULL, 0, &count) == VECDB_LIMIT && count == 2);
        assert(vecdb_find_words(db, w.key, ids, 1, &count) == VECDB_LIMIT && count == 2);
        assert(vecdb_find_word(db, "not-in-fixture", &id) == VECDB_NOT_FOUND);
        assert(vecdb_find_word(db, "", &id) == VECDB_ARGUMENT);
        assert(vecdb_find_word(db, "\xc0\xaf", &id) == VECDB_ARGUMENT);
        assert(vecdb_word_vector(db, 0, &a) == VECDB_OK);
        assert(vecdb_word_vector(db, 1, &b) == VECDB_OK);
        assert(vecdb_cosine(&a, &b, &score) == VECDB_OK && fabsf(score - .8f) < 1e-6f);
        assert(vecdb_dot(&a, &b, &score) == VECDB_OK && fabsf(score - .8f) < 1e-6f);
        query = a; query.dimension--;
        assert(vecdb_cosine(&a, &query, &score) == VECDB_SPACE && score == 0);
        query = a; query.space = "another-model-with-same-dimension";
        assert(vecdb_cosine(&a, &query, &score) == VECDB_SPACE);
        assert(vecdb_sense_vector(db, 0, &query) == VECDB_OK);
        assert(vecdb_cosine(&a, &query, &score) == VECDB_SPACE);
        query.data = values;
        assert(vecdb_cosine(&query, &query, &score) == VECDB_OK && score == 1);
        values[0] = NAN;
        assert(vecdb_cosine(&query, &query, &score) == VECDB_ARGUMENT);
        values[0] = 1;
        assert(vecdb_word_vector(db, 7, &b) == VECDB_OK);
        assert(vecdb_cosine(&a, &b, &score) == VECDB_ZERO_VECTOR && score == 0);
        assert(vecdb_word_vector(db, UINT32_MAX, &b) == VECDB_RANGE && b.data == NULL);
        assert(vecdb_sense(db, 0, &s) == VECDB_OK);
        assert(vecdb_word(db, s.word_id, &w) == VECDB_OK);
        assert(strstr(vecdb_metadata(db), "synthetic") != NULL);
        assert(strcmp(vecdb_status_string(VECDB_SPACE), "unknown status") != 0);
        vecdb_close(db); db = NULL;
    }
    puts("C core tests passed (50 open/close cycles)");
    return 0;
}
