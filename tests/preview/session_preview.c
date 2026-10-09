/* Host-only simulation of the actual native framebuffer renderer. */
#include <stdio.h>
#include "home_render.h"
static void save(home_ui*s,const char*name){
 uint16_t*p=malloc(SPARKLES_PIXELS*2);assert(home_render(s,p,SPARKLES_PIXELS));char path[160];snprintf(path,sizeof path,"evidence/session-preview-%s.ppm",name);
 FILE*f=fopen(path,"wb");assert(f);fprintf(f,"P6\n368 448\n255\n");for(int i=0;i<SPARKLES_PIXELS;i++){unsigned v=p[i];fputc((v>>11)*255/31,f);fputc(((v>>5)&63)*255/63,f);fputc((v&31)*255/31,f);}fclose(f);free(p);
}
int main(void){home_ui s={0};save(&s,"home");s.drag_offset=-170;save(&s,"swipe");s.drag_offset=0;s.tile=1;save(&s,"tile-settings");s.page=SETTINGS;save(&s,"settings-no-wifi");s.saved=s.connected=true;strcpy(s.credentials.ssid,"Example network");s.pair.state=PAIR_ENROLLED_UNPAIRED;save(&s,"settings");s.page=SPARKLES;s.input.scene.time=3;save(&s,"sparkles-idle");
 s.input.stamp_us=3000000;for(int i=0;i<200;i++)direct_sample(&s.input,3010000+i*10000,true,120,160);save(&s,"sparkles-hold");for(int i=0;i<30;i++)direct_sample(&s.input,5010000+i*10000,false,0,0);save(&s,"sparkles-release");puts("Native host framebuffer simulations saved; NOT physical screenshots");}
