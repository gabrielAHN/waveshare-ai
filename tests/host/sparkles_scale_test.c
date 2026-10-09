#include <assert.h>
#include <stdio.h>
#include "sparkles.h"
int main(void){
 uint16_t *buffer=malloc((SPARKLES_PIXELS+2)*2);assert(buffer);
 for(unsigned i=0;i<SPARKLES_PIXELS+2;i++)buffer[i]=0xabcd;
 sparkles_state s={.time=2};assert(sparkles_render_direct(&s,buffer+1,SPARKLES_PIXELS));
 assert(buffer[0]==0xabcd&&buffer[SPARKLES_PIXELS+1]==0xabcd);
 unsigned differing=0;
 for(int y=0;y<SPARKLES_H;y+=2)for(int x=0;x<SPARKLES_W;x+=2){
  unsigned i=1+y*SPARKLES_W+x;
  differing+=buffer[i]!=buffer[i+1]||buffer[i]!=buffer[i+SPARKLES_W]||buffer[i]!=buffer[i+SPARKLES_W+1];
 }
 printf("non-replicated 2x2 blocks=%u\n",differing);fflush(stdout);assert(differing==0);
 free(buffer);puts("2x scene sampling (2x2 blocks) and guards PASS");
}
