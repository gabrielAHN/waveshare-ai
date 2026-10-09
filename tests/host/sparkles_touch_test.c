#include <assert.h>
#include <stdio.h>
#include "sparkles.h"
/* sparkles_advance (fed by direct_input.h): press, exponential release, last contact held, clamps. */
int main(void){
 sparkles_state touch={.time=2};
 sparkles_advance(&touch,0,-.75f,-.5f,1);assert(touch.strength==1&&touch.touch_x==-.75f&&touch.touch_y==-.5f);
 float held_x=touch.touch_x,held_y=touch.touch_y;
 sparkles_advance(&touch,.6f,0,0,0);assert(touch.strength>.1f&&touch.strength<.25f);
 assert(touch.touch_x==held_x&&touch.touch_y==held_y);
 sparkles_advance(&touch,4,0,0,0);assert(touch.strength<.002f);
 for(int y=-1;y<=1;y+=2)for(int x=-1;x<=1;x+=2){sparkles_advance(&touch,0,x*2,y*2,1);assert(touch.touch_x==x&&touch.touch_y==y);}
 puts("press, exponential release, held last contact and four corner clamps PASS");
 return 0;
}
