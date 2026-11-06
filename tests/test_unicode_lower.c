#include "../src/internal/unicode_lower.h"
#define CHECK(condition) do { if(!(condition)){fprintf(stderr,"Unicode lower check failed: %s\n",#condition);exit(1);} } while(0)
#include <stdio.h>
static void check(const char *s,const uint32_t *expected,int n){int32_t count=0;UChar32 *p=lower_codepoints(s,&count);CHECK(p&&count==n);CHECK(!memcmp(p,expected,(size_t)n*sizeof(*p)));free(p);}
int main(void){const uint32_t a[]={0x69,0x307},b[]={0x3bf,0x3c2},c[]={0x3bf,0x3c3,0x61},d[]={0x41};int32_t n;UChar32 *p;
check("\xc4\xb0",a,2);check("\xce\x9f\xce\xa3",b,2);check("\xce\x9f\xce\xa3" "A",c,3);p=lower_codepoints("A",&n);CHECK(p&&n==1&&p[0]==d[0]+32);free(p);CHECK(!lower_codepoints("\xc0\xaf",&n));CHECK(!lower_codepoints("\xed\xa0\x80",&n));CHECK(!lower_codepoints("\xf4\x90\x80\x80",&n));puts("Unicode lower tests passed");return 0;}
