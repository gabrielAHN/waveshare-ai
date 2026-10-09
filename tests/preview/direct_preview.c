/* HOST SIMULATION ONLY: no device or physical contact is exercised. */
#include <stdio.h>
#include "direct_input.h"
static uint16_t pixels[SPARKLES_PIXELS];
static void save(const char *path,const sparkles_state*s){
 assert(sparkles_render_direct(s,pixels,SPARKLES_PIXELS));FILE*f=fopen(path,"wb");assert(f);
 fprintf(f,"P6\n368 448\n255\n");for(int i=0;i<SPARKLES_PIXELS;i++){
  unsigned p=pixels[i];fputc(((p>>11)&31)*255/31,f);fputc(((p>>5)&63)*255/63,f);fputc((p&31)*255/31,f);
 }fclose(f);
}
int main(void){
 direct_input a={0};for(int i=1;i<=200;i++){
  direct_sample(&a,i*10000,0,0,0);if(i%5==0)sparkles_render_direct(&a.scene,pixels,SPARKLES_PIXELS);
 }
 save("evidence/direct-preview-idle.ppm",&a.scene);
 for(int i=201;i<=240;i++){
  direct_sample(&a,i*10000,1,260,210);if(i%5==0)sparkles_render_direct(&a.scene,pixels,SPARKLES_PIXELS);
  if(i==205)save("evidence/direct-preview-touch-50ms.ppm",&a.scene);
 }
 save("evidence/direct-preview-touch-400ms.ppm",&a.scene);
 for(int i=241;i<=260;i++)direct_sample(&a,i*10000,1,260,210-(i-240)*9);
 save("evidence/direct-preview-up.ppm",&a.scene);
 for(int i=261;i<=280;i++)direct_sample(&a,i*10000,1,260-(i-260)*12,30);
 save("evidence/direct-preview-left.ppm",&a.scene);
 for(int i=281;i<=380;i++)direct_sample(&a,i*10000,0,0,0);
 save("evidence/direct-preview-release.ppm",&a.scene);
 puts("host-only gradual touch/swipe/release previews produced");
}
