#include <assert.h>
#include <stdio.h>
#include "sparkles.h"
#include "adapter.h"
int main(void){
 uint16_t *guard=malloc((SPARKLES_PIXELS+2)*2);
 assert(guard);for(int i=0;i<SPARKLES_PIXELS+2;i++)guard[i]=0xdead;
 sparkles_state s={.time=1.2f};
 assert(sparkles_render_direct(&s,guard+1,SPARKLES_PIXELS));
 assert(guard[0]==0xdead&&guard[SPARKLES_PIXELS+1]==0xdead);
 assert(!sparkles_render_direct(&s,guard+1,SPARKLES_PIXELS-1));
 assert(sp_pack_lcd(255,0,0)==0xf800);assert(sp_pack_lcd(0,255,0)==0x07e0);assert(sp_pack_lcd(0,0,255)==0x001f);
 uint16_t *shadow=calloc(SPARKLES_PIXELS,2);uint8_t packed[8]={0};shadow[0]=0xf800;shadow[1]=0x07e0;shadow[368]=0x001f;shadow[369]=0xffff;
 pack_window(shadow,packed,0,0,2,2);const uint8_t expect[]={0xf8,0,7,0xe0,0,0x1f,0xff,0xff};assert(!memcmp(packed,expect,8));
 free(shadow);free(guard);puts("direct RGB565 sentinels, capacity and guards passed");
}
