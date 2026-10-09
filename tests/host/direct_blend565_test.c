#include <assert.h>
#include <stdio.h>
#include "sparkles.h"
int main(void){
 for(unsigned a=0;a<256;a++)for(unsigned v=0;v<65536;v+=131){
  unsigned f=0xf7bc,w=(a+4)>>3;if(w>32)w=32;
  unsigned r=((((v>>11)&31)*(32-w)+((f>>11)&31)*w)>>5);
  unsigned g=((((v>>5)&63)*(32-w)+((f>>5)&63)*w)>>5);
  unsigned b=(((v&31)*(32-w)+(f&31)*w)>>5);
  assert(sp_blend565(v,f,a)==((r<<11)|(g<<5)|b));
 }
 assert(sp_blend565(0xf800,0x001f,0)==0xf800);assert(sp_blend565(0xf800,0x001f,255)==0x001f);
 puts("packed RGB565 alpha blend agrees with scalar channel oracle for all alpha values");
}
