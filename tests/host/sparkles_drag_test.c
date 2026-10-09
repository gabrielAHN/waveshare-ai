#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "home_ui.h"
static int64_t now;
static void sample(home_ui *s,bool down,int x,int y){home_sample(s,now+=10000,down,x,y);}
int main(void){
 home_ui s={0};s.page=SPARKLES;s.session_off=false;/* calm: touch only */
 /* Full scene routing, not only direct_input: no live session or network required. */
 sample(&s,true,1,1);
 float prior_x=s.input.scene.touch_x;
 for(int x=2;x<368;x++){
  sample(&s,true,x,100);
  assert(s.page==SPARKLES&&s.input.down);
  assert(s.input.scene.touch_x>prior_x);prior_x=s.input.scene.touch_x;
 }
 assert(s.input.scene.gradient==1&&s.input.scene.strength>.99f);
 /* Reverse and vertical swipes remain continuous rather than waiting for UP. */
 for(int x=366;x>=0;x--){sample(&s,true,x,100);assert(s.input.scene.touch_x<prior_x);prior_x=s.input.scene.touch_x;}
 assert(s.input.scene.gradient<-.99f);
 for(int y=101;y<448;y++){sample(&s,true,180,y);assert(s.input.down&&s.page==SPARKLES);}
 assert(s.input.scene.touch_y==1&&s.input.scene.theme==-1);
 float hold=s.input.scene.hold,tail=s.input.scene.strength,theme=s.input.scene.theme;
 sample(&s,false,0,0);assert(!s.input.down&&s.input.scene.strength<tail);
 assert(s.input.scene.touch_y==1&&s.input.scene.hold==hold);
 /* UP from the bottom edge is the Home pull: it never paints (it decides on release). */
 sample(&s,true,180,440);sample(&s,true,184,224);
 assert(!s.input.down&&s.input.scene.theme==theme);
 sample(&s,false,184,224);
 assert(s.page==HOME&&!s.input.down&&s.input.scene.theme==theme);
 /* A new normal touch does not bridge old coordinates or change the palette. */
 s.page=SPARKLES;sample(&s,true,350,50);assert(s.input.scene.theme==theme);
 sample(&s,true,351,49);assert(s.input.scene.theme>theme);
 puts("continuous full-screen drag/reverse/vertical routing and bottom Home: PASS");
}
