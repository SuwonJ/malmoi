#ifndef MALMOI_DEMO_UTILS_H
#define MALMOI_DEMO_UTILS_H
#include "vecdb.h"
#include <stdio.h>
#include <stdlib.h>
#include "cli_args.h"

static VecDB *demo_open(int argc, char **argv) {
    VecDB *db = NULL;
    int rc;
    if (argc != 2) { fprintf(stderr, "usage: %s database.mlm\n", argv[0]); exit(2); }
    rc = vecdb_open(&db, argv[1]);
    if (rc) { fprintf(stderr, "%s\n", vecdb_status_string(rc)); exit(rc); }
    return db;
}
static void demo_check(int status, VecDB *db) {
    if (status) { fprintf(stderr, "%s\n", vecdb_status_string(status)); vecdb_close(db); exit(status); }
}
#endif
