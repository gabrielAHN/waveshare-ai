#include <assert.h>
#include <stdio.h>
#include "home_render.h"
static uint16_t p[SPARKLES_PIXELS+2];
int main(void){
 home_ui s={0};home_sample(&s,10000,true,20,200);home_sample(&s,20000,false,0,0);assert(s.page==SPARKLES);
 /* The Home pull goes up from the bottom edge to centre; release never opens the Home card. */
 home_sample(&s,30000,true,180,440);home_sample(&s,40000,true,184,224);home_sample(&s,50000,false,0,0);assert(s.page==HOME);
 home_sample(&s,60000,true,300,200);home_sample(&s,70000,true,120,200);home_sample(&s,80000,false,0,0);
 assert(s.page==HOME&&s.tile==1&&s.drag_offset!=0);int offset=s.drag_offset;
 home_sample(&s,120000,false,0,0);assert(abs(s.drag_offset)<abs(offset)&&s.drag_offset!=0);
 home_sample(&s,1000000,false,0,0);assert(s.drag_offset==0);
 /* An out-and-back drag is NOT a tap. */
 home_sample(&s,1010000,true,160,200);home_sample(&s,1020000,true,190,200);home_sample(&s,1030000,true,160,200);home_sample(&s,1040000,false,0,0);assert(s.page==HOME);
 /* Service pages fill panel; neighbors remain wholly outside at rest. */
 for(int k=0;k<HOME_TILES;k++){s.tile=k;p[0]=p[SPARKLES_PIXELS+1]=0x9876;assert(home_render(&s,p+1,SPARKLES_PIXELS));assert(p[0]==0x9876&&p[SPARKLES_PIXELS+1]==0x9876);assert(p[1+200*368+18]!=sp_pack_lcd(181,207,213));}
 home_sample(&s,1100000,true,180,430);home_sample(&s,1110000,false,0,0);assert(s.page==HOME);  /* the bottom band opens no tile */
 home_sample(&s,1200000,true,345,200);home_sample(&s,1210000,false,0,0);assert(s.page==SETTINGS);
 puts("full-screen service targets, animated horizontal settling, no swipe-open, safe Home: PASS");
}
