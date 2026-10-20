/* B integration: caller owns lookup/hash/Top-K; this only accesses core views. */
#include "demo_utils.h"
MALMOI_MAIN {
    VecDB *db = demo_open(argc, argv);
    VecDBWord word;
    MalmoiVector a, b;
    uint32_t id;
    float score;
    demo_check(vecdb_word(db, 0, &word), db);
    demo_check(vecdb_find_word(db, word.key, &id), db);
    demo_check(vecdb_word_vector(db, id, &a), db);
    demo_check(vecdb_word_vector(db, 1, &b), db);
    demo_check(vecdb_cosine(&a, &b, &score), db);
    printf("word_id=%u raw=%s key=%s cosine_with_word_1=%.6f\n", (unsigned)id, word.word, word.key, (double)score);
    vecdb_close(db); return 0;
}
