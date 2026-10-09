#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "home_render.h"
/* Back-and-forth chat render/state fuzz (ASan/UBSan): random UTF-8/ASCII/newline/no-space replies
 * up to the wire cap, every state, every reveal, repeated re-press/release/stop/leave between
 * turns. Any out-of-bounds write in wrap/render/state trips the sanitizer. */
static uint16_t px[SPARKLES_PIXELS];
static uint32_t rng=12345;
static uint32_t rnd(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void fill(char*out,size_t cap){
 size_t n=rnd()%(cap+1),i=0;
 int mode=rnd()%5;
 while(i<n){
  uint32_t r=rnd()%100;
  if(mode==0&&r<30&&i+3<=n){out[i++]=(char)0xE4;out[i++]=(char)0xB8;out[i++]=(char)0xB2;continue;} /* CJK */
  if(mode==1&&r<5){out[i++]='\n';continue;}
  if(mode==2){out[i++]=(char)('a'+rnd()%26);continue;} /* one huge word (URL-like) */
  if(mode==3&&r<10){out[i++]=(char)(0x80+rnd()%0x40);continue;} /* stray continuation bytes */
  out[i++]=r<15?' ':(char)(33+rnd()%94);
 }
 out[i]=0;
}
int main(void){
 home_ui s={0};s.page=HELPER;s.connected=true;s.helper.nbots=3;
 s.phone.st.valid=true;s.phone.st.state=PH_AUTHORIZED;
 int64_t t=1000000;
 for(int turn=0;turn<4000;turn++){
  helper_view*h=&s.helper;
  /* press/hold/release like a finger on the mic */
  helper_press(h,t);t+=700000+rnd()%500000;helper_tick(h,t);
  h->level_milli=rnd()%1000;
  if(rnd()%7==0){helper_leave(h);}else helper_release(h,t);
  voice_command c={0};
  for(int k=0;k<32;k++)c.id[k]="0123456789abcdef"[rnd()%16];
  fill(c.transcript,VOICE_TRANSCRIPT_MAX);fill(c.text,VOICE_TEXT_MAX);
  static const unsigned seq[]={VOICE_TRANSCRIBING,VOICE_RUNNING,VOICE_RUNNING,VOICE_DONE,VOICE_ERROR,VOICE_INTERRUPTED,VOICE_STOPPING};
  for(int step=0;step<6;step++){
   c.status=seq[rnd()%7];
   if(step==0)c.status=VOICE_TRANSCRIBING;
   helper_apply(h,&c);
   if(rnd()%9==0)helper_stop(h);
   if(rnd()%11==0)helper_fail(h,"Bridge unreachable");
   for(int f=0;f<3;f++){t+=33000+rnd()%400000;helper_tick(h,t);s.live_now_us=t;assert(home_render(&s,px,SPARKLES_PIXELS));}
   if(helper_terminal(h->state))break;
  }
  /* re-press immediately while reply still typing (back-and-forth) */
  if(rnd()%3==0){h->reveal=(float)(rnd()%(VOICE_TEXT_MAX+40));assert(home_render(&s,px,SPARKLES_PIXELS));}
 }
 puts("back-and-forth chat state/render fuzz 4000 turns under ASan: PASS");
 return 0;
}
