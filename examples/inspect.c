/* Portable machine-readable dump used by Python/C compatibility tests. */
#include "vecdb.h"
#include <stdio.h>
#include "cli_args.h"

static void json_string(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    putchar('"');
    for (; *p; ++p) {
        if (*p == '"' || *p == '\\') { putchar('\\'); putchar(*p); }
        else if (*p < 0x20) printf("\\u%04x", (unsigned)*p);
        else putchar(*p);
    }
    putchar('"');
}
static void vector(const MalmoiVector *v) {
    size_t j;
    putchar('[');
    for (j = 0; j < v->dimension; ++j) printf("%s%.9g", j ? "," : "", (double)v->data[j]);
    putchar(']');
}
MALMOI_MAIN {
    VecDB *db = NULL;
    size_t i;
    int rc;
    if (argc != 2) { fprintf(stderr, "usage: inspect database.mlm\n"); return 2; }
    rc = vecdb_open(&db, argv[1]);
    if (rc) { fprintf(stderr, "%s\n", vecdb_status_string(rc)); return rc; }
    printf("{\"word_dimension\":%zu,\"sense_dimension\":%zu,\"metadata_json\":",
           vecdb_word_dimension(db), vecdb_sense_dimension(db));
    json_string(vecdb_metadata(db)); printf(",\"word_space\":");
    json_string(vecdb_word_space(db)); printf(",\"sense_space\":"); json_string(vecdb_sense_space(db));
    printf(",\"words\":[");
    for (i = 0; i < vecdb_word_count(db); ++i) {
        VecDBWord w;
        MalmoiVector v = {NULL, 0, NULL};
        (void)vecdb_word(db, (uint32_t)i, &w);
        (void)vecdb_word_vector(db, (uint32_t)i, &v);
        printf("%s{\"id\":%u,\"word\":", i ? "," : "", (unsigned)w.id); json_string(w.word);
        printf(",\"key\":"); json_string(w.key); printf(",\"lexeme_id\":"); json_string(w.lexeme_id);
        printf(",\"vector\":"); vector(&v); putchar('}');
    }
    printf("],\"senses\":[");
    for (i = 0; i < vecdb_sense_count(db); ++i) {
        VecDBSense s;
        MalmoiVector v = {NULL, 0, NULL};
        (void)vecdb_sense(db, (uint32_t)i, &s);
        (void)vecdb_sense_vector(db, (uint32_t)i, &v);
        printf("%s{\"id\":%u,\"word_id\":%u,\"source_id\":", i ? "," : "", (unsigned)s.id, (unsigned)s.word_id);
        json_string(s.source_id); printf(",\"definition\":"); json_string(s.definition);
        printf(",\"pos\":"); json_string(s.pos); printf(",\"vector\":"); vector(&v); putchar('}');
    }
    printf("]}\n"); vecdb_close(db);
    return 0;
}
