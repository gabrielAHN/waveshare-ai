#include <stdio.h>
#include "home_render.h"
static uint16_t px[SPARKLES_PIXELS];
static void save(const char*path,home_ui*s){assert(home_render(s,px,SPARKLES_PIXELS));FILE*f=fopen(path,"wb");fprintf(f,"P6\n368 448\n255\n");
 for(int i=0;i<SPARKLES_PIXELS;i++){unsigned p=px[i];fputc(((p>>11)&31)*255/31,f);fputc(((p>>5)&63)*255/63,f);fputc((p&31)*255/31,f);}fclose(f);}
int main(void){home_ui s={.page=SETTINGS};strcpy(s.credentials.ssid,"home-net");s.saved=true;s.connected=true;strcpy(s.status,"Connected");strcpy(s.ip,"192.0.2.33");s.pair.state=PAIR_ENROLLED_UNPAIRED;
 save("evidence/density-settings-on.ppm",&s);s.session_off=true;save("evidence/density-settings-off.ppm",&s);{settings_buttons b=home_settings_buttons(&s,0);int k=home_settings_find(&b,SA_SPARKLE);printf("session row y=%d h=%d\n",b.t[k].y,b.t[k].h);}return 0;}
