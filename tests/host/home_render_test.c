#include <assert.h>
#include <stdio.h>
#include "home_render.h"
int main(void){
 uint16_t *a=malloc((SPARKLES_PIXELS+2)*sizeof *a),*b=malloc(SPARKLES_PIXELS*sizeof *b);assert(a&&b);a[0]=123;a[SPARKLES_PIXELS+1]=456;
 home_ui s={0};assert(!home_render(&s,a+1,SPARKLES_PIXELS-1));assert(!home_render(NULL,a+1,SPARKLES_PIXELS));assert(!home_render(&s,NULL,SPARKLES_PIXELS));
 static const home_page pages[]={HOME,SPARKLES,SETTINGS,HELPER,SENSORS};
 for(unsigned k=0;k<sizeof pages/sizeof *pages;k++)for(int wifi=0;wifi<3;wifi++){
  s.page=pages[k];s.saved=wifi>0;s.connected=wifi>1;s.drag_offset=-280;assert(home_render(&s,a+1,SPARKLES_PIXELS));assert(a[0]==123&&a[SPARKLES_PIXELS+1]==456);
 }
 /* The Wi-Fi password is never drawn: two different passwords render identical pixels. */
 s.page=SETTINGS;s.saved=s.connected=true;strcpy(s.credentials.ssid,"net");strcpy(s.credentials.password,"aaaaaaaa");assert(home_render(&s,a+1,SPARKLES_PIXELS));
 strcpy(s.credentials.password,"bbbbbbbb");assert(home_render(&s,b,SPARKLES_PIXELS));assert(!memcmp(a+1,b,SPARKLES_PIXELS*sizeof *b));
 free(a);free(b);puts("render capacity/null guards, clipped moving tiles, every page x Wi-Fi state, password never drawn: PASS");
}
