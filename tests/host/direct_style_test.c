#include <assert.h>
#include <stdio.h>
#include "sparkles.h"
int main(void){
 int r=149,g=187,b=201;sp_direct_tint(&r,&g,&b,0,0,180);assert(r==149&&g==187&&b==201);
 int r1=r,g1=g,b1=b;sp_direct_tint(&r1,&g1,&b1,1,0,180);assert(r1<r&&b1>b);
 r1=r;g1=g;b1=b;sp_direct_tint(&r1,&g1,&b1,-1,0,180);assert(r1>r&&b1<b);
 int rl=r,gl=g,bl=b,rr=r,gr=g,br=b;
 sp_direct_tint(&rl,&gl,&bl,0,1,0);sp_direct_tint(&rr,&gr,&br,0,1,367);assert(rl!=rr&&bl!=br);
 for(int t=-1;t<=1;t++)for(int q=-1;q<=1;q++){
  int a=250,c=250,d=250;sp_direct_tint(&a,&c,&d,t,q,367);assert(a>=0&&a<=255&&c>=0&&c<=255&&d>=0&&d<=255);
 }
 for(unsigned k=0;k<1000;k++)assert(abs(sp_grain(k))<=4);
 puts("bounded muted palette, directional gradient and bounded grain passed");
}
