#include "malmoi.h"
#include "internal/search_backend.h"
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#endif
struct Malmoi { VecDB *db; MalmoiSearch *search; };
static char *copy(const char *s) {
    size_t n=strlen(s)+1;
    char *p=(char *)malloc(n);
    if (p) memcpy(p,s,n);
    return p;
}
static char *database_path(void) {
#ifdef _WIN32
    const wchar_t *value;
    wchar_t *owned=NULL;
    char *result=NULL;
    int n;
#ifdef _MSC_VER
    size_t length=0;
    if (_wdupenv_s(&owned,&length,L"MALMOI_DATA")!=0) return NULL;
    value=owned;
#else
    value=_wgetenv(L"MALMOI_DATA");
#endif
    if (!value || !value[0]) { free(owned); return copy("data/dictionary.mlm"); }
    n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value,-1,NULL,0,NULL,NULL);
    if (n) {
        result=(char *)malloc((size_t)n);
        if (result && !WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value,-1,result,n,NULL,NULL)) {
            free(result);result=NULL;
        }
    }
    free(owned); return result;
#else
    const char *value=getenv("MALMOI_DATA");
    return copy(value && value[0] ? value : "data/dictionary.mlm");
#endif
}
void malmoi_close(Malmoi *m) {
    if (m) { malmoi_search_close(m->search);vecdb_close(m->db);free(m); }
}
int malmoi_open(Malmoi **out) {
    Malmoi *m;
    char *path;
    int rc;
    if (!out) return MALMOI_ARGUMENT;
    *out=NULL;
    path=database_path();if (!path) return MALMOI_MEMORY;
    m=(Malmoi *)calloc(1,sizeof(*m));
    if (!m) {free(path);return MALMOI_MEMORY;}
    rc=vecdb_open(&m->db,path);free(path);
    if (!rc) rc=malmoi_search_open(&m->search,m->db,NULL);
    if (rc) {malmoi_close(m);return rc;}
    *out=m;return MALMOI_OK;
}
int malmoi_search(const Malmoi *m,const char *text,MalmoiResult *out,size_t k,size_t *count) {
    return malmoi_search_definitions(m ? m->search : NULL,text,out,k,count);
}
int malmoi_similar(const Malmoi *m,const char *word,MalmoiResult *out,size_t k,size_t *count) {
    return malmoi_similar_words(m ? m->search : NULL,word,out,k,count);
}
int malmoi_similarity(const Malmoi *m,const char *a,const char *b,float *score) {
    return malmoi_word_similarity(m ? m->search : NULL,a,b,score);
}
const char *malmoi_error_string(int status) {return vecdb_status_string(status);}
