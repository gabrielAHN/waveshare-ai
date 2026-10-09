#include <assert.h>
#include <stdio.h>
#include "direct_input.h"
int main(void){
 direct_input a={0};
 direct_sample(&a,1000000,1,300,200);
 assert(fabsf(a.scene.time-1)<1e-6&&a.scene.strength>.99f&&a.scene.strength<1);
 direct_sample(&a,1010000,1,50,60);
 assert(fabsf(a.scene.touch_x-(50.f/367*2-1))<1e-6);
 direct_sample(&a,1020000,0,0,0);
 assert(a.scene.strength>.96f&&a.scene.strength<1);
 assert(fabsf(a.scene.touch_y-(60.f/447*2-1))<1e-6);
 /* A short contact survives in the snapshot even if no render saw DOWN. */
 direct_sample(&a,1100000,0,0,0);assert(a.scene.strength>.7f);
 direct_sample(&a,5100000,0,0,0);assert(a.scene.strength<.00002f);
 assert(fabsf(a.scene.time-5.1f)<.0001f);
 float t=a.scene.time;direct_sample(&a,5000000,1,0,0);assert(a.scene.time==t&&a.scene.strength<.00002f);
 direct_sample(&a,5110000,1,368,448);assert(!a.down);
 assert(a.samples==6&&a.errors==0);
 puts("direct touch monotonic clock, drag, short-contact retention, release decay and bounds passed");
}
