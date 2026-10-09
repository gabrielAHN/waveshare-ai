#include <stdio.h>
#include <assert.h>
#include "sparkles.h"
int main(void){sparkles_state s={.time=3};uint16_t*p=malloc(SPARKLES_PIXELS*2);assert(p&&sparkles_render_direct(&s,p,SPARKLES_PIXELS));FILE*f=fopen("evidence/session-tile-source.ppm","wb");assert(f);fprintf(f,"P6\n368 448\n255\n");for(int i=0;i<SPARKLES_PIXELS;i++){unsigned v=p[i];fputc((v>>11)*255/31,f);fputc(((v>>5)&63)*255/63,f);fputc((v&31)*255/31,f);}fclose(f);free(p);}
