#include "malmoi_bridge.h"
#include "cli_args.h"
#include <stdio.h>

MALMOI_MAIN {
    Malmoi *engine = NULL;
    MalmoiBridgeOptions options;
    MalmoiStep path[16];
    size_t count = 0, i;
    int rc;
    if (argc != 3) {
        fprintf(stderr, "usage: bridge_cli FROM TO\n");
        return MALMOI_ARGUMENT;
    }
    malmoi_bridge_default_options(&options);
    rc = malmoi_open(&engine);
    if (rc) goto done;
    rc = malmoi_bridge(engine, argv[1], argv[2], &options, path, 16, &count);
    if (rc) goto done;
    if (count == 0) {
        printf("연결을 찾지 못했습니다.\n");
        goto done;
    }
    printf("%s", path[0].word);
    for (i = 1; i < count; ++i)
        printf(" -> %s (%.3f)", path[i].word, (double)path[i].score);
    printf("\n");
done:
    if (rc) fprintf(stderr, "%s\n", malmoi_error_string(rc));
    malmoi_close(engine);
    return rc;
}