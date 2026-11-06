#include "malmoi.h"
#include <stdio.h>
#include <string.h>
int main(void){Malmoi *e=NULL;MalmoiSenseResult r[20],repeat[20];MalmoiResult ordinary[20];char q[8194];size_t n,i,n2;int rc=malmoi_open(&e);if(rc){fprintf(stderr,"open %d\n",rc);return rc;}if(strcmp(malmoi_search_mode(e),"native-index"))return 1;
 while(fgets(q,sizeof(q),stdin)){size_t z=strlen(q);while(z&&(q[z-1]=='\n'||q[z-1]=='\r'))q[--z]=0;rc=malmoi_search_senses(e,q,r,20,&n);if(rc){fprintf(stderr,"search %d\n",rc);malmoi_close(e);return rc;}rc=malmoi_search_senses(e,q,repeat,20,&n2);if(rc||n!=n2){malmoi_close(e);return 1;}for(i=0;i<n;i++)if(r[i].word!=repeat[i].word||r[i].definition!=repeat[i].definition){malmoi_close(e);return 1;}rc=malmoi_search(e,q,ordinary,20,&n2);if(rc||n!=n2){malmoi_close(e);return 1;}for(i=0;i<n;i++){if(strcmp(r[i].word,ordinary[i].key)||r[i].score!=ordinary[i].score){malmoi_close(e);return 1;}printf("%u\t%lld\t%s\t%.9g\t%s\t%s\n",r[i].row,(long long)r[i].sense_id,r[i].word,(double)r[i].score,r[i].definition,r[i].word_type);}puts("---");}
 malmoi_close(e);return 0;}
