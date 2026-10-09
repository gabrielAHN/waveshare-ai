#include <assert.h>
#include <stdio.h>
#include "home_render.h"
static uint16_t p[SPARKLES_PIXELS];
static void save(home_ui*s,const char*name){
 assert(home_render(s,p,SPARKLES_PIXELS));char path[160];snprintf(path,sizeof path,"evidence/ui-%s.ppm",name);FILE*f=fopen(path,"wb");assert(f);fprintf(f,"P6\n368 448\n255\n");
 for(int i=0;i<SPARKLES_PIXELS;i++){unsigned char rgb[]={(p[i]>>11)*255/31,((p[i]>>5)&63)*255/63,(p[i]&31)*255/31};fwrite(rgb,1,3,f);}fclose(f);
}
int main(void){home_ui s={0};const char*n[]={"home-sparkles","home-ask","home-settings"};for(int i=0;i<3;i++){s.tile=i;save(&s,n[i]);}
 s.tile=1;s.drag_offset=150;save(&s,"home-transition");
 s=(home_ui){.page=SPARKLES,.session_off=false};home_sample(&s,1000000,false,0,0);save(&s,"idle");
 home_sample(&s,1010000,true,40,200);home_sample(&s,1020000,true,320,200);home_sample(&s,1120000,false,0,0);save(&s,"drag");
 home_sample(&s,1820000,false,0,0);save(&s,"release");home_sample(&s,5000000,false,0,0);save(&s,"decayed");
}
