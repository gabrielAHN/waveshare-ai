#include <assert.h>
#include <stdio.h>
#include "direct_input.h"
int main(void){
 direct_input a={0};direct_sample(&a,1000000,0,0,0);
 direct_sample(&a,1010000,1,180,220);assert(a.scene.strength>0&&a.scene.strength<.15f);
 float first=a.scene.strength;
 direct_sample(&a,1110000,1,180,120);assert(a.scene.strength>first&&a.scene.strength<.8f);
 assert(a.scene.theme>0);assert(a.scene.gradient==0);
 direct_sample(&a,1210000,1,280,120);assert(a.scene.gradient>0);
 float theme=a.scene.theme,gradient=a.scene.gradient,strength=a.scene.strength;
 direct_sample(&a,1310000,0,0,0);assert(a.scene.strength<strength&&a.scene.theme==theme&&a.scene.gradient==gradient);
 direct_sample(&a,1320000,1,10,400);assert(a.scene.theme==theme&&a.scene.gradient==gradient); /* new contact never jumps themes */
 direct_sample(&a,1420000,1,0,440);assert(a.scene.theme<theme&&a.scene.gradient<gradient);
 puts("gradual touch onset, swipe direction, persistent themes and no new-contact jump passed");
}
