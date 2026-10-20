/* D integration: target and guess are word IDs in the shared word space. */
#include "demo_utils.h"
MALMOI_MAIN {
    VecDB *db = demo_open(argc, argv);
    MalmoiVector target, guess;
    float score;
    demo_check(vecdb_word_vector(db, 0, &target), db);
    demo_check(vecdb_word_vector(db, 1, &guess), db);
    demo_check(vecdb_cosine(&target, &guess, &score), db);
    printf("target_word_id=0 guess_word_id=1 cosine=%.6f (not a probability)\n", (double)score);
    vecdb_close(db); return 0;
}
