#include "lexical.h"
#include "vecdb.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static int order(const void *a,const void *b) {
    uint64_t x=*(const uint64_t *)a,y=*(const uint64_t *)b;
    return (x>y)-(x<y);
}
void malmoi_grams_free(MalmoiGrams *v) {free(v->grams);memset(v,0,sizeof(*v));}
/* Input is already validated UTF-8 by the loader or public search validation.
 * Ignore ASCII punctuation/spacing and common Unicode punctuation.
 * Unicode normalization is the caller's NFC responsibility. */
int malmoi_grams(const char *text,MalmoiGrams *out) {
    const unsigned char *s=(const unsigned char *)text;
    size_t bytes=strlen(text),n=0,i,j,k;
    uint32_t *points;
    double norm=0;
    memset(out,0,sizeof(*out));
    if (bytes>=SIZE_MAX/sizeof(uint32_t) || bytes>SIZE_MAX/(3*sizeof(uint64_t))) return VECDB_LIMIT;
    points=(uint32_t *)malloc((bytes+1)*sizeof(uint32_t));
    if (!points) return VECDB_MEMORY;
    while (*s) {
        uint32_t c=*s++;
        if (c>=128) {
            unsigned need=c<224 ? 1U : c<240 ? 2U : 3U;
            c&=(1U<<(6U-need))-1U;
            while (need--) c=(c<<6)|(*s++&63U);
        }
        if (c<128) {
            if (c>='A' && c<='Z') c+='a'-'A';
            if (!((c>='a' && c<='z') || (c>='0' && c<='9'))) continue;
        } else if (c==0x3000 || c==0xa0 || (c>=0x2000 && c<=0x206f) || (c>=0x3001 && c<=0x303f)) continue;
        points[n++]=c;
    }
    if (n) {
        out->grams=(uint64_t *)malloc(n*3*sizeof(uint64_t));
        if (!out->grams) {free(points);return VECDB_MEMORY;}
        for (i=0;i<n;++i) {
            uint64_t hash=UINT64_C(14695981039346656037);
            for (j=0;j<3 && i+j<n;++j) {
                hash^=points[i+j];hash*=UINT64_C(1099511628211);
                out->grams[out->count++]=hash;
            }
        }
        qsort(out->grams,out->count,sizeof(uint64_t),order);
        for (i=0;i<out->count;i=j) {
            for (j=i+1;j<out->count && out->grams[j]==out->grams[i];++j) {}
            k=j-i;norm+=(double)k*(double)k;
        }
        out->inv_norm=1.0/sqrt(norm);
    }
    free(points);return VECDB_OK;
}
float malmoi_grams_cosine(const MalmoiGrams *a,const MalmoiGrams *b) {
    size_t i=0,j=0,ai,bj;
    double dot=0,score;
    while (i<a->count && j<b->count) {
        if (a->grams[i]<b->grams[j]) {++i;continue;}
        if (a->grams[i]>b->grams[j]) {++j;continue;}
        ai=i;bj=j;
        while (i<a->count && a->grams[i]==a->grams[ai]) ++i;
        while (j<b->count && b->grams[j]==b->grams[bj]) ++j;
        dot+=(double)(i-ai)*(double)(j-bj);
    }
    score=dot*a->inv_norm*b->inv_norm;
    return (float)(score>1 ? 1 : score);
}
