#ifndef MALMOI_UNICODE_LOWER_H
#define MALMOI_UNICODE_LOWER_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "unicode_lower_data.h"
typedef uint32_t UChar32;
/* Validated UTF-8 only: reject overlong sequences, surrogates and > U+10FFFF. */
static UChar32 *lower_codepoints(const char *q, int32_t *count) {
    size_t bytes=strlen(q),n=0,i,j,used=0; uint32_t *input=malloc((bytes+1)*sizeof(*input)),*out;
    const unsigned char *p=(const unsigned char *)q;
    if(!input)return NULL;
    while(*p){uint32_t c=*p++;unsigned need=0,k;uint32_t minimum=0;
        if(c>=0xc2&&c<=0xdf){c&=31;need=1;minimum=0x80;}
        else if(c>=0xe0&&c<=0xef){c&=15;need=2;minimum=0x800;}
        else if(c>=0xf0&&c<=0xf4){c&=7;need=3;minimum=0x10000;}
        else if(c>=0x80)goto fail;
        for(k=0;k<need;k++){if((*p&0xc0)!=0x80)goto fail;c=(c<<6)|(*p++&63);}
        if(c<minimum||c>0x10ffff||(c>=0xd800&&c<=0xdfff))goto fail;
        input[n++]=c;
    }
    out=malloc((3*n+1)*sizeof(*out));if(!out)goto fail;
    for(i=0;i<n;i++){
        size_t lo=0,hi=sizeof(lower_mappings)/sizeof(*lower_mappings);uint32_t c=input[i];
        if(c==0x3a3){int before=0,after=0;size_t t;
            for(t=i;t>0;){uint32_t x=input[--t];int ignore=0,cased=0;size_t z;
                for(z=0;z<sizeof(ignorable_ranges)/sizeof(*ignorable_ranges);z++)if(x>=ignorable_ranges[z].first&&x<=ignorable_ranges[z].last){ignore=1;break;}
                if(ignore)continue;
                for(z=0;z<sizeof(cased_ranges)/sizeof(*cased_ranges);z++)if(x>=cased_ranges[z].first&&x<=cased_ranges[z].last){cased=1;break;}
                before=cased;break;
            }
            for(t=i+1;t<n;t++){uint32_t x=input[t];int ignore=0,cased=0;size_t z;
                for(z=0;z<sizeof(ignorable_ranges)/sizeof(*ignorable_ranges);z++)if(x>=ignorable_ranges[z].first&&x<=ignorable_ranges[z].last){ignore=1;break;}
                if(ignore)continue;
                for(z=0;z<sizeof(cased_ranges)/sizeof(*cased_ranges);z++)if(x>=cased_ranges[z].first&&x<=cased_ranges[z].last){cased=1;break;}
                after=cased;break;
            }
            out[used++]=(before&&!after)?0x3c2:0x3c3;continue;
        }
        while(lo<hi){size_t mid=lo+(hi-lo)/2;if(lower_mappings[mid].cp<c)lo=mid+1;else hi=mid;}
        if(lo<sizeof(lower_mappings)/sizeof(*lower_mappings)&&lower_mappings[lo].cp==c){for(j=0;j<lower_mappings[lo].length;j++)out[used++]=lower_mappings[lo].lower[j];}
        else out[used++]=c;
    }
    free(input);*count=(int32_t)used;return out;
fail:free(input);return NULL;
}
#define U8_APPEND_UNSAFE(s,i,c) do { uint32_t utf_cp=(uint32_t)(c); if(utf_cp<0x80){(s)[(i)++]=(char)utf_cp;}else if(utf_cp<0x800){(s)[(i)++]=(char)(0xc0|(utf_cp>>6));(s)[(i)++]=(char)(0x80|(utf_cp&63));}else if(utf_cp<0x10000){(s)[(i)++]=(char)(0xe0|(utf_cp>>12));(s)[(i)++]=(char)(0x80|((utf_cp>>6)&63));(s)[(i)++]=(char)(0x80|(utf_cp&63));}else{(s)[(i)++]=(char)(0xf0|(utf_cp>>18));(s)[(i)++]=(char)(0x80|((utf_cp>>12)&63));(s)[(i)++]=(char)(0x80|((utf_cp>>6)&63));(s)[(i)++]=(char)(0x80|(utf_cp&63));} } while(0)
#endif
