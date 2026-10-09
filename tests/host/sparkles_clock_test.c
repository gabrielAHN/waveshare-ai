#include <assert.h>
#include <stdio.h>
#include "sparkles.h"
int main(void){sparkles_state s={0};sparkles_advance(&s,1.7f,0,0,0);printf("wall=1.7 scene=%f\n",s.time);fflush(stdout);assert(fabsf(s.time-1.7f)<.001f);return 0;}
