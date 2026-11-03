#include "malmoi.h"
#include "cli_args.h"
#include <stdio.h>
#include <string.h>

/* This caller has no model name, database path, encoder type or dimension. */
MALMOI_MAIN {
    Malmoi *engine=NULL;
    MalmoiResult hits[10];
    size_t count=0,i;
    int rc;
    if (argc<3) {
        fprintf(stderr,"usage: malmoi_cli query TEXT | word WORD | pair A B\n"); return MALMOI_ARGUMENT;
    }
    rc=malmoi_open(&engine); if (rc) goto done;
    if (!strcmp(argv[1],"query") && argc==3) {
        rc=malmoi_search(engine,argv[2],hits,10,&count);
    } else if (!strcmp(argv[1],"word") && argc==3) {
        rc=malmoi_similar(engine,argv[2],hits,10,&count);
    } else if (!strcmp(argv[1],"pair") && argc==4) {
        float score;
        rc=malmoi_similarity(engine,argv[2],argv[3],&score);
        if (!rc) printf("%.9g\n",(double)score);
    } else rc=MALMOI_ARGUMENT;
    if (!rc) for (i=0;i<count;++i) printf("%u\t%s\t%.9g\n",(unsigned)hits[i].word_id,hits[i].key,(double)hits[i].score);
done:
    if (rc) fprintf(stderr,"%s\n",malmoi_error_string(rc));
    malmoi_close(engine); return rc;
}
