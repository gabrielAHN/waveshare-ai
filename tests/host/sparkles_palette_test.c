#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "sparkles.h"
int main(void){
 static uint16_t a[SPARKLES_PIXELS];sparkles_state s={.time=2};assert(sparkles_render_direct(&s,a,SPARKLES_PIXELS));
 /* LCD order: R5 G6 B5 from the high bits down */
 double r=0,g=0,b=0;for(int i=0;i<SPARKLES_PIXELS;i++){r+=(a[i]>>11)*255./31;g+=((a[i]>>5)&63)*255./63;b+=(a[i]&31)*255./31;}
 r/=SPARKLES_PIXELS;g/=SPARKLES_PIXELS;b/=SPARKLES_PIXELS;printf("RGB mean %.2f %.2f %.2f (reference ~163,197,207)\n",r,g,b);fflush(stdout);
 assert(r>140 && r<190 && g>177 && g<222 && b>190 && b<230);
 return 0;
}
