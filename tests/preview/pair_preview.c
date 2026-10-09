/* Host preview of every Settings screen (tests/host/settings_states.h) -> $PREVIEW_DIR/settings-*.ppm
 * (default evidence/). Build paths are owned by tools/render_docs_previews.sh.
 * Then /usr/bin/python3 tests/preview/settings_sheet.py. Simulation only: renderer pixels, not a photo of the AMOLED. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../host/settings_states.h"
static uint16_t px[SPARKLES_PIXELS];
int main(void){
 for(int i=0;i<SETTINGS_STATES;i++){
  home_ui s;const char*name=settings_state(i,&s);
  const char*dir=getenv("PREVIEW_DIR");char path[512];snprintf(path,sizeof path,"%s/settings-%s.ppm",dir?dir:"evidence",name);
  assert(home_render(&s,px,SPARKLES_PIXELS));FILE*f=fopen(path,"wb");assert(f);fprintf(f,"P6\n368 448\n255\n");
  for(int k=0;k<SPARKLES_PIXELS;k++){unsigned p=px[k];fputc(((p>>11)&31)*255/31,f);fputc(((p>>5)&63)*255/63,f);fputc((p&31)*255/31,f);}
  fclose(f);printf("%s\n",path);
 }
 return 0;
}
