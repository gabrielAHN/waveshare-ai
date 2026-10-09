#include <assert.h>
#include <stdio.h>
#include "sparkles.h"
int main(void){
 sp_init();sp_glint(184,224,25,1,0,-.3f,1.6f,1);
 unsigned edge_energy=0,center=0;
 for(int v=0;v<10;v++)for(int y=0;y<SP_SPRITE;y++)for(int x=0;x<SP_SPRITE;x++){
  unsigned a=sp_sprites[v*SP_SPRITE*SP_SPRITE+y*SP_SPRITE+x];
  if(x<3||x>=SP_SPRITE-3||y<3||y>=SP_SPRITE-3)edge_energy+=a;
  if(x==80&&y==80)center+=a;
 }
 printf("sprite border energy=%u center=%u\n",edge_energy,center);fflush(stdout);
 assert(edge_energy==0&&center>2000);
 puts("all cached sprite variants have transparent feathered perimeter");
}
