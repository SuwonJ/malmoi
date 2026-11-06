#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "malmoi.h"
#include "internal/search_backend.h"
#ifdef MALMOI_WITH_NATIVE_INDEX
#include "internal/native_lexical.h"
#endif
#ifdef MALMOI_WITH_E5
#include "internal/e5_encoder.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#elif !defined(_WIN32)
#include <unistd.h>
#endif
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#endif
struct Malmoi {
    VecDB *db;
    MalmoiSearch *search;
    char *path;
    int semantic;
#ifdef MALMOI_WITH_NATIVE_INDEX
    MalmoiNative *native_index;
#endif
#ifdef MALMOI_WITH_E5
    MalmoiE5 *encoder;
#endif
};
static char *copy(const char *s) {
    size_t n=strlen(s)+1;
    char *p=(char *)malloc(n);
    if (p) memcpy(p,s,n);
    return p;
}
/* MSVC warns on fopen under /WX; use its checked variant there. */
static FILE *open_read_binary(const char *path) {
#ifdef _MSC_VER
    FILE *f = NULL;
    return fopen_s(&f, path, "rb") == 0 ? f : NULL;
#else
    return fopen(path, "rb");
#endif
}
/* Prefer the full development dataset, then the data shipped beside the app. */
static char *default_database_path(void) {
    char executable[32768];
    char *slash;
    FILE *f=open_read_binary("data/full.mlm");
    if (f) { fclose(f); return copy("data/full.mlm"); }
    executable[0]='\0';
#ifdef _WIN32
    {
        wchar_t wide[32768];
        DWORD n=GetModuleFileNameW(NULL,wide,32768);
        if (n && n<32768)
            if (!WideCharToMultiByte(CP_UTF8,0,wide,-1,executable,sizeof(executable),NULL,NULL))
                executable[0]='\0';
    }
#elif defined(__APPLE__)
    {
        uint32_t size=sizeof(executable);
        if (_NSGetExecutablePath(executable,&size)) executable[0]='\0';
    }
#else
    {
        ssize_t n=readlink("/proc/self/exe",executable,sizeof(executable)-1);
        if (n>0) executable[n]='\0';
        else executable[0]='\0';
    }
#endif
    slash=strrchr(executable,'/');
#ifdef _WIN32
    {
        char *backslash=strrchr(executable,'\\');
        if (backslash && (!slash || backslash>slash)) slash=backslash;
    }
#endif
    if (slash && (size_t)(slash-executable)+sizeof("/data/dictionary.mlm")<sizeof(executable)) {
        memcpy(slash,"/data/dictionary.mlm",sizeof("/data/dictionary.mlm"));
        f=open_read_binary(executable);
        if (f) { fclose(f); return copy(executable); }
    }
    return copy("data/dictionary.mlm");
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
    if (!value || !value[0]) { free(owned); return default_database_path(); }
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
    return value && value[0] ? copy(value) : default_database_path();
#endif
}
void malmoi_close(Malmoi *m) {
    if (m) {
#ifdef MALMOI_WITH_NATIVE_INDEX
        malmoi_native_close(m->native_index);
#endif
        malmoi_search_close(m->search);
#ifdef MALMOI_WITH_E5
        malmoi_e5_close(m->encoder);
#endif
        vecdb_close(m->db);free(m->path);free(m);
    }
}
int malmoi_open(Malmoi **out) {
    Malmoi *m;
    char *path;
    int rc;
    const char *model=getenv("MALMOI_MODEL");
    const char *backend=getenv("MALMOI_SEARCH_BACKEND");
    MalmoiEncoder encoder={0};
    if (!out) return MALMOI_ARGUMENT;
    *out=NULL;
    if (backend && backend[0] && strcmp(backend,"native") && strcmp(backend,"native-index")) return MALMOI_CONFIG;
    if (backend && backend[0] && strcmp(backend,"native") && model && model[0]) return MALMOI_CONFIG;
    path=database_path();if (!path) return MALMOI_MEMORY;
    m=(Malmoi *)calloc(1,sizeof(*m));
    if (!m) {free(path);return MALMOI_MEMORY;}
    m->path=path;
    rc=vecdb_open(&m->db,path);
    if (!rc && model && model[0]) {
#ifdef MALMOI_WITH_E5
        rc=malmoi_e5_open(&m->encoder,path,model,vecdb_word_space(m->db),&encoder);
        if (!rc) m->semantic=1;
#else
        rc=MALMOI_CONFIG;
#endif
    }
    if (!rc) rc=malmoi_search_open(&m->search,m->db,m->semantic ? &encoder : NULL);
    if (!rc && backend && !strcmp(backend,"native-index")) {
#ifdef MALMOI_WITH_NATIVE_INDEX
        const char *index_dir=getenv("MALMOI_NATIVE_INDEX"),*model_dir=getenv("MALMOI_KIWI_MODEL");
        if (!index_dir || !index_dir[0] || !model_dir || !model_dir[0]) rc=MALMOI_CONFIG;
        else rc=malmoi_native_open(&m->native_index,index_dir,model_dir);
#else
        rc=MALMOI_UNSUPPORTED;
#endif
    }
    if (rc) {malmoi_close(m);return rc;}
    *out=m;return MALMOI_OK;
}
int malmoi_search(const Malmoi *m,const char *text,MalmoiResult *out,size_t k,size_t *count) {
#ifdef MALMOI_WITH_NATIVE_INDEX
    if (m && m->native_index) {
        MalmoiNativeHit hits[200];size_t n=0,i,limit=k<200?k:200;int rc;
        if (!count || (k && !out)) return MALMOI_ARGUMENT;
        *count=0;rc=malmoi_native_search(m->native_index,text,hits,limit,&n);
        if (rc) return rc;
        for (i=0;i<n;i++) {
            uint32_t id=UINT32_MAX;VecDBWord word;
            if(vecdb_find_word(m->db,hits[i].word,&id)==VECDB_OK && vecdb_word(m->db,id,&word)==VECDB_OK){out[i].word_id=id;out[i].word=word.word;out[i].key=word.key;}
            else {out[i].word_id=UINT32_MAX;out[i].word=hits[i].word;out[i].key=hits[i].word;}
            out[i].score=hits[i].score;
        }
        *count=n;return MALMOI_OK;
    }
#endif
    if (m && m->semantic) return malmoi_search_text(m->search,text,out,k,count);
    return malmoi_search_definitions(m ? m->search : NULL,text,out,k,count);
}
int malmoi_search_senses(const Malmoi *m,const char *text,MalmoiSenseResult *out,size_t k,size_t *count) {
    if(!count)return MALMOI_ARGUMENT;
    *count=0;
#ifdef MALMOI_WITH_NATIVE_INDEX
    if(m && m->native_index){
        MalmoiNativeHit hits[200];size_t n=0,i,limit=k<200?k:200;int rc;
        if(k && !out)return MALMOI_ARGUMENT;
        rc=malmoi_native_search(m->native_index,text,hits,limit,&n);if(rc)return rc;
        for(i=0;i<n;i++){out[i].row=hits[i].row;out[i].sense_id=hits[i].sense_id;out[i].word=hits[i].word;out[i].definition=hits[i].definition;out[i].word_type=hits[i].word_type;out[i].score=hits[i].score;}
        *count=n;return MALMOI_OK;
    }
#else
    (void)m;(void)text;(void)out;(void)k;
#endif
    return MALMOI_UNSUPPORTED;
}
size_t malmoi_word_count(const Malmoi *m) {
    return m ? vecdb_word_count(m->db) : 0;
}
int malmoi_word_at(const Malmoi *m, size_t index, MalmoiWord *out) {
    VecDBWord w;
    int rc;
    if (!out) return MALMOI_ARGUMENT;
    out->id = UINT32_MAX;
    out->word = NULL;
    out->key = NULL;
    if (!m) return MALMOI_ARGUMENT;
    if (index >= vecdb_word_count(m->db) || index > UINT32_MAX)
        return MALMOI_RANGE;
    rc = vecdb_word(m->db, (uint32_t)index, &w);
    if (rc != VECDB_OK) return rc;
    out->id = w.id;
    out->word = w.word;
    out->key = w.key;
    return MALMOI_OK;
}
const char *malmoi_data_path(const Malmoi *m) { return m ? m->path : NULL; }
const char *malmoi_search_mode(const Malmoi *m) {
    if (!m) return NULL;
#ifdef MALMOI_WITH_NATIVE_INDEX
    if (m->native_index) return "native-index";
#endif
    return m->semantic ? "e5-semantic" : "lexical";
}
int malmoi_similar(const Malmoi *m,const char *word,MalmoiResult *out,size_t k,size_t *count) {
    return malmoi_similar_words(m ? m->search : NULL,word,out,k,count);
}
int malmoi_similarity(const Malmoi *m,const char *a,const char *b,float *score) {
    return malmoi_word_similarity(m ? m->search : NULL,a,b,score);
}
const char *malmoi_error_string(int status) {
    return status==MALMOI_CONFIG ? "invalid encoder configuration" : vecdb_status_string(status);
}
