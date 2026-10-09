#pragma once
/* WGS1 (bots_command.h): test-only USB gestures replayed as REAL touch samples through home_sample(),
 * the path a finger takes (logged by the caller as usb-injected, never as touch). The USB reader
 * (home_wifi.c) calls this under the state lock; the host tests (tests/host/home_motion_test.c) drive the
 * same function. *clock = the first sample time (us); it comes back past the gesture's last sample. */
#include "home_ui.h"
#include "bots_command.h"
static inline void gesture_replay(home_ui*ui,unsigned g,int64_t*clock){
 int64_t t=*clock;
 if(g!=GESTURE_REPORT&&ui->down){home_sample(ui,t,false,0,0);t+=10000;}
 if(g==GESTURE_TOP_NEW){
  /* 15: the top pull, from the top band down to the centre = new chat */
  for(int k=0;k<=12;k++){home_sample(ui,t,true,184,20+16*k);t+=16000;}
  home_sample(ui,t,false,0,0);   /* released with no point, like the panel */
 }else if(g==GESTURE_NEW||g==GESTURE_OLDER){
  /* 1: pull the newest message up = new chat; 2: drag the chat down = older messages */
  int y0=g==GESTURE_OLDER?140:300,y1=g==GESTURE_OLDER?300:140;
  for(int k=0;k<=8;k++){home_sample(ui,t,true,184,y0+(y1-y0)*k/8);t+=10000;}
  home_sample(ui,t,false,184,y1);
 }else if(g==GESTURE_HOME){
  /* 3: the Home pull, an UP drag from the bottom edge to screen centre, released with no point. */
  for(int k=0;k<=8;k++){home_sample(ui,t,true,184,440-27*k);t+=10000;}
  home_sample(ui,t,false,0,0);
 }else if(g==GESTURE_OPEN_ASK&&ui->page==HOME){
  /* Horizontal tile swipes toward the Ask tile, each followed by the slide settling, then a tap. */
  int ask=home_tile_index(HELPER);  /* -1 when the AI plugin is not built: no swipes, the tap stays on Home */
  for(int guard=0;ask>=0&&guard<HOME_TILES&&ui->tile!=ask;guard++){
   int dir=ui->tile<ask?1:-1;  /* finger moves left (dx<0) = next tile */
   for(int k=0;k<=8;k++){home_sample(ui,t,true,184-dir*12*k,220);t+=10000;}
   home_sample(ui,t,false,184-dir*96,220);t+=10000;
   for(int k=0;k<40;k++){home_sample(ui,t,false,0,0);t+=10000;}
  }
  home_sample(ui,t,true,184,220);t+=30000;home_sample(ui,t,true,184,220);t+=30000;home_sample(ui,t,false,184,220);
 }else if((g==GESTURE_SPARKLE_NEXT||g==GESTURE_SPARKLE_PREV)&&ui->page==SPARKLES){
  /* the same quick flick a finger makes: 8 samples, 150 px in 80 ms, left = next */
  int dir=g==GESTURE_SPARKLE_NEXT?-1:1;
  for(int k=0;k<=8;k++){home_sample(ui,t,true,184-dir*75+dir*150*k/8,220);t+=10000;}
  home_sample(ui,t,false,184+dir*75,220);
 }
 *clock=t;
}
