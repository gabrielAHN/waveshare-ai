/* Settings (QR-first, no keyboard): every screen and every text box stays inside the rounded panel's
 * safe area and above the bottom-edge Home band, every control is a LARGE full-width target (>= 72 px
 * tall, >= 140 px wide, label in the big font, never truncated), no two targets overlap, the warm
 * palette holds (Orange accent), and no tap anywhere can edit the Wi-Fi credentials or the bridge
 * address. The render rules hold in both colour themes (Light and Dark, Settings > Display). */
#include <assert.h>
#include <stdio.h>
#include "settings_states.h"
#include "pair_usb.h"
static uint16_t px[SPARKLES_PIXELS];
static void tap(home_ui*s,int x,int y){int64_t t=s->input.stamp_us+20000;home_sample(s,t,true,x,y);home_sample(s,t+40000,false,x,y);s->input.stamp_us=t+60000;s->live_now_us=s->input.stamp_us;}
static void drag(home_ui*s,int x0,int y0,int x1,int y1){int64_t t=s->input.stamp_us+20000;home_sample(s,t,true,x0,y0);home_sample(s,t+20000,true,x1,y1);home_sample(s,t+40000,false,x1,y1);s->input.stamp_us=t+60000;s->live_now_us=s->input.stamp_us;}
static void tap_button(home_ui*s,int k){settings_buttons b=home_settings_buttons(s,s->live_now_us);assert(k<b.count);tap(s,b.t[k].x+b.t[k].w/2,b.t[k].y+b.t[k].h/2);}
static int find_button(const home_ui*s,const char*label){settings_buttons b=home_settings_buttons(s,s->live_now_us);for(int k=0;k<b.count;k++)if(!strcmp(b.t[k].label,label))return k;return -1;}
static bool rect_safe(int x,int y,int w,int h){
 for(int yy=y;yy<y+h;yy++)for(int xx=x;xx<x+w;xx+=(yy==y||yy==y+h-1)?1:(w-1>0?w-1:1))if(!set_safe_px(xx,yy))return false;
 return true;
}
/* Every drawn text of state i whose top is in [y0, y1), joined with spaces (wrapped lines rejoin). */
static void joined_texts(int i,int y0,int y1,char out[192]){
 home_ui s;settings_state(i,&s);settings_record rec;memset(&rec,0,sizeof rec);set_rec=&rec;assert(home_render(&s,px,SPARKLES_PIXELS));set_rec=NULL;
 out[0]=0;
 for(int j=0;j<rec.count;j++){
  if(rec.t[j].y<y0||rec.t[j].y>=y1)continue;
  if(strlen(out)+strlen(rec.t[j].text)+2>=192)break;
  if(out[0])strcat(out," ");
  strcat(out,rec.t[j].text);
 }
}
/* One provider tab's label: the tab title, the signed-in caption (state in_state), the signed-out caption
 * (out_state) and the sign-out warning (confirm_state). */
static void check_label(int tab,const char*name,const char*signed_in,const char*signed_out,const char*note,int in_state,int out_state,int confirm_state){
 size_t n=strlen(name);
 assert(n>=1&&n<=WAVESHARE_AI_ACCOUNT_NAME_MAX);
 for(const char*c=name;*c;c++)assert(*c>=0x20&&*c<0x7f);
 home_ui s;settings_state(in_state,&s);assert((int)s.settings_tab==tab);settings_buttons b=home_settings_buttons(&s,s.live_now_us);
 assert(b.count==1&&b.t[0].action==SA_SIGNOUT_ASK&&!strcmp(b.t[0].caption,signed_in));
 if(n<=WAVESHARE_AI_ACCOUNT_CAPTION_MAX){assert(!strncmp(b.t[0].caption,name,n)&&!strcmp(b.t[0].caption+n,": signed in as"));}
 else{assert(!strcmp(b.t[0].caption,"Signed in as"));}
 settings_record rec;memset(&rec,0,sizeof rec);set_rec=&rec;assert(home_render(&s,px,SPARKLES_PIXELS));set_rec=NULL;
 bool title=false,caption=false;
 for(int j=0;j<rec.count;j++){
  if(!strcmp(rec.t[j].text,name)&&rec.t[j].y<56&&rec.t[j].scale==2)title=true;
  if(!strcmp(rec.t[j].text,signed_in))caption=true;
 }
 assert(title&&caption);
 home_ui g;settings_state(out_state,&g);assert((int)g.settings_tab==tab);b=home_settings_buttons(&g,g.live_now_us);
 assert(b.count>=1&&b.t[0].action==SA_SIGNIN&&!strcmp(b.t[0].caption,signed_out));
 char joined[192];joined_texts(confirm_state,100,SET_SLOT_A,joined);
 assert(!strcmp(joined,note));
 printf("ACCOUNT %s name=\"%s\" signed_in=\"%s\" signed_out=\"%s\"\n",settings_tab_name(tab),name,signed_in,signed_out);
}
int main(void){
 /* 0. The safe-area rule itself: >= SET_EDGE px from every panel edge, >= SET_EDGE px inside the
  *    rounded corner arc (radius SET_PANEL_R), nothing in the bottom-edge Home band. */
 assert(SET_EDGE>=16&&SET_PANEL_R>=48&&SET_HOME_BAND>=28);
 assert(set_safe_px(184,224)&&!set_safe_px(SET_EDGE-1,224)&&!set_safe_px(368-SET_EDGE,224)&&!set_safe_px(184,SET_EDGE-1));
 assert(!set_safe_px(184,448-SET_HOME_BAND)&&set_safe_px(184,448-SET_HOME_BAND-1));
 assert(!set_safe_px(SET_EDGE,SET_EDGE)&&!set_safe_px(368-SET_EDGE-1,SET_EDGE)&&!set_safe_px(SET_EDGE,448-SET_HOME_BAND-1)); /* corners */
 assert(SET_BTN_MIN_H>=72&&SET_BTN_MIN_W>=140);
 int screens_seen[SS_SIGNOUT+1]={0};
 for(int i=0;i<SETTINGS_STATES;i++)for(int dark=0;dark<2;dark++){
  /* every state as built (Light unless it picks Dark), then the same state in Dark */
  home_ui s;const char*name=settings_state(i,&s);if(dark)s.theme_mode=THEME_DARK;
  settings_screen m=home_settings_screen(&s);screens_seen[m]++;
  settings_record rec;memset(&rec,0,sizeof rec);set_rec=&rec;
  assert(home_render(&s,px,SPARKLES_PIXELS));set_rec=NULL;
  /* 1. Pixels: every non-background pixel is inside the rounded safe area. */
  int outside=0,ink=0;
  for(int y=0;y<448;y++)for(int x=0;x<368;x++){if(px[y*368+x]==PHONE_BG)continue;ink++;if(!set_safe_px(x,y))outside++;}
  settings_buttons b=home_settings_buttons(&s,s.live_now_us);
  printf("SETTINGS_STATE %-30s %s screen=%-8s ink_px=%6d outside_safe=%d buttons=%d texts=%d",name,theme_mode_name(s.theme_mode),settings_screen_name(m),ink,outside,b.count,rec.count);
  for(int k=0;k<b.count;k++)printf(" [%d,%d %dx%d \"%s\"]",b.t[k].x,b.t[k].y,b.t[k].w,b.t[k].h,b.t[k].label);
  puts("");
  assert(outside==0&&ink>1500);
  /* 2. Text boxes: all inside the safe area, nothing clipped or truncated, never below scale 1. */
  assert(rec.count>0&&rec.count<=SET_REC_MAX&&!rec.overflow);
  for(int k=0;k<rec.count;k++){const settings_text*t=&rec.t[k];
   if(!rect_safe(t->x,t->y,t->w,t->h)||t->truncated)printf("  BAD TEXT \"%s\" at %d,%d %dx%d truncated=%d\n",t->text,t->x,t->y,t->w,t->h,t->truncated);
   assert(rect_safe(t->x,t->y,t->w,t->h)&&!t->truncated&&t->scale>=1);
   /* no two text boxes overlap (4 px apart), and a text box is either inside a target or >= 8 px clear of it */
   for(int j=0;j<k;j++){const settings_text*u=&rec.t[j];
    bool apart=t->y>=u->y+u->h+4||u->y>=t->y+t->h+4||t->x>=u->x+u->w+4||u->x>=t->x+t->w+4;
    if(!apart)printf("  TEXT OVERLAP \"%s\" / \"%s\"\n",t->text,u->text);
    assert(apart);}
   for(int j=0;j<b.count;j++){const settings_target*u=&b.t[j];
    bool inside=t->x>=u->x&&t->y>=u->y&&t->x+t->w<=u->x+u->w&&t->y+t->h<=u->y+u->h;
    bool clear=t->y>=u->y+u->h+8||u->y>=t->y+t->h+8||t->x>=u->x+u->w+8||u->x>=t->x+t->w+8;
    if(!inside&&!clear)printf("  TEXT \"%s\" (%d,%d %dx%d) touches target \"%s\"\n",t->text,t->x,t->y,t->w,t->h,u->label);
    assert(inside||clear);}
   /* and clear of the QR quiet zone (the open provider tab's own QR) */
   const phone_view *qr=home_tab_phone_c(&s,s.settings_tab);if(!qr)qr=&s.phone;
   int size=m==SS_QR?phone_qr_size(qr):0;
   if(size){int qx,qy,qs;phone_qr_geometry(qr,&qx,&qy,&qs);(void)qx;int cy=qy-PHONE_QR_QUIET*qs,card=(size+2*PHONE_QR_QUIET)*qs;
    assert(t->y>=cy+card+4||cy>=t->y+t->h+4);}
  }
  /* 3. Targets: <= 3, large, full-width-ish, inside the safe area, labelled in the big font, >= 12 px apart. */
  assert(b.count<=SETTINGS_MAX_BUTTONS);
  for(int k=0;k<b.count;k++){const settings_target*t=&b.t[k];
   assert(t->h>=SET_BTN_MIN_H&&t->w>=SET_BTN_MIN_W&&rect_safe(t->x,t->y,t->w,t->h));
   assert(t->label[0]&&t->action!=SA_NONE);
   assert(px[(t->y+t->h/2)*368+t->x+4]!=PHONE_BG); /* the target is drawn as a filled control */
   bool big=false;for(int j=0;j<rec.count;j++)if(!strcmp(rec.t[j].text,t->label)&&rec.t[j].x>=t->x&&rec.t[j].y>=t->y&&rec.t[j].x+rec.t[j].w<=t->x+t->w&&rec.t[j].y+rec.t[j].h<=t->y+t->h)big=rec.t[j].scale>=2||(int)strlen(t->label)*18>t->w-2*SET_PAD;
   if(!big)printf("  label \"%s\" not drawn big inside its target\n",t->label);
   assert(big);
   for(int j=0;j<k;j++){const settings_target*u=&b.t[j];
    bool apart=t->y>=u->y+u->h+12||u->y>=t->y+t->h+12||t->x>=u->x+u->w+12||u->x>=t->x+t->w+12;assert(apart);}
  }
  /* 4. Warm palette with the Orange accent, Light and Dark (no purple/pink): R+8 >= B and G+8 >= B for
   *    every pixel. (Blue / Pink / Purple accents are the user's choice.) */
  if(s.accent==0)for(int k=0;k<SPARKLES_PIXELS;k++){int r=(px[k]>>11)*8,g=((px[k]>>5)&63)*4,bl=(px[k]&31)*8;assert(r+8>=bl&&g+8>=bl);}
  if(dark)continue;  /* the tap rules below do not depend on the colours */
  /* 5. A 6 px tap grid over the whole panel never edits credentials/bridge address; a tap outside
   *    every target changes nothing that a button would; a tap inside one does exactly its action. */
  for(int y=3;y<420;y+=6)for(int x=3;x<368;x+=6){
   home_ui t;settings_state(i,&t);home_ui before=t;tap(&t,x,y);
   assert(!memcmp(&t.credentials,&before.credentials,sizeof t.credentials));
   assert(!memcmp(t.pair.base,before.pair.base,sizeof t.pair.base));
   int hit=home_settings_hit(&b,x,y);
   if(hit<0)assert(t.pair.want_scan==before.pair.want_scan&&!t.pair.enroll_requested&&!t.pair.want_cancel&&t.session_off==before.session_off&&!t.phone.want_forget&&t.phone.want_start==before.phone.want_start&&t.phone.showing==before.phone.showing&&
    t.theme_mode==before.theme_mode&&t.accent==before.accent&&!t.theme_save&&
    !t.phone_ha.want_forget&&t.phone_ha.want_start==before.phone_ha.want_start&&t.phone_ha.showing==before.phone_ha.showing);
  }
  for(int k=0;k<b.count;k++){home_ui t;settings_state(i,&t);home_ui before=t;tap_button(&t,k);
   bool changed=t.pair.want_scan!=before.pair.want_scan||t.pair.enroll_requested||t.pair.want_cancel||t.sound_off!=before.sound_off||t.session_off!=before.session_off||t.phone.want_forget||
    t.theme_mode!=before.theme_mode||t.accent!=before.accent||
    t.phone.want_start!=before.phone.want_start||t.phone.showing!=before.phone.showing||t.phone.confirm_signout!=before.phone.confirm_signout||
    t.phone_ha.want_forget||t.phone_ha.want_start!=before.phone_ha.want_start||t.phone_ha.showing!=before.phone_ha.showing||t.phone_ha.confirm_signout!=before.phone_ha.confirm_signout;
   /* a sign-in target acts on the open tab's own provider only */
   if(s.settings_tab==SETTINGS_HOME_ASSISTANT)assert(!t.phone.want_forget&&t.phone.want_start==before.phone.want_start&&t.phone.confirm_signout==before.phone.confirm_signout);
   if(s.settings_tab==SETTINGS_HERMES)assert(!t.phone_ha.want_forget&&t.phone_ha.want_start==before.phone_ha.want_start&&t.phone_ha.confirm_signout==before.phone_ha.confirm_signout);
   if(!changed)printf("  button %d \"%s\" did nothing\n",k,b.t[k].label);
   assert(changed);}
 }
 for(int m=0;m<=SS_SIGNOUT;m++)assert(screens_seen[m]);
 /* 6. QR screen: big symbol (>= 5 px modules) with a full 4-module white quiet zone, card inside the
  *    safe area, readable user code (scale 2), and a large Back. */
 {home_ui s;settings_state(9,&s);int size=phone_qr_size(&s.phone),qx,qy,qs;phone_qr_geometry(&s.phone,&qx,&qy,&qs);
  int card=(size+2*PHONE_QR_QUIET)*qs,cx=qx-PHONE_QR_QUIET*qs,cy=qy-PHONE_QR_QUIET*qs;
  assert(qs>=6&&rect_safe(cx-4,cy-4,card+8,card+8)); /* 6 px modules = the size proven to scan on the device */
  settings_buttons qb=home_settings_buttons(&s,s.live_now_us);for(int k=0;k<qb.count;k++)assert(qb.t[k].y>=cy+card+8);
  settings_record rec;memset(&rec,0,sizeof rec);set_rec=&rec;assert(home_render(&s,px,SPARKLES_PIXELS));set_rec=NULL;
  /* the whole square quiet zone (+1 px of rim) is white; only the symbol area has ink */
  for(int y=cy-1;y<cy+card+1;y++)for(int x=cx-1;x<cx+card+1;x++){
   bool symbol=x>=qx&&x<qx+size*qs&&y>=qy&&y<qy+size*qs;if(!symbol)assert(px[y*368+x]==PHONE_QR_PAPER);}
  bool code=false;for(int j=0;j<rec.count;j++)if(!strcmp(rec.t[j].text,"QWRT7KXM")&&rec.t[j].scale>=2)code=true;assert(code);
  assert(find_button(&s,"Back")>=0);
  printf("QR size=%d module_px=%d card_px=%d card=[%d,%d]\n",size,qs,card,cx,cy);}
 /* 7. Hermes and Sound are separate full-screen pages; Sound owns both toggles. */
 {home_ui s;settings_state(12,&s);settings_buttons b=home_settings_buttons(&s,s.live_now_us);
  assert(b.count==1&&b.t[0].action==SA_SIGNOUT_ASK&&!strcmp(b.t[0].label,"sam"));
  s.settings_tab=SETTINGS_SOUND;b=home_settings_buttons(&s,s.live_now_us);
  assert(b.count==2&&b.t[0].action==SA_SOUND&&b.t[0].toggle&&b.t[0].on&&b.t[1].action==SA_SPARKLE&&b.t[1].toggle&&b.t[1].on);
  home_ui g;settings_state(15,&g);b=home_settings_buttons(&g,g.live_now_us);assert(b.t[0].action==SA_SIGNIN&&b.t[0].primary);}
 /* 8. Sign out needs a large confirm; "Keep signed in" backs out. */
 {home_ui s;settings_state(12,&s);tap_button(&s,0);assert(s.phone.confirm_signout&&!s.phone.want_forget);
  tap_button(&s,find_button(&s,"Keep signed in"));assert(!s.phone.confirm_signout&&!s.phone.want_forget);
  tap_button(&s,0);tap_button(&s,find_button(&s,"Yes, sign out"));assert(s.phone.want_forget);}
 /* 9. QR Back closes the QR without signing anything; Try again asks for a fresh code. */
 {home_ui s;settings_state(9,&s);tap_button(&s,find_button(&s,"Back"));assert(!s.phone.showing&&home_settings_screen(&s)==SS_STATUS);
  home_ui e;settings_state(10,&e);tap_button(&e,find_button(&e,"Try again"));assert(e.phone.want_start&&e.phone.showing);}
 /* 10. WPC1 button hooks press exactly what a finger would (5 = first target, 6 = second). */
 {home_ui s;settings_state(12,&s);s.page=HOME;s.settings_tab=SETTINGS_SOUND;assert(pair_cmd_apply(&s,PAIR_CMD_BUTTON_B)&&s.page==SETTINGS&&s.session_off);
  s.settings_tab=SETTINGS_HERMES;assert(pair_cmd_apply(&s,PAIR_CMD_BUTTON_A)&&s.phone.confirm_signout);
  home_ui n;settings_state(0,&n);assert(!pair_cmd_apply(&n,PAIR_CMD_BUTTON_A)); /* no button, nothing happens */
  home_ui w;settings_state(12,&w);assert(pair_cmd_apply(&w,PAIR_CMD_FORGET_WIFI)&&w.action==HOME_FORGET&&w.busy);assert(!pair_cmd_apply(&w,PAIR_CMD_FORGET_WIFI));}
 /* 10b. WPC1 "phone" on a board that is already signed in is refused (would reset it to pending). */
 {home_ui s;settings_state(12,&s);assert(!pair_cmd_apply(&s,PAIR_CMD_PHONE)&&!s.phone.want_start&&!s.phone.showing);
  home_ui g;settings_state(15,&g);assert(pair_cmd_apply(&g,PAIR_CMD_PHONE)&&g.phone.want_start&&g.phone.showing);}
 /* 10c. Before the first phone status, Hermes is status-only; Sound still owns both toggles. */
 {home_ui s;settings_state(19,&s);settings_buttons nb=home_settings_buttons(&s,s.live_now_us);assert(nb.count==0);
  s.settings_tab=SETTINGS_SOUND;nb=home_settings_buttons(&s,s.live_now_us);assert(nb.count==2&&nb.t[1].action==SA_SPARKLE);}
 /* 10d. Phone approval supersedes the pre-approval phone-only gate immediately. */
 {home_ui s;settings_state(12,&s);s.page=HELPER;
  s.bots.data.valid=true;s.bots.data.count=3;s.bots.data.received_us=s.live_now_us;s.bots.attempts=1;s.bots.attempt_us=s.live_now_us;
  for(int k=0;k<3;k++){s.bots.data.b[k].available=0;s.bots.data.b[k].reason=BR_PHONE;}
  settings_phone(&s,PH_NONE,PHONE_FLAG_REQUIRED,0,"","","");home_bots_sync(&s,s.live_now_us);assert(s.helper.block==BR_PHONE);
  phone_status ok={0};ok.valid=true;ok.state=PH_AUTHORIZED;ok.flags=PHONE_FLAG_REQUIRED;strcpy(ok.name,"sam");
  home_phone_apply(&s,&ok,true);home_bots_sync(&s,s.live_now_us);
  assert(s.helper.block==BR_NONE&&!helper_mic_disabled(&s.helper));assert(s.bots.refresh);
  s.bots.data.b[0].available=true;s.bots.data.b[0].reason=BR_NONE;
  home_bots_sync(&s,s.live_now_us);assert(s.helper.block==BR_NONE);
  s.bots.data.b[0].available=false;s.bots.data.b[0].reason=BR_SIGNIN;home_bots_sync(&s,s.live_now_us);assert(s.helper.block==BR_SIGNIN);}
 /* 11. Gate enforced + not signed in: entering Settings opens the QR once, by itself. */
 {home_ui s;settings_state(15,&s);s.auto_qr=false;tap(&s,180,10);assert(s.phone.showing&&s.phone.want_start);
  s.phone.showing=false;s.phone.want_start=false;tap(&s,180,10);assert(!s.phone.showing);}
 /* 12. Bottom-edge UP is Home on every Settings screen, never a button. */
 for(int i=0;i<SETTINGS_STATES;i++){home_ui s;settings_state(i,&s);int64_t t=s.input.stamp_us+20000;
  home_sample(&s,t,true,184,440);home_sample(&s,t+20000,true,184,224);home_sample(&s,t+40000,false,184,224);assert(s.page==HOME&&!s.phone.showing);}
 /* 13. Settings is five full-screen horizontal pages. A swipe that starts on a toggle cancels
  *     that toggle; short/vertical drags cancel the tap; rounded-corner starts are inert. */
 {home_ui s;settings_state(12,&s);s.settings_tab=SETTINGS_SOUND;
  settings_buttons b=home_settings_buttons(&s,s.live_now_us);assert(b.count==2);
  assert(b.t[0].action==SA_SOUND&&b.t[0].toggle&&b.t[0].on);
  assert(b.t[1].action==SA_SPARKLE&&b.t[1].toggle&&b.t[1].on);
  int x=b.t[0].x+b.t[0].w/2,y=b.t[0].y+b.t[0].h/2;
  drag(&s,x,y,x-96,y+4);assert(s.settings_tab==SETTINGS_BATTERY&&!s.sound_off&&!s.sound_save);
  s.settings_tab=SETTINGS_SOUND;drag(&s,x,y,x+18,y+42);assert(s.settings_tab==SETTINGS_SOUND&&!s.sound_off);
  drag(&s,SET_EDGE,SET_EDGE,SET_EDGE+100,SET_EDGE+2);assert(s.settings_tab==SETTINGS_SOUND);}
 /* 14. Page order Wi-Fi, Display, Sound, Battery, then the providers: Hermes (WAVESHARE_AI_HERMES_ACCOUNT_NAME), Home
  *     Assistant (WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME); boundaries clamp, reverse swipes work, and the Wi-Fi page
  *     remains USB/no-keyboard. */
 {home_ui s;settings_state(12,&s);s.settings_tab=SETTINGS_WIFI;
  drag(&s,250,220,340,222);assert(s.settings_tab==SETTINGS_WIFI);
  drag(&s,300,220,190,222);assert(s.settings_tab==SETTINGS_DISPLAY);
  drag(&s,300,220,190,222);assert(s.settings_tab==SETTINGS_SOUND);
  drag(&s,300,220,190,222);assert(s.settings_tab==SETTINGS_BATTERY);
  drag(&s,300,220,190,222);assert(s.settings_tab==SETTINGS_HERMES);
  drag(&s,300,220,190,222);assert(s.settings_tab==SETTINGS_HOME_ASSISTANT);
  drag(&s,300,220,190,222);assert(s.settings_tab==SETTINGS_HOME_ASSISTANT);
  drag(&s,70,220,180,222);assert(s.settings_tab==SETTINGS_HERMES);
  drag(&s,70,220,180,222);assert(s.settings_tab==SETTINGS_BATTERY);
  drag(&s,70,220,180,222);assert(s.settings_tab==SETTINGS_SOUND);
  drag(&s,70,220,180,222);assert(s.settings_tab==SETTINGS_DISPLAY);
  drag(&s,70,220,180,222);assert(s.settings_tab==SETTINGS_WIFI);
  assert(!home_settings_has_keyboard(&s));}
 /* 15. QR belongs to Hermes, stays phone-only, and a cancelled Back gesture does not close it. */
 {home_ui s;settings_state(9,&s);assert(s.settings_tab==SETTINGS_HERMES&&home_settings_screen(&s)==SS_QR);
  int k=find_button(&s,"Back");settings_buttons b=home_settings_buttons(&s,s.live_now_us);assert(k>=0);
  int x=b.t[k].x+b.t[k].w/2,y=b.t[k].y+b.t[k].h/2;
  drag(&s,x,y,x-90,y);assert(s.phone.showing&&s.settings_tab==SETTINGS_HERMES);
  tap(&s,x,y);assert(!s.phone.showing);}
 /* 16. The sign-in labels, one per provider (account.h <- Kconfig CONFIG_WAVESHARE_AI_HERMES_ACCOUNT_NAME /
  *     CONFIG_WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME, host defaults "Hermes" / "Home Assistant"), each name its
  *     own tab, its signed-in / not-signed-in captions and its sign-out warning, all drawn whole (every
  *     state above already passed the safe-area, truncation and overlap checks with them).
  *     run_host_tests.sh repeats this suite with the longest caption name and the longest allowed name. */
 check_label(SETTINGS_HERMES,WAVESHARE_AI_HERMES_ACCOUNT_NAME,WAVESHARE_AI_HERMES_SIGNED_IN,WAVESHARE_AI_HERMES_SIGNED_OUT,WAVESHARE_AI_HERMES_SIGNOUT_NOTE,12,15,13);
 check_label(SETTINGS_HOME_ASSISTANT,WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME,WAVESHARE_AI_HOME_ASSISTANT_SIGNED_IN,WAVESHARE_AI_HOME_ASSISTANT_SIGNED_OUT,WAVESHARE_AI_HOME_ASSISTANT_SIGNOUT_NOTE,30,31,33);
 /* ... and the ONE account tab of a shared sign-in (SPEC3 Contract S): the Hermes label, the Hermes captions,
  *     the shared sign-out warning. */
 check_label(SETTINGS_HERMES,WAVESHARE_AI_HERMES_ACCOUNT_NAME,WAVESHARE_AI_HERMES_SIGNED_IN,WAVESHARE_AI_HERMES_SIGNED_OUT,WAVESHARE_AI_SHARED_SIGNOUT_NOTE,34,49,35);
 /* Flag 8 (shared sign-in): one tab (5 dots, the Home Assistant tab unreachable) that covers both providers and
  * says each one's state; its sign-out confirmation says it signs out both. (Before SPEC3 each of the two tabs
  * said "Shared with ... sign-in".) */
 {char joined[192];
  for(int i=0;i<SETTINGS_STATES;i++){home_ui s;settings_state(i,&s);
   bool shared=(phone_shared(&s.phone)||phone_shared(&s.phone_ha))&&s.connected;
   if(shared)assert(s.settings_tab==SETTINGS_HERMES&&home_settings_tabs(&s)==5&&!home_settings_tab_reachable(&s,SETTINGS_HOME_ASSISTANT));}
  joined_texts(38,SET_ROW2_Y,SET_STATUS_Y,joined);assert(strstr(joined,WAVESHARE_AI_SHARED_COVERS)&&strstr(joined,"Hermes: ready")&&!strstr(joined,"Shared with"));
  joined_texts(34,SET_ROW2_Y,SET_STATUS_Y,joined);assert(strstr(joined,WAVESHARE_AI_SHARED_COVERS)&&strstr(joined,"Hermes: ready")&&strstr(joined,"Home Assistant: ready"));
  joined_texts(41,SET_ROW2_Y,SET_STATUS_Y,joined);assert(strstr(joined,WAVESHARE_AI_SHARED_COVERS)&&strstr(joined,"Home Assistant: not for this account"));
  joined_texts(49,SET_ROW2_Y,SET_STATUS_Y,joined);assert(strstr(joined,"Hermes: sign in to use Ask")&&strstr(joined,"Home Assistant: sign in to use Sensor"));
  joined_texts(53,SET_ROW2_Y,SET_STATUS_Y,joined);assert(strstr(joined,WAVESHARE_AI_SHARED_COVERS));
  joined_texts(30,SET_ROW2_Y,SET_STATUS_Y,joined);assert(!strstr(joined,"Shared with")&&!strstr(joined,WAVESHARE_AI_SHARED_COVERS));
  {home_ui q;settings_state(50,&q);assert(home_settings_screen(&q)==SS_QR&&q.phone.showing&&!q.phone_ha.showing&&find_button(&q,"Back")>=0);}
  joined_texts(39,100,SET_SLOT_A,joined);assert(!strcmp(joined,WAVESHARE_AI_SHARED_SIGNOUT_NOTE));
  joined_texts(35,100,SET_SLOT_A,joined);assert(!strcmp(joined,WAVESHARE_AI_SHARED_SIGNOUT_NOTE));
  assert(strstr(WAVESHARE_AI_SHARED_SIGNOUT_NOTE,WAVESHARE_AI_HERMES_ACCOUNT_NAME)&&strstr(WAVESHARE_AI_SHARED_SIGNOUT_NOTE,WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME));
  printf("SHARED hermes=\"%s\" home_assistant=\"%s\" signout=\"%s\"\n",WAVESHARE_AI_HERMES_SHARED,WAVESHARE_AI_HOME_ASSISTANT_SHARED,WAVESHARE_AI_SHARED_SIGNOUT_NOTE);
  /* 404: "Not set up on <bridge name or this host>", no buttons. */
  joined_texts(36,64,SET_STATUS_Y,joined);assert(strstr(joined,"Not set up on Studio host"));
  joined_texts(37,64,SET_STATUS_Y,joined);assert(strstr(joined,"Not set up on " SETTINGS_LONG_BRIDGE));
  joined_texts(40,64,SET_STATUS_Y,joined);assert(strstr(joined,"Not set up on this host"));
  for(int i=36;i<=40;i+=(i==37?3:1)){home_ui n;settings_state(i,&n);assert(home_settings_buttons(&n,n.live_now_us).count==0);}}
 /* 17. The Home Assistant tab: the same rows and screens as Hermes, acting on ITS sign-in view only. */
 {home_ui s;settings_state(30,&s);tap_button(&s,0);assert(s.phone_ha.confirm_signout&&!s.phone.confirm_signout&&home_settings_screen(&s)==SS_SIGNOUT);
  tap_button(&s,find_button(&s,"Keep signed in"));assert(!s.phone_ha.confirm_signout&&!s.phone_ha.want_forget);
  tap_button(&s,0);tap_button(&s,find_button(&s,"Yes, sign out"));assert(s.phone_ha.want_forget&&!s.phone.want_forget);
  home_ui o;settings_state(31,&o);settings_buttons b=home_settings_buttons(&o,o.live_now_us);
  assert(b.count==1&&b.t[0].action==SA_SIGNIN&&b.t[0].primary);
  tap_button(&o,0);assert(o.phone_ha.want_start&&o.phone_ha.showing&&!o.phone.want_start&&!o.phone.showing&&o.settings_tab==SETTINGS_HOME_ASSISTANT);
  home_ui q;settings_state(32,&q);assert(home_settings_screen(&q)==SS_QR);
  int k=find_button(&q,"Back");assert(k>=0);b=home_settings_buttons(&q,q.live_now_us);
  int x=b.t[k].x+b.t[k].w/2,y=b.t[k].y+b.t[k].h/2;
  drag(&q,x,y,x-90,y);assert(q.phone_ha.showing&&q.settings_tab==SETTINGS_HOME_ASSISTANT);   /* the QR keeps its tab */
  tap(&q,x,y);assert(!q.phone_ha.showing&&home_settings_screen(&q)==SS_STATUS);
  /* WPC1 5/6 press the open tab's targets; WPC1 4 opens the open tab's QR. */
  home_ui w;settings_state(30,&w);w.page=HOME;assert(pair_cmd_apply(&w,PAIR_CMD_BUTTON_A)&&w.page==SETTINGS&&w.phone_ha.confirm_signout&&!w.phone.confirm_signout);
  home_ui p4;settings_state(31,&p4);assert(pair_cmd_apply(&p4,PAIR_CMD_PHONE)&&p4.phone_ha.want_start&&p4.phone_ha.showing&&!p4.phone.want_start);
  home_ui a;settings_state(36,&a);assert(!pair_cmd_apply(&a,PAIR_CMD_BUTTON_A)&&!pair_cmd_apply(&a,PAIR_CMD_PHONE)&&!a.phone_ha.want_start);
  /* Entering Settings while Home Assistant requires a sign-in (Hermes signed in): its QR opens by itself, once. */
  home_ui e;settings_state(31,&e);e.settings_tab=SETTINGS_WIFI;e.auto_qr=false;tap(&e,180,10);
  assert(e.settings_tab==SETTINGS_HOME_ASSISTANT&&e.phone_ha.showing&&e.phone_ha.want_start&&!e.phone.showing);
  e.phone_ha.showing=e.phone_ha.want_start=false;tap(&e,180,10);assert(!e.phone_ha.showing);
  /* Pairing with the bridge shows on the Home Assistant tab too while the board is unpaired. */
  home_ui m;settings_state(43,&m);assert(home_settings_screen(&m)==SS_FIND&&find_button(&m,"Studio host")==0);
  m.auto_scan=false;m.pair.step=PV_IDLE;m.pair.list.count=0;tap(&m,180,10);assert(m.pair.want_scan);}
 /* 18. Settings > Display (theme.h): two standard rows, Theme (Light / Dark, tap toggles) and Accent (the
  *     colour's name + a swatch, tap steps to the next one, wrapping), titled "Display", part of the device
  *     core (reachable without Wi-Fi); taps change only the theme and ask the NVS worker to save it. */
 {assert(!strcmp(settings_tab_name(SETTINGS_DISPLAY),"display")&&SETTINGS_DISPLAY==SETTINGS_WIFI+1&&SETTINGS_SOUND==SETTINGS_DISPLAY+1);
  assert(!strcmp(settings_action_name(SA_THEME),"theme")&&!strcmp(settings_action_name(SA_ACCENT),"accent")&&!strcmp(settings_action_name(SA_ACCENT+1),"?"));
  home_ui s;settings_state(44,&s);settings_buttons b=home_settings_buttons(&s,s.live_now_us);
  assert(home_settings_screen(&s)==SS_STATUS&&b.count==2);
  assert(b.t[0].action==SA_THEME&&!strcmp(b.t[0].caption,"Theme")&&!strcmp(b.t[0].label,"Light")&&b.t[0].swatch==SET_SWATCH_THEME&&!b.t[0].primary&&!b.t[0].toggle);
  assert(b.t[1].action==SA_ACCENT&&!strcmp(b.t[1].caption,"Accent")&&!strcmp(b.t[1].label,"Orange")&&b.t[1].swatch==SET_SWATCH_ACCENT);
  assert(b.t[0].y==SET_ROW1_Y&&b.t[1].y==SET_ROW2_Y&&b.t[0].h==SET_ROW_H&&b.t[1].h==SET_ROW_H);
  settings_record rec;memset(&rec,0,sizeof rec);set_rec=&rec;assert(home_render(&s,px,SPARKLES_PIXELS));set_rec=NULL;
  bool title=false;for(int j=0;j<rec.count;j++)if(!strcmp(rec.t[j].text,"Display")&&rec.t[j].y<56&&rec.t[j].scale==2)title=true;
  assert(title);
  /* the accent swatch (switch slot of the Accent row) is the accent; the Theme disc is half light, half black */
  int sx=b.t[1].x+b.t[1].w-SET_PAD-SET_SWITCH_W/2,sy=b.t[1].y+b.t[1].h/2;assert(px[sy*368+sx]==theme_color_of(THEME_LIGHT,0,TA_FILL));
  home_ui before=s;tap_button(&s,0);
  assert(s.theme_mode==THEME_DARK&&s.accent==0&&s.theme_save&&s.sound_off==before.sound_off&&s.session_off==before.session_off&&s.settings_tab==SETTINGS_DISPLAY);
  b=home_settings_buttons(&s,s.live_now_us);assert(!strcmp(b.t[0].label,"Dark"));
  assert(!home_visual_equal(&s,&before));  /* the whole page repaints in the new theme */
  s.theme_save=false;tap_button(&s,0);assert(s.theme_mode==THEME_LIGHT&&s.theme_save);
  static const char*names[THEME_ACCENTS+1]={"Orange","Blue","Green","Pink","Purple","Orange"};
  for(int k=1;k<=THEME_ACCENTS;k++){tap_button(&s,1);b=home_settings_buttons(&s,s.live_now_us);
   assert(s.accent==k%THEME_ACCENTS&&!strcmp(b.t[1].label,names[k])&&s.theme_mode==THEME_LIGHT);
   assert(home_render(&s,px,SPARKLES_PIXELS)&&px[sy*368+sx]==theme_color_of(THEME_LIGHT,k%THEME_ACCENTS,TA_FILL));}
  /* WPC1 5/6 press the rows like a finger */
  home_ui w;settings_state(44,&w);w.page=HOME;assert(pair_cmd_apply(&w,PAIR_CMD_BUTTON_B)&&w.page==SETTINGS&&w.accent==1&&w.theme_save);
  /* no Wi-Fi: Display is still reachable, between Wi-Fi and Sound */
  home_ui o;settings_state(48,&o);assert(!o.connected&&home_settings_tab_reachable(&o,SETTINGS_DISPLAY)&&home_settings_buttons(&o,0).count==2);
  o.settings_tab=SETTINGS_WIFI;drag(&o,300,220,190,222);assert(o.settings_tab==SETTINGS_DISPLAY);
  home_settings_tick(&o);assert(o.settings_tab==SETTINGS_DISPLAY);
  drag(&o,300,220,190,222);assert(o.settings_tab==SETTINGS_SOUND);
  tap(&o,184,10);assert(o.settings_tab==SETTINGS_SOUND);}
 printf("settings_ui: %d states inside the rounded safe area (edge %d, corner R%d, home band %d), large non-overlapping targets, no truncation, no text entry, warm palette, tap grid, QR, Back, sign-out confirm, WPC1, Display tab, Light + Dark: PASS\n",SETTINGS_STATES,SET_EDGE,SET_PANEL_R,SET_HOME_BAND);
 return 0;
}
