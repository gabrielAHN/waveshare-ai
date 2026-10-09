#include <assert.h>
#include <stdio.h>
#include "home_render.h"
int main(void){uint16_t*a=malloc((SPARKLES_PIXELS+2)*2),*b=malloc((SPARKLES_PIXELS+2)*2);assert(a&&b);
 int xs[]={-290,-120,0,44,366,380},ys[]={-200,-12,0,94,440,450};
 for(unsigned xi=0;xi<sizeof xs/sizeof *xs;xi++)for(unsigned yi=0;yi<sizeof ys/sizeof *ys;yi++)for(int radius=0;radius<=18;radius+=18){
  for(int i=0;i<SPARKLES_PIXELS+2;i++)a[i]=b[i]=0x1357;
  int x=xs[xi],y=ys[yi],w=280,h=180;home_art(a+1,x,y,w,h,session_tile_art,radius);
  for(int yy=0;yy<h;yy++)for(int xx=0;xx<w;xx++){int px=x+xx,py=y+yy;if(px<0||px>=368||py<0||py>=448)continue;int dx=xx<radius?radius-xx:xx>=w-radius?xx-(w-radius-1):0,dy=yy<radius?radius-yy:yy>=h-radius?yy-(h-radius-1):0;if(!dx||!dy||dx*dx+dy*dy<=radius*radius)b[1+py*368+px]=session_tile_art[yy*w+xx];}
  assert(!memcmp(a,b,(SPARKLES_PIXELS+2)*2));
 }
 free(a);free(b);puts("Bulk tile blit matches pixel oracle across clipping/rounding and guards: PASS");}
