#define _POSIX_C_SOURCE 200809L
#include "native_lexical.h"
#include "sha256.h"
#include <kiwi/capi.h>
#include "unicode_lower.h"
#include "native_platform.h"
#include "native_hybrid.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER 56U
#define MAX_QUERY 8192U
#define MAX_HITS 200U

typedef struct { unsigned char *p; size_t n; } Mapping;
typedef struct {
    Mapping terms, posts;
    uint64_t nterms, ndocs, nnz;
    const unsigned char *offsets, *blob, *pointers, *documents, *values, *idf;
} SparseIndex;
typedef struct Owned { char *word, *definition, *type; struct Owned *next; } Owned;
typedef struct { char *key,*word; uint32_t start,count; int has_vector; } NativeWord;
struct MalmoiNative {
    SparseIndex chr, bm;
    Mapping senses, lengths;
    const unsigned char *offsets, *blob;
    uint64_t ndocs;
    kiwi_h kiwi;
    pthread_mutex_t lock;
    int lock_initialized;
    Owned *owned;
    Owned **by_row;
    NativeWord *words; size_t word_count;
    uint32_t *word_rows,*row_words,*doc_posts,*bm_doc_posts;
    uint64_t *doc_ptr,*bm_doc_ptr; double *doc_inv,*bm_self;
    struct {uint32_t a,b;float score;int valid;} pair_cache[256];
    struct {uint32_t word,sense_row;float *scores;int valid;} similar_cache[4];
    struct {uint32_t row,guess;float score;int valid;} sense_pair_cache[256];
    size_t similar_next;
};
typedef struct { uint32_t row; float score; } Hit;
typedef struct { uint32_t row; double score; } Fused;
static uint32_t get32(const void *p) { uint32_t v; memcpy(&v,p,4); return v; }
static uint64_t get64(const void *p) { uint64_t v; memcpy(&v,p,8); return v; }
static float getfloat(const void *p) { float v; memcpy(&v,p,4); return v; }
static void unmap(Mapping *m) {
    if(m->p){
#ifdef _WIN32
        UnmapViewOfFile(m->p);
#else
        munmap(m->p,m->n);
#endif
    }m->p=NULL;m->n=0;
}
static int parse_hex(char c) { if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;return -1; }
static int manifest_hash(const char *dir,unsigned char out[32]) {
    char path[4096],buf[16385],*p,*end;FILE *f;size_t n,i;
    if(snprintf(path,sizeof(path),"%s/manifest.json",dir)>=(int)sizeof(path))return MALMOI_LIMIT;
    f=native_fopen(path,"rb");if(!f)return MALMOI_IO;n=fread(buf,1,sizeof(buf)-1,f);
    if(ferror(f)||(!feof(f)&&n==sizeof(buf)-1)){fclose(f);return MALMOI_FORMAT;}fclose(f);buf[n]=0;
    p=strstr(buf,"\"data_sha256\"");if(!p)return MALMOI_FORMAT;
    p=strchr(p,':');if(!p)return MALMOI_FORMAT;p++;
    while(*p==' '||*p=='\t')p++;
    if(*p++!='\"')return MALMOI_FORMAT;
    end=strchr(p,'\"');if(!end||end-p!=64)return MALMOI_FORMAT;
    for(i=0;i<32;i++){int a=parse_hex(p[i*2]),b=parse_hex(p[i*2+1]);if(a<0||b<0)return MALMOI_FORMAT;out[i]=(unsigned char)((a<<4)|b);}
    if(!strstr(buf,"\"format\": \"malmoi-native-lexical\"") || !strstr(buf,"\"corpus_scope\": \"all-valid-v1\""))return MALMOI_FORMAT;
    return MALMOI_OK;
}
static int map_section(const char *dir,const char *file,uint32_t kind,const unsigned char hash[32],Mapping *out) {
    char path[4096];
    if(snprintf(path,sizeof(path),"%s/%s",dir,file)>=(int)sizeof(path))return MALMOI_LIMIT;
#ifdef _WIN32
    {wchar_t *wide=native_wide(path);HANDLE f,mapping;LARGE_INTEGER size;
    if(!wide)return MALMOI_ARGUMENT;
    f=CreateFileW(wide,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);free(wide);
    if(f==INVALID_HANDLE_VALUE)return MALMOI_IO;
    if(!GetFileSizeEx(f,&size)||size.QuadPart<HEADER){CloseHandle(f);return MALMOI_FORMAT;}
    if((uint64_t)size.QuadPart>SIZE_MAX){CloseHandle(f);return MALMOI_LIMIT;}
    out->n=(size_t)size.QuadPart;
    mapping=CreateFileMappingW(f,NULL,PAGE_READONLY,0,0,NULL);CloseHandle(f);
    if(!mapping)return MALMOI_IO;
    out->p=MapViewOfFile(mapping,FILE_MAP_READ,0,0,0);CloseHandle(mapping);
    if(!out->p)return MALMOI_IO;}
#else
    {struct stat st;int fd=open(path,O_RDONLY);if(fd<0)return MALMOI_IO;
    if(fstat(fd,&st)||st.st_size<(off_t)HEADER){close(fd);return MALMOI_FORMAT;}
    out->n=(size_t)st.st_size;if((off_t)out->n!=st.st_size){close(fd);return MALMOI_LIMIT;}
    out->p=mmap(NULL,out->n,PROT_READ,MAP_PRIVATE,fd,0);close(fd);
    if(out->p==MAP_FAILED){out->p=NULL;return MALMOI_IO;}}
#endif
    if(memcmp(out->p,"MLMIDX01",8)||get32(out->p+8)!=1||get32(out->p+12)!=kind||memcmp(out->p+24,hash,32)){unmap(out);return MALMOI_FORMAT;}
    return MALMOI_OK;
}
static int verify_file(const char *dir,const char *name,uint64_t expected_size,const unsigned char expected[32]) {
    char path[4096];FILE *f;struct native_stat st;unsigned char actual[32],buf[65536];size_t n;MalmoiSha256 sha;
    if(snprintf(path,sizeof(path),"%s/%s",dir,name)>=(int)sizeof(path))return MALMOI_LIMIT;
    f=native_fopen(path,"rb");if(!f)return MALMOI_IO;
    if(native_fstat(native_fileno(f),&st)||st.st_size<0||(uint64_t)st.st_size!=expected_size){fclose(f);return MALMOI_FORMAT;}
    malmoi_sha256_init(&sha);while((n=fread(buf,1,sizeof(buf),f))>0)malmoi_sha256_update(&sha,buf,n);
    if(ferror(f)){fclose(f);return MALMOI_IO;}fclose(f);malmoi_sha256_final(&sha,actual);
    return memcmp(actual,expected,32)?MALMOI_FORMAT:MALMOI_OK;
}
static int verify_integrity(const char *dir,const char *model,const unsigned char hash[32]) {
    static const char *names[]={"bm25_lengths.bin","bm25_postings.bin","bm25_terms.bin","char_postings.bin","char_terms.bin","senses.bin",
        "combiningRule.txt","cong.mdl","default.dict","dialect.dict","extract.mdl","multi.dict","nounchr.mdl","sj.morph","typo.dict"};
    Mapping file={0};unsigned seen=0;uint64_t count,i;size_t pos=HEADER;int rc=map_section(dir,"integrity.bin",7,hash,&file);
    if(rc)return rc;
    count=get64(file.p+16);
    if(count!=15){rc=MALMOI_FORMAT;goto end;}
    for(i=0;i<count;i++){unsigned loc,len,j;char name[256];uint64_t size;
        if(pos+44>file.n){rc=MALMOI_FORMAT;goto end;}loc=file.p[pos];len=file.p[pos+1];
        if(file.p[pos+2]||file.p[pos+3]||!len||pos+44+len>file.n){rc=MALMOI_FORMAT;goto end;}
        memcpy(name,file.p+pos+44,len);name[len]=0;size=get64(file.p+pos+4);
        for(j=0;j<15;j++)if(!strcmp(name,names[j]))break;
        if(j==15||loc!=(j>=6)||(seen&(1U<<j))){rc=MALMOI_FORMAT;goto end;}seen|=1U<<j;
        rc=verify_file(loc?model:dir,name,size,file.p+pos+12);if(rc)goto end;pos+=44+len;
    }
    rc=(seen==((1U<<15)-1)&&pos==file.n)?MALMOI_OK:MALMOI_FORMAT;
end:unmap(&file);return rc;
}
static void index_close(SparseIndex *x){unmap(&x->terms);unmap(&x->posts);}
static int index_open(SparseIndex *x,const char *dir,const char *terms,const char *posts,uint32_t tk,uint32_t pk,const unsigned char hash[32]) {
    uint64_t needed,i,prev;int rc=map_section(dir,terms,tk,hash,&x->terms);if(rc)return rc;
    rc=map_section(dir,posts,pk,hash,&x->posts);if(rc)return rc;
    x->nterms=get64(x->terms.p+16);if(x->nterms!=get64(x->posts.p+16)||x->posts.n<72)return MALMOI_FORMAT;
    x->ndocs=get64(x->posts.p+56);x->nnz=get64(x->posts.p+64);
    if(x->nterms>(SIZE_MAX-80)/16||x->nnz>(SIZE_MAX-80-16*x->nterms)/8)return MALMOI_LIMIT;
    needed=HEADER+8*(x->nterms+1);if(needed>x->terms.n)return MALMOI_FORMAT;
    x->offsets=x->terms.p+HEADER;x->blob=x->terms.p+needed;
    if(get64(x->offsets+8*x->nterms)!=x->terms.n-needed)return MALMOI_FORMAT;
    prev=0;for(i=0;i<x->nterms;i++){uint64_t a=get64(x->offsets+8*i),b=get64(x->offsets+8*(i+1));
        if(a!=prev||b<a||b-a<4||b>x->terms.n-needed||get32(x->blob+a)>=x->nterms)return MALMOI_FORMAT;
        prev=b;}
    needed=72+8*(x->nterms+1)+8*x->nnz+8*x->nterms;if(needed!=x->posts.n)return MALMOI_FORMAT;
    x->pointers=x->posts.p+72;x->documents=x->pointers+8*(x->nterms+1);x->values=x->documents+4*x->nnz;x->idf=x->values+4*x->nnz+4*x->nterms;
    if(get64(x->pointers)!=0||get64(x->pointers+8*x->nterms)!=x->nnz)return MALMOI_FORMAT;
    prev=0;for(i=0;i<x->nterms;i++){uint64_t a=get64(x->pointers+8*i),b=get64(x->pointers+8*(i+1));if(a!=prev||b<a||b>x->nnz)return MALMOI_FORMAT;prev=b;}
    return MALMOI_OK;
}
static int native_catalog_build(MalmoiNative*m);
static void native_catalog_free(MalmoiNative*m);
static char *copy_n(const unsigned char *p,size_t n);
static void free_owned(Owned *p){while(p){Owned *n=p->next;free(p->word);free(p->definition);free(p->type);free(p);p=n;}}
void malmoi_native_close(MalmoiNative *m){if(!m)return;if(m->kiwi)kiwi_close(m->kiwi);index_close(&m->chr);index_close(&m->bm);unmap(&m->senses);unmap(&m->lengths);free_owned(m->owned);free(m->by_row);native_catalog_free(m);if(m->lock_initialized)pthread_mutex_destroy(&m->lock);free(m);}
int malmoi_native_open(MalmoiNative **out,const char *dir,const char *model){
    MalmoiNative *m;unsigned char hash[32];uint64_t i,prev,needed;int rc;
    if(!out||!dir||!model)return MALMOI_ARGUMENT;
    *out=NULL;
    if(*(const unsigned char*)&(uint16_t){1}!=1)return MALMOI_UNSUPPORTED;
    rc=manifest_hash(dir,hash);if(rc)return rc;
    rc=verify_integrity(dir,model,hash);if(rc)return rc;
    m=calloc(1,sizeof(*m));if(!m)return MALMOI_MEMORY;
    rc=index_open(&m->chr,dir,"char_terms.bin","char_postings.bin",1,2,hash);if(rc)goto fail;
    rc=index_open(&m->bm,dir,"bm25_terms.bin","bm25_postings.bin",3,4,hash);if(rc)goto fail;
    rc=map_section(dir,"senses.bin",5,hash,&m->senses);if(rc)goto fail;
    rc=map_section(dir,"bm25_lengths.bin",6,hash,&m->lengths);if(rc)goto fail;
    m->ndocs=get64(m->senses.p+16);
    if(m->ndocs!=m->chr.ndocs||m->ndocs!=m->bm.ndocs||m->ndocs!=get64(m->lengths.p+16)||m->ndocs>UINT32_MAX){rc=MALMOI_FORMAT;goto fail;}
    if(m->ndocs>SIZE_MAX/sizeof(*m->by_row)){rc=MALMOI_LIMIT;goto fail;}
    m->by_row=calloc((size_t)m->ndocs,sizeof(*m->by_row));if(m->ndocs&&!m->by_row){rc=MALMOI_MEMORY;goto fail;}
    if(m->ndocs>(SIZE_MAX-HEADER-8)/8){rc=MALMOI_LIMIT;goto fail;}
    needed=HEADER+8*(m->ndocs+1);if(needed>m->senses.n||HEADER+8+4*m->ndocs!=m->lengths.n){rc=MALMOI_FORMAT;goto fail;}
    m->offsets=m->senses.p+HEADER;m->blob=m->senses.p+needed;
    if(get64(m->offsets)!=0||get64(m->offsets+8*m->ndocs)!=m->senses.n-needed){rc=MALMOI_FORMAT;goto fail;}
    prev=0;for(i=0;i<m->ndocs;i++){uint64_t a=get64(m->offsets+8*i),b=get64(m->offsets+8*(i+1));if(a!=prev||b<a||b-a<24||b>m->senses.n-needed){rc=MALMOI_FORMAT;goto fail;}prev=b;}
    rc=native_catalog_build(m);if(rc)goto fail;
    if(strcmp(kiwi_version(),"0.24.0")){rc=MALMOI_CONFIG;goto fail;}
    m->kiwi=kiwi_init(model,4,KIWI_BUILD_DEFAULT,KIWI_DIALECT_STANDARD);if(!m->kiwi){fprintf(stderr,"malmoi: Kiwi: %s\n",kiwi_error());rc=MALMOI_CONFIG;goto fail;}
    if(pthread_mutex_init(&m->lock,NULL)){rc=MALMOI_IO;goto fail;}m->lock_initialized=1;*out=m;return MALMOI_OK;
fail:malmoi_native_close(m);return rc;
}
static int term_id(const SparseIndex*x,const char*s,size_t len){size_t lo=0,hi=(size_t)x->nterms;while(lo<hi){size_t mid=lo+(hi-lo)/2;uint64_t a=get64(x->offsets+8*mid),b=get64(x->offsets+8*(mid+1));size_t n=(size_t)(b-a)-4;int c=memcmp(x->blob+a+4,s,n<len?n:len);if(!c)c=(n>len)-(n<len);if(c<0)lo=mid+1;else hi=mid;}if(lo<x->nterms){uint64_t a=get64(x->offsets+8*lo),b=get64(x->offsets+8*(lo+1));if(b-a==len+4&&!memcmp(x->blob+a+4,s,len))return (int)get32(x->blob+a);}return -1;}
static int pyspace(UChar32 c){return(c>=9&&c<=13)||(c>=28&&c<=32)||c==0x85||c==0xa0||c==0x1680||(c>=0x2000&&c<=0x200a)||c==0x2028||c==0x2029||c==0x202f||c==0x205f||c==0x3000;}
static int cmp_u32(const void*a,const void*b){uint32_t x=*(const uint32_t*)a,y=*(const uint32_t*)b;return(x>y)-(x<y);}
static int char_scores(const SparseIndex*x,const char*q,float **out){int32_t n,i,j;UChar32 *cp=lower_codepoints(q,&n);uint32_t *counts=NULL,*seen=NULL;size_t seen_n=0,k;float *scores=NULL;double norm=0;int rc=MALMOI_MEMORY;if(!cp)return MALMOI_ARGUMENT;
 for(i=0,j=0;i<n;){if(pyspace(cp[i])){int32_t start=i;while(i<n&&pyspace(cp[i]))i++;cp[j++]=(i-start>=2)?32:cp[start];}else cp[j++]=cp[i++];}n=j;
 counts=calloc((size_t)x->nterms,sizeof(*counts));seen=malloc((size_t)x->nterms*sizeof(*seen));scores=calloc((size_t)x->ndocs,sizeof(*scores));if(!counts||!seen||!scores)goto end;
 for(i=0;i<n;i++){char token[20];int32_t p=0,ii;for(ii=i;ii<n&&ii<i+4;ii++){U8_APPEND_UNSAFE(token,p,cp[ii]);if(ii-i+1>=2){int id=term_id(x,token,(size_t)p);if(id>=0){if(!counts[id])seen[seen_n++]=(uint32_t)id;counts[id]++;}}}}
 qsort(seen,seen_n,sizeof(*seen),cmp_u32);for(k=0;k<seen_n;k++){uint32_t id=seen[k];float tf=(float)(1.0+log((double)counts[id]));float w=tf*getfloat(x->idf+4*id);if(!isfinite(w)){rc=MALMOI_FORMAT;goto end;}norm+=(double)w*w;}
 if(norm>0){float den=(float)sqrt(norm);for(k=0;k<seen_n;k++){uint32_t id=seen[k];float tf=(float)(1.0+log((double)counts[id]));float w=(tf*getfloat(x->idf+4*id))/den;uint64_t a=get64(x->pointers+8*id),b=get64(x->pointers+8*(id+1)),z;for(z=a;z<b;z++){uint32_t row=get32(x->documents+4*z);float value=getfloat(x->values+4*z);if(row>=x->ndocs||!isfinite(value)){rc=MALMOI_FORMAT;goto end;}scores[row]+=w*value;}}}
 *out=scores;scores=NULL;rc=MALMOI_OK;
end:free(cp);free(counts);free(seen);free(scores);return rc;}
static char *lower_utf8(const char*s){int32_t n,i,p=0;UChar32 *cp=lower_codepoints(s,&n);char*out;if(!cp)return NULL;out=malloc((size_t)n*4+1);if(!out){free(cp);return NULL;}for(i=0;i<n;i++)U8_APPEND_UNSAFE(out,p,cp[i]);out[p]=0;free(cp);return out;}
static int bm_scores(const SparseIndex*x,kiwi_h kiwi,const char*q,float **out){kiwi_analyze_option_t opt={0};kiwi_res_h r=NULL;int i,n,rc=MALMOI_MEMORY;uint32_t *seen=calloc((size_t)x->nterms,sizeof(*seen)),*ids=malloc((size_t)x->nterms*sizeof(*ids));size_t nids=0,k;float *s=calloc((size_t)x->ndocs,sizeof(*s));if(!seen||!ids||!s)goto end;opt.match_options=KIWI_MATCH_ALL;opt.dialect_cost=3;opt.typo_threshold=2.5f;r=kiwi_analyze(kiwi,q,1,opt,NULL);if(!r){rc=MALMOI_FORMAT;goto end;}n=kiwi_res_word_num(r,0);
 for(i=0;i<n;i++){const char*tag=kiwi_res_tag(r,0,i),*form=kiwi_res_form(r,0,i);char *lower;int id;if(!tag||!form){rc=MALMOI_FORMAT;goto end;}if(tag[0]=='S'||tag[0]=='Z')continue;lower=lower_utf8(form);if(!lower)goto end;id=term_id(x,lower,strlen(lower));free(lower);if(id>=0&&!seen[id]){seen[id]=1;ids[nids++]=(uint32_t)id;}}
 qsort(ids,nids,sizeof(*ids),cmp_u32);for(k=0;k<nids;k++){uint64_t a=get64(x->pointers+8*ids[k]),b=get64(x->pointers+8*(ids[k]+1)),z;for(z=a;z<b;z++){uint32_t row=get32(x->documents+4*z);float value=getfloat(x->values+4*z);if(row>=x->ndocs||!isfinite(value)){rc=MALMOI_FORMAT;goto end;}s[row]+=value;}}
 *out=s;s=NULL;rc=MALMOI_OK;
end:if(r)kiwi_res_close(r);free(ids);free(seen);free(s);return rc;}
static int cmp_hit(const void*a,const void*b){const Hit*x=a,*y=b;if(x->score>y->score)return -1;if(x->score<y->score)return 1;return(x->row>y->row)-(x->row<y->row);}
static int top100(const float*s,uint64_t n,Hit out[100],size_t *count){Hit *h=malloc((size_t)n*sizeof(*h));size_t i,k=0;if(!h)return MALMOI_MEMORY;for(i=0;i<n;i++)if(s[i]>0){h[k].row=(uint32_t)i;h[k++].score=s[i];}qsort(h,k,sizeof(*h),cmp_hit);*count=k<100?k:100;memcpy(out,h,*count*sizeof(*h));free(h);return MALMOI_OK;}
static int cmp_fused(const void*a,const void*b){const Fused*x=a,*y=b;if(x->score>y->score)return -1;if(x->score<y->score)return 1;return(x->row>y->row)-(x->row<y->row);}
static char *copy_n(const unsigned char *p,size_t n){char *s=malloc(n+1);if(s){memcpy(s,p,n);s[n]=0;}return s;}
int malmoi_native_search(MalmoiNative*m,const char*q,MalmoiNativeHit*out,size_t k,size_t*count){float *cs=NULL,*bs=NULL;Hit ch[100],bh[100];Fused f[MAX_HITS];MalmoiNativeHit temp[MAX_HITS];size_t nc=0,nb=0,nf=0,printed=0,i;int rc;
 if(!count)return MALMOI_ARGUMENT;
 *count=0;
 if(!m||!q||(!out&&k))return MALMOI_ARGUMENT;
 {const unsigned char *p=(const unsigned char *)q;int content=0;while(*p){if(*p>=0x80||(*p!=' '&&*p!='\t'&&*p!='\r'&&*p!='\n'))content=1;p++;}if(!content)return MALMOI_ARGUMENT;}
 if(strlen(q)>MAX_QUERY)return MALMOI_LIMIT;
 if(!k)return MALMOI_OK;
 if(pthread_mutex_lock(&m->lock))return MALMOI_IO;
 rc=char_scores(&m->chr,q,&cs);if(rc)goto end;rc=bm_scores(&m->bm,m->kiwi,q,&bs);if(rc)goto end;rc=top100(cs,m->ndocs,ch,&nc);if(rc)goto end;rc=top100(bs,m->ndocs,bh,&nb);if(rc)goto end;
 for(i=0;i<nc;i++){f[nf].row=ch[i].row;f[nf++].score=exp(-(double)i/10.0);}for(i=0;i<nb;i++){size_t j;for(j=0;j<nf;j++)if(f[j].row==bh[i].row)break;if(j==nf){f[nf].row=bh[i].row;f[nf++].score=0;}f[j].score+=exp(-(double)i/10.0);}qsort(f,nf,sizeof(*f),cmp_fused);
 for(i=0;i<nf&&printed<k;i++){uint64_t a=get64(m->offsets+8*f[i].row),endoff=get64(m->offsets+8*(f[i].row+1)),sid;const unsigned char*p=m->blob+a;uint32_t lens[4];size_t j,total=24;Owned *node;
  if(endoff<a||endoff-a<24){rc=MALMOI_FORMAT;goto end;}sid=get64(p);for(j=0;j<4;j++){lens[j]=get32(p+8+4*j);total+=lens[j];}if(total!=endoff-a){rc=MALMOI_FORMAT;goto end;}
  for(j=0;j<printed;j++)if(strlen(temp[j].word)==lens[0]&&!memcmp(temp[j].word,p+24,lens[0]))break;
  if(j<printed)continue;
  node=m->by_row[f[i].row];if(!node){node=calloc(1,sizeof(*node));if(!node){rc=MALMOI_MEMORY;goto end;}node->word=copy_n(p+24,lens[0]);node->definition=copy_n(p+24+lens[0]+lens[1],lens[2]);node->type=copy_n(p+24+lens[0]+lens[1]+lens[2],lens[3]);if(!node->word||!node->definition||!node->type){free_owned(node);rc=MALMOI_MEMORY;goto end;}node->next=m->owned;m->owned=node;m->by_row[f[i].row]=node;}
  temp[printed].row=f[i].row;temp[printed].sense_id=(int64_t)sid;temp[printed].word=node->word;temp[printed].definition=node->definition;temp[printed].word_type=node->type;temp[printed].score=(float)f[i].score;printed++;
 }
 if(printed)memcpy(out,temp,printed*sizeof(*out));
 *count=printed;
 rc=MALMOI_OK;
end:free(cs);free(bs);pthread_mutex_unlock(&m->lock);return rc;}

#include "native_words.inc"
