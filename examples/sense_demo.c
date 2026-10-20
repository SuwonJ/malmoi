/* C integration: a caller must supply a query vector in the SAME space.
 * This demo's copied sense vector is a known-answer test, not a text encoder. */
#include "demo_utils.h"
MALMOI_MAIN {
    VecDB *db = demo_open(argc, argv);
    VecDBSense sense;
    VecDBWord word;
    MalmoiVector document, query;
    float score;
    demo_check(vecdb_sense(db, 0, &sense), db);
    demo_check(vecdb_word(db, sense.word_id, &word), db);
    demo_check(vecdb_sense_vector(db, sense.id, &document), db);
    query = document;
    demo_check(vecdb_cosine(&query, &document, &score), db);
    printf("sense_id=%u source=%s word_id=%u word=%s definition=%s self_cosine=%.6f\n",
           (unsigned)sense.id, sense.source_id, (unsigned)sense.word_id, word.word, sense.definition, (double)score);
    vecdb_close(db); return 0;
}
