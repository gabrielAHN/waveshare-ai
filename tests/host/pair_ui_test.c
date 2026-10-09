#include <assert.h>
#include <stdio.h>
#include "home_render.h"
#include "pair_usb.h"
/* Settings -> Hermes bridge: auto search on entry, pick with a large button, enrollment code,
 * cancel, summary; no address entry anywhere. */
static void tap(home_ui*s,int x,int y){int64_t t=s->input.stamp_us+20000;home_sample(s,t,true,x,y);home_sample(s,t+40000,false,x,y);s->input.stamp_us=t+60000;}
/* Tap the centre of the target with this action (asserts it exists). */
static void tap_action(home_ui*s,int action){settings_buttons b=home_settings_buttons(s,0);int k=home_settings_find(&b,action);assert(k>=0);tap(s,b.t[k].x+b.t[k].w/2,b.t[k].y+b.t[k].h/2);}
static const char*FP1="479ad1dc62451c82f1cb229bf5bf629f" "1e2eb88b79d0307b6dcf1898a0cce5f7";
static const char*FP2="00000000000000000000000000000000" "00000000000000000000000000000001";
static uint16_t*A,*B;
static void render_ok(const home_ui*s){A[0]=123;A[SPARKLES_PIXELS+1]=456;assert(home_render(s,A+1,SPARKLES_PIXELS));assert(A[0]==123&&A[SPARKLES_PIXELS+1]==456);}
static int diff_px(const home_ui*a,const home_ui*b){assert(home_render(a,A+1,SPARKLES_PIXELS)&&home_render(b,B,SPARKLES_PIXELS));int d=0;for(int i=0;i<SPARKLES_PIXELS;i++)d+=A[i+1]!=B[i];return d;}
int main(void){
 A=malloc((SPARKLES_PIXELS+2)*sizeof *A);B=malloc(SPARKLES_PIXELS*sizeof *B);assert(A&&B);
 /* No Wi-Fi: a message about USB provisioning, no buttons, no search. */
 home_ui s={0};s.page=SETTINGS;tap(&s,180,200);
 assert(home_settings_screen(&s)==SS_NO_WIFI&&home_settings_buttons(&s,0).count==0&&!s.pair.want_scan);render_ok(&s);  /* Wi-Fi comes with the flash */
 /* Wi-Fi up, never enrolled: entering Settings searches once by itself. */
 s.saved=s.connected=true;s.settings_tab=SETTINGS_HERMES;tap(&s,180,10);
 assert(s.pair.want_scan&&s.pair.step==PV_SCANNING&&home_settings_screen(&s)==SS_FIND);s.pair.want_scan=false;
 assert(home_settings_buttons(&s,0).count==0);render_ok(&s); /* nothing to press while searching */
 /* Nothing found -> one big "Search again". */
 s.pair.step=PV_LIST;settings_buttons b=home_settings_buttons(&s,0);assert(b.count==1&&b.t[0].action==SA_SCAN&&!strcmp(b.t[0].label,"Search again"));
 tap_action(&s,SA_SCAN);assert(s.pair.want_scan);s.pair.want_scan=false;
 /* Found two: one large button per bridge; B picks the second. The auto-search does not repeat. */
 s.pair.step=PV_LIST;
 assert(pair_list_add(&s.pair.list,"Studio host","192.0.2.10",8098,FP1)==1&&pair_list_add(&s.pair.list,"Other","192.0.2.11",8098,FP2)==1);
 tap(&s,180,10);assert(!s.pair.want_scan);
 b=home_settings_buttons(&s,0);assert(b.count==2&&!strcmp(b.t[0].label,"Studio host")&&!strcmp(b.t[1].label,"Other")&&b.t[0].action==SA_PICK0&&b.t[1].action==SA_PICK1);render_ok(&s);
 tap_action(&s,SA_PICK1);assert(s.pair.enroll_requested&&s.pair.want_enroll==1&&s.pair.step==PV_ENROLLING);
 /* While enrolling, the code is shown big and only Cancel responds. */
 s.pair.enroll_requested=false;s.pair.code=895757;s.pair.code_shown=true;home_ui nocode=s;nocode.pair.code_shown=false;
 assert(home_settings_screen(&s)==SS_CODE&&diff_px(&s,&nocode)>500);render_ok(&s);
 home_ui other=s;other.pair.code=895758;assert(!home_visual_equal(&s,&other));
 b=home_settings_buttons(&s,0);assert(b.count==1&&b.t[0].action==SA_CANCEL);
 tap(&s,180,b.t[0].y-40);assert(!s.pair.want_scan&&!s.pair.want_cancel);
 tap_action(&s,SA_CANCEL);assert(s.pair.want_cancel);s.pair.want_cancel=false;
 /* Worker reports success: status screen names the bridge. */
 s.pair.step=PV_DONE;s.pair.state=PAIR_ENROLLED_UNPAIRED;strcpy(s.pair.bridge,"Studio host");strcpy(s.pair.base,"https://192.0.2.10:8098");pair_hex32(FP1,s.pair.fp);s.pair.code_shown=false;
 assert(home_settings_screen(&s)==SS_STATUS);render_ok(&s);
 /* Forget / change bridge are USB-only (WPC1): the screen has no such control. */
 b=home_settings_buttons(&s,0);for(int i=0;i<b.count;i++)assert(!strstr(b.t[i].label,"orget")&&!strstr(b.t[i].label,"ridge"));
 assert(pair_cmd_apply(&s,PAIR_CMD_FORGET)&&s.pair.want_forget);s.pair.want_forget=false;s.phone.want_forget=false;
 /* Every step x phase x Wi-Fi renders within bounds; error note renders. */
 for(int st=0;st<=PV_ERROR;st++)for(int ph=0;ph<=PAIR_PAIRED;ph++)for(int w=0;w<3;w++){s.pair.step=st;s.pair.state=ph;s.saved=w>0;s.connected=w>1;strcpy(s.pair.note,"Bridge unreachable");render_ok(&s);}
 /* Bottom-edge UP returns Home. */
 s.pair.step=PV_LIST;int64_t t=s.input.stamp_us+20000;home_sample(&s,t,true,180,440);home_sample(&s,t+20000,true,184,224);home_sample(&s,t+40000,false,184,224);assert(s.page==HOME);
 /* Idle Settings is a still screen (no redraw) until something visible changes. */
 s.page=SETTINGS;s.connected=true;s.pair.step=PV_IDLE;s.pair.state=PAIR_ENROLLED_UNPAIRED;
 /* ...once the sign-in answer is in (before it, the sign-in tab shows a turning spinner: loading_state_test). */
 s.phone.st=(phone_status){.valid=true,.state=PH_AUTHORIZED,.flags=PHONE_FLAG_REQUIRED};
 home_ui still=s;assert(home_visual_equal(&s,&still));
 still.pair.live_http=503;assert(!home_visual_equal(&s,&still));
 free(A);free(B);puts("pair_ui: auto search, large pick/cancel buttons, enroll code, status, USB-only forget, render bounds: PASS");return 0;
}
