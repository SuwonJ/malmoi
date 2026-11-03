#include "../src/internal/search_backend.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct { int mode; } Fake;
static int encoder(void *data,const char *text,float *out,size_t n) {
    Fake *f=(Fake *)data;
    size_t i;
    (void)text;
    if (f->mode==1) return VECDB_IO;
    for (i=0;i<n;++i) out[i]=0;
    if (f->mode==2) return VECDB_OK;
    out[0]=f->mode==3 ? NAN : 1;
    return VECDB_OK;
}
int main(int argc,char **argv) {
    VecDB *db=NULL;
    MalmoiSearch *s=NULL,*word_only=NULL;
    MalmoiResult hits[20];
    MalmoiEncoder e;
    Fake fake={0};
    size_t n,i,j;
    float score;
    assert(argc==2 && vecdb_open(&db,argv[1])==VECDB_OK);
    e.encode=encoder; e.userdata=&fake; e.space=vecdb_word_space(db);
    assert(malmoi_search_open(&s,db,&e)==VECDB_OK);
    assert(malmoi_search_open(&word_only,db,NULL)==VECDB_OK);
    assert(malmoi_search_text(s,"rain",hits,20,&n)==VECDB_OK && n==6);
    assert(hits[0].word_id==0 && hits[0].score==1);
    assert(hits[1].word_id==1 && fabsf(hits[1].score-.8f)<1e-6f);
    for (i=0;i<n;++i) {
        assert(hits[i].score>=-1 && hits[i].score<=1);
        for (j=0;j<i;++j) assert(strcmp(hits[i].key,hits[j].key)!=0);
        if (i) assert(hits[i-1].score>=hits[i].score);
    }
    hits[1].word_id=12345;
    assert(malmoi_search_text(s,"rain",hits,1,&n)==VECDB_OK && n==1 && hits[1].word_id==12345);
    assert(malmoi_search_text(s,"rain",NULL,0,&n)==VECDB_OK && n==0);
    assert(malmoi_search_text(s,"rain",NULL,1,&n)==VECDB_ARGUMENT && n==0);
    assert(malmoi_search_text(s," \t\n",hits,2,&n)==VECDB_ARGUMENT && n==0);
    assert(malmoi_search_text(s,"\xc0\xaf",hits,2,&n)==VECDB_ARGUMENT && n==0);
    assert(malmoi_search_text(word_only,"rain",hits,2,&n)==VECDB_UNSUPPORTED && n==0);
    assert(malmoi_similar_words(s,"먼지잼",hits,20,&n)==VECDB_OK && n==5);
    for (i=0;i<n;++i) assert(strcmp(hits[i].key,"먼지잼")!=0);
    assert(hits[0].word_id==1 && fabsf(hits[0].score-.8f)<1e-6f);
    assert(malmoi_similar_words(s,"눈",hits,20,&n)==VECDB_OK);
    for (i=0;i<n;++i) assert(strcmp(hits[i].key,"눈")!=0);
    assert(malmoi_similar_words(s,"영벡터",hits,1,&n)==VECDB_ZERO_VECTOR);
    assert(malmoi_similar_words(s,"missing",hits,1,&n)==VECDB_NOT_FOUND);
    assert(malmoi_word_similarity(s,"먼지-잼","이슬비",&score)==VECDB_OK && fabsf(score-.8f)<1e-6f);
    assert(malmoi_word_similarity(s,"눈","눈",&score)==VECDB_OK && fabsf(score-1)<1e-6f);
    assert(malmoi_word_similarity(s,"먼지잼","영벡터",&score)==VECDB_ZERO_VECTOR && score==0);
    assert(malmoi_word_similarity(s,"missing","눈",&score)==VECDB_NOT_FOUND && score==0);
    fake.mode=1; assert(malmoi_search_text(s,"rain",hits,10,&n)==VECDB_IO && n==0);
    fake.mode=2; assert(malmoi_search_text(s,"rain",hits,10,&n)==VECDB_ZERO_VECTOR && n==0);
    fake.mode=3; assert(malmoi_search_text(s,"rain",hits,10,&n)==VECDB_ARGUMENT && n==0);
    e.space="wrong-space";
    { MalmoiSearch *bad=NULL; assert(malmoi_search_open(&bad,db,&e)==VECDB_SPACE && bad==NULL); }
    malmoi_search_close(s); malmoi_search_close(word_only); malmoi_search_close(NULL); vecdb_close(db);
    puts("Search API tests passed"); return 0;
}
