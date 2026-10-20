#ifndef MALMOI_CLI_ARGS_H
#define MALMOI_CLI_ARGS_H
/* UTF-8 command line contract on both Unix and Windows. */
#include <stdlib.h>
static int malmoi_main(int argc, char **argv);
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
int wmain(int argc, wchar_t **wide) {
    char **args = (char **)calloc((size_t)argc + 1, sizeof(char *));
    int i, result = 2;
    if (!args) return 2;
    for (i = 0; i < argc; ++i) {
        int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide[i], -1, NULL, 0, NULL, NULL);
        if (!n) goto done;
        args[i] = (char *)malloc((size_t)n);
        if (!args[i] || !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide[i], -1, args[i], n, NULL, NULL)) goto done;
    }
    result = malmoi_main(argc, args);
done:
    for (i = 0; i < argc; ++i) free(args[i]);
    free(args);
    return result;
}
#else
int main(int argc, char **argv) { return malmoi_main(argc, argv); }
#endif
#define MALMOI_MAIN static int malmoi_main(int argc, char **argv)
#endif
