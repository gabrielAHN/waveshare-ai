/* Colour themes (the named device's firmware/main/theme.h, Settings > Display): Light / Dark + five accents.
 *  1. The Light + Orange palette is the original one, value for value (every role pinned to the literal
 *     colour the renderer always used), and the accents are the specified five.
 *  2. WCAG contrast, computed from the palette: every text/background pair a Dark page draws is
 *     >= 4.5:1 for all five accents (on the 8-bit values AND on the RGB565 the panel shows), including
 *     button labels on the accent fill, the accent as text on every dark surface, and bubble text over
 *     the brightest colour of the dark Ask orbs under the dark glass.
 *  3. NVS value: mode | accent << 4; anything invalid reads as light + accent 0.
 *  4. Rendering: Dark pages are black where the layout allows (no leftover cream), the QR stays black on
 *     white, Sparkles / sensor status hues / provider colours ignore the theme, a theme change repaints.
 * Host only. Prints the lowest contrast pair. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "settings_states.h"

static uint16_t px[SPARKLES_PIXELS], px2[SPARKLES_PIXELS];

/* ---- WCAG 2.x relative luminance / contrast ---- */
static double lin(double c){c/=255.0;return c<=0.04045?c/12.92:pow((c+0.055)/1.055,2.4);}
static double lum(theme_rgb c){return 0.2126*lin(c.r)+0.7152*lin(c.g)+0.0722*lin(c.b);}
static double ratio(theme_rgb a,theme_rgb b){double x=lum(a),y=lum(b);if(x<y){double t=x;x=y;y=t;}return (x+0.05)/(y+0.05);}
/* What the panel shows: the RGB565 value expanded back to 8 bits. */
static theme_rgb shown(uint16_t v){return (theme_rgb){(uint8_t)((v>>11)*255/31),(uint8_t)(((v>>5)&63)*255/63),(uint8_t)((v&31)*255/31)};}
static double contrast(theme_rgb fg,theme_rgb bg){
 double a=ratio(fg,bg),b=ratio(shown(theme_565(fg)),shown(theme_565(bg)));return a<b?a:b;
}
static double lowest=99;static char lowest_pair[128];static int pairs;
static void pair(const char*what,int accent,theme_rgb fg,theme_rgb bg){
 double c=contrast(fg,bg);pairs++;
 if(c<lowest){lowest=c;snprintf(lowest_pair,sizeof lowest_pair,"%s accent=%s fg=(%d,%d,%d) bg=(%d,%d,%d)",what,theme_accent_name(accent),fg.r,fg.g,fg.b,bg.r,bg.g,bg.b);}
 if(c<4.5)printf("LOW CONTRAST %.2f %s accent=%s fg=(%d,%d,%d) bg=(%d,%d,%d)\n",c,what,theme_accent_name(accent),fg.r,fg.g,fg.b,bg.r,bg.g,bg.b);
 assert(c>=4.5);
}
#define DK(role) theme_rgb_of(THEME_DARK,a,(role))

/* Light + Orange = the default colours (home_render.h / power_ui.h). */
typedef struct {int role;uint8_t r,g,b;} pinned;
static const pinned light_orange[]={
 {TC_PAGE,255,248,238},{TC_ROW,255,228,196},{TC_DOT,255,228,196},{TC_INK,74,38,16},{TC_MUTED,150,86,44},{TC_ERROR,196,52,24},
 {TC_CARD,255,255,255},{TC_KNOB,255,255,255},{TC_OFF,160,150,140},{TC_SHADOW,255,248,240},{TC_GLASS,255,255,255},{TC_PAPER,255,252,247},
 {TC_USER,240,104,24},{TC_USER_INK,255,255,255},{TC_USER_DOT,255,196,140},{TC_USER_SHADOW,236,196,166},{TC_SCROLL,214,190,168},
 {TC_BOT_DOT,236,214,190},{TC_HOME_BG,181,207,213},{TC_HOME_INK,48,68,80},
 {TC_TILE_OFF_PAPER,214,214,210},{TC_TILE_OFF_INK,112,112,108},{TC_TILE_OFF_PILL,236,236,232},{TC_TILE_SPIN,200,196,190},
 {TC_BATT_SHELL,255,206,160},{TC_BATT_DEEP,240,170,120},{TC_BATT_GROUND,236,196,160},{TC_BATT_SHELL_OFF,255,228,196},
 {TC_BATT_DEEP_OFF,240,206,170},{TC_BATT_WINDOW,255,255,255},{TC_BATT_GLOSS,255,255,255},{TC_BATT_DARK,74,38,16},
 {TC_POWER_CARD,74,38,16},{TC_POWER_INK,255,255,255},{TC_SN_PAGE,250,244,232},{TC_SN_INK,47,52,74},{TC_SN_MUTED,132,126,146},
 {TC_SN_CARD,241,233,219},{TC_SN_RULE,222,212,196},{TC_SN_CORE,255,252,246},{TC_SN_TINT,255,254,250},
 {TA_FILL,238,112,28},{TA_TINT,255,196,140},{TA_ON,255,255,255},{TA_TEXT,238,112,28},{TA_FULL,250,150,40},{TA_NOTE,255,196,140},
};

/* A working board (Wi-Fi, host, both sign-ins, session feed), as tests/preview/docs_preview.c builds it. */
static void working(home_ui*s){
 memset(s,0,sizeof *s);s->connected=s->saved=true;s->pair.state=PAIR_ENROLLED_UNPAIRED;strcpy(s->pair.bridge,"Studio host");
 pair_live_result(&s->pair,200,1);s->phone.st.valid=true;s->phone.st.state=PH_AUTHORIZED;s->phone.st.flags=PHONE_FLAG_REQUIRED;
 strcpy(s->phone.st.name,"sam");s->phone_ha.st=s->phone.st;s->phone_ha.st.flags=PHONE_FLAG_REQUIRED;
}
static int64_t now=1000000;
static void ask(home_ui*s){
 working(s);s->page=HELPER;s->live_now_us=now;
 static const char*ids[3]={"helper","atlas","coding"},*names[3]={"Helper","Atlas","Coding"};
 s->bots.data=(bots_data){.valid=true,.count=3,.received_us=now};
 for(int i=0;i<3;i++){strcpy(s->bots.data.b[i].id,ids[i]);strcpy(s->bots.data.b[i].name,names[i]);s->bots.data.b[i].available=true;s->bots.data.b[i].reset_s=BOTS_NONE32;}
 helper_restore_bot(&s->helper,0);home_bots_sync(s,now);
}
static void run(home_ui*s,float seconds){for(int i=0;i<(int)(seconds*100);i++){now+=10000;helper_tick(&s->helper,now);}s->live_now_us=now;home_bots_sync(s,now);}
static int count565(const uint16_t*p,uint16_t c){int n=0;for(int i=0;i<SPARKLES_PIXELS;i++)n+=p[i]==c;return n;}
/* Light-only neutral colours that must never show on a Dark page (leftover cream / brown ink), outside
 * the box [x0,x1) x [y0,y1): art that is not themed (the Home tile art: Kotaro, the Sparkles image) or
 * whose anti-aliased rims can hit a light grey by coincidence (the Battery icon's white bolt). */
static int leftovers_outside(const uint16_t*p,int x0,int y0,int x1,int y1){
 static const int roles[]={TC_PAGE,TC_ROW,TC_INK,TC_MUTED,TC_ERROR,TC_OFF,TC_PAPER,TC_SCROLL,TC_BOT_DOT,TC_HOME_BG,
  TC_TILE_OFF_PAPER,TC_TILE_OFF_PILL,TC_TILE_SPIN,TC_SN_PAGE,TC_SN_CARD,TC_SN_INK,TC_POWER_CARD,TC_HOME_INK};
 int n=0;
 for(unsigned k=0;k<sizeof roles/sizeof roles[0];k++){
  uint16_t c=theme_color_of(THEME_LIGHT,0,roles[k]);if(c==theme_color_of(THEME_DARK,0,roles[k]))continue;
  for(int i=0;i<SPARKLES_PIXELS;i++){int x=i%368,y=i/368;
   if(x>=x0&&x<x1&&y>=y0&&y<y1)continue;
   n+=p[i]==c;}
 }
 return n;
}
static int leftovers(const uint16_t*p){return leftovers_outside(p,BATT_X0,BATT_Y0,BATT_X0+BATT_W+16,BATT_Y0+BATT_H+12);}

int main(void){
 /* ---- 1. Palette ---- */
 for(unsigned k=0;k<sizeof light_orange/sizeof light_orange[0];k++){
  const pinned*e=&light_orange[k];
  assert(theme_color_of(THEME_LIGHT,0,e->role)==sp_pack_lcd(e->r,e->g,e->b));
  theme_rgb c=theme_rgb_of(THEME_LIGHT,0,e->role);assert(c.r==e->r&&c.g==e->g&&c.b==e->b);
 }
 assert(sizeof light_orange/sizeof light_orange[0]==THEME_ROLES);  /* every role is pinned */
 assert(theme_color_of(THEME_LIGHT,0,TC_HOME_INK)==0x322a);       /* the old HC_TEXT constant */
 static const uint8_t spec[THEME_ACCENTS][3]={{238,112,28},{64,132,240},{46,164,104},{226,82,146},{138,96,228}};
 static const char*spec_names[THEME_ACCENTS]={"Orange","Blue","Green","Pink","Purple"};
 for(int a=0;a<THEME_ACCENTS;a++){
  theme_rgb c=theme_accent_color(a);assert(c.r==spec[a][0]&&c.g==spec[a][1]&&c.b==spec[a][2]&&!strcmp(theme_accent_name(a),spec_names[a]));
  for(int m=0;m<THEME_MODES;m++){theme_rgb f=theme_rgb_of(m,a,TA_FILL);assert(f.r==c.r&&f.g==c.g&&f.b==c.b);}  /* the fill IS the accent */
 }
 assert(!strcmp(theme_accent_name(9),"Orange")&&!strcmp(theme_mode_name(THEME_DARK),"Dark")&&!strcmp(theme_mode_name(3),"Light"));
 /* Dark pages are black and the dark neutrals warm-neutral (R+8 >= B, G+8 >= B: Settings' warm rule) */
 for(int a=0;a<THEME_ACCENTS;a++){theme_rgb b=DK(TC_PAGE);assert(!b.r&&!b.g&&!b.b);b=DK(TC_SN_PAGE);assert(!b.r&&!b.g&&!b.b);
  b=DK(TC_HOME_BG);assert(!b.r&&!b.g&&!b.b);b=DK(TC_PAPER);assert(!b.r&&!b.g&&!b.b);}
 for(int r=0;r<TC_NEUTRALS;r++){if(r>=TC_SN_PAGE&&r<=TC_SN_TINT)continue;theme_rgb c=theme_rgb_of(THEME_DARK,0,r);assert(c.r+8>=c.b&&c.g+8>=c.b);}

 /* ---- 2. Contrast of every text/background pair a Dark page draws, every accent ---- */
 for(int a=0;a<THEME_ACCENTS;a++){
  /* Settings: titles / paragraphs / status lines on the page, rows (label, caption, accent hint), the
   * primary button (label, caption, hint on the accent), the pairing code card, Battery's "?" */
  static const int texts[]={TC_INK,TC_MUTED,TC_ERROR,TA_TEXT};
  for(unsigned t=0;t<4;t++){pair("settings text/page",a,DK(texts[t]),DK(TC_PAGE));pair("settings text/row",a,DK(texts[t]),DK(TC_ROW));
   pair("settings text/card",a,DK(texts[t]),DK(TC_CARD));}
  pair("primary label/accent fill",a,DK(TA_ON),DK(TA_FILL));
  pair("battery ?/window",a,DK(TC_OFF),DK(TC_BATT_WINDOW));
  /* Home: tile label (and icon) on black, a closed tile's label and its status pill */
  pair("home label/tile",a,DK(TC_HOME_INK),DK(TC_HOME_BG));
  pair("home closed label/tile",a,DK(TC_TILE_OFF_INK),DK(TC_TILE_OFF_PAPER));
  pair("home pill text/pill",a,DK(TC_TILE_OFF_INK),DK(TC_TILE_OFF_PILL));
  /* Ask: text over the orbs is outlined in the shadow colour; on the bare paper; the user bubble */
  for(unsigned t=0;t<4;t++){pair("ask text/outline",a,DK(texts[t]),DK(TC_SHADOW));pair("ask text/paper",a,DK(texts[t]),DK(TC_PAPER));}
  pair("ask user text/user bubble",a,DK(TC_USER_INK),DK(TC_USER));
  /* your words follow the accent: each accent has its own bubble, and Orange keeps the original */
  for(int o=0;o<THEME_ACCENTS;o++)if(o!=a){theme_rgb x=DK(TC_USER),y=theme_rgb_of(THEME_DARK,o,TC_USER);assert(x.r!=y.r||x.g!=y.g||x.b!=y.b);}
  {theme_rgb l=theme_rgb_of(THEME_LIGHT,a,TC_USER),f=theme_rgb_of(THEME_LIGHT,a,TA_FILL);
   if(a){assert(l.r==f.r&&l.g==f.g&&l.b==f.b);}else{assert(l.r==240&&l.g==104&&l.b==24);}}
  /* Browser: the host on the bar, the loading / error message on the paper */
  /* Power-off overlay */
  pair("power title/card",a,DK(TC_POWER_INK),DK(TC_POWER_CARD));
  pair("power digit/card",a,DK(TA_TEXT),DK(TC_POWER_CARD));
  pair("power note/card",a,DK(TA_NOTE),DK(TC_POWER_CARD));
  /* Sensor: the word (ink), the label + number (status label colour) on each status fill and the
   * neutral card; the note line on the page */
  theme_use(THEME_DARK,a);
  for(int q=SQ_GOOD;q<=SQ_BAD+1;q++){
   pair("sensor word/tile",a,SN_INK_RGB,sn_fill_rgb((sensors_quality)q));
   pair("sensor label/tile",a,sn_label_rgb((sensors_quality)q),sn_fill_rgb((sensors_quality)q));
  }
  pair("sensor note/page",a,sn_mix(SN_MUTED_RGB,SN_INK_RGB,40),SN_PAGE_RGB);
 }
 /* Ask bubbles and the bot pill: ink / error text on the dark glass over EVERY colour of the dark orbs
  * (each bot's tint, every warmth, level and dither phase). */
 {double worst=99;uint16_t worst_under=0;
  uint16_t t=theme_glass_tint(theme_color_of(THEME_DARK,0,TC_GLASS),true);
  for(int tint=0;tint<3;tint++){
   orbs_theme_dark=1;orbs_build_lut(tint);assert(orbs_lut_dark==1);
   assert(orbs_lut[0][0][0]==0&&orbs_lut[3][ORBS_WARM-1][0]==0);  /* no glow = pixels off */
   for(int ph=0;ph<4;ph++)for(int w=0;w<ORBS_WARM;w++)for(int i=0;i<ORBS_LEVELS;i++){
    uint16_t under=orbs_lut[ph][w][i],g=theme_glass_px(under,t,true);
    for(int k=0;k<2;k++){
     theme_rgb fg=theme_rgb_of(THEME_DARK,0,k?TC_ERROR:TC_INK);
     double c=ratio(shown(theme_565(fg)),shown(g));if(c<worst){worst=c;worst_under=under;}
     pairs++;if(c<lowest){lowest=c;snprintf(lowest_pair,sizeof lowest_pair,"ask %s/glass over orb 0x%04x",k?"error":"ink",under);}
     assert(c>=4.5);
    }
   }
  }
  orbs_theme_dark=0;orbs_build_lut(0);assert(orbs_lut_dark==0);
  printf("DARK_GLASS worst=%.2f over orb 0x%04x\n",worst,worst_under);}
 printf("CONTRAST pairs=%d lowest=%.2f (%s)\n",pairs,lowest,lowest_pair);
 assert(lowest>=4.5);

 /* ---- 3. NVS value ---- */
 for(int m=0;m<THEME_MODES;m++)for(int a=0;a<THEME_ACCENTS;a++){
  uint8_t v=theme_nvs_encode(m,a),mm=9,aa=9;assert(v==(uint8_t)(m|a<<4));
  assert(theme_nvs_decode(v,&mm,&aa)&&mm==m&&aa==a);
 }
 assert(theme_nvs_encode(THEME_DARK,4)==0x41&&theme_nvs_encode(0,0)==0x00&&theme_nvs_encode(7,9)==0x00);
 int valid=0;
 for(int v=0;v<256;v++){uint8_t mm=9,aa=9;bool ok=theme_nvs_decode((uint8_t)v,&mm,&aa);
  assert(ok==((v&15)<THEME_MODES&&(v>>4)<THEME_ACCENTS));
  if(!ok)assert(mm==THEME_LIGHT&&aa==0);
  valid+=ok;}
 assert(valid==THEME_MODES*THEME_ACCENTS);
 {uint8_t mm,aa;assert(!theme_nvs_decode(0x02,&mm,&aa)&&!theme_nvs_decode(0x50,&mm,&aa)&&!theme_nvs_decode(0xff,&mm,&aa)&&mm==0&&aa==0);}
 assert(!strcmp(THEME_NVS_KEY,"theme"));

 /* ---- 4. Rendering ---- */
 /* Settings, every state in Dark: black page (corners), no leftover light colour; QR stays black on white */
 for(int i=0;i<SETTINGS_STATES;i++){
  home_ui s;const char*name=settings_state(i,&s);s.theme_mode=THEME_DARK;
  assert(home_render(&s,px,SPARKLES_PIXELS));
  int left=leftovers(px);if(left)printf("LEFTOVER %s %d px\n",name,left);
  assert(left==0&&px[2*368+2]==0&&px[440*368+365]==0);
  if(home_settings_screen(&s)==SS_QR&&s.phone.showing&&s.phone.qr_ok){
   int size=phone_qr_size(&s.phone),qx,qy,qs;phone_qr_geometry(&s.phone,&qx,&qy,&qs);
   for(int y=qy-qs;y<qy+(size+1)*qs;y++)for(int x=qx-qs;x<qx+(size+1)*qs;x++)assert(px[y*368+x]==PHONE_QR_PAPER||px[y*368+x]==PHONE_QR_INK);
  }
 }
 /* the accent on Settings: a primary button, the tab dot and switches follow it; its label is black in Dark */
 for(int m=0;m<THEME_MODES;m++)for(int a=0;a<THEME_ACCENTS;a++){
  home_ui s;settings_state(15,&s);s.theme_mode=(uint8_t)m;s.accent=(uint8_t)a;  /* "Scan to sign in" (primary) */
  settings_buttons b=home_settings_buttons(&s,s.live_now_us);assert(b.count>=1&&b.t[0].primary);
  assert(home_render(&s,px,SPARKLES_PIXELS));
  assert(px[(b.t[0].y+4)*368+b.t[0].x+b.t[0].w/2]==theme_color_of(m,a,TA_FILL));
  int label=0,fill=0;  /* inside the button: the accent fill and its label colour */
  for(int y=b.t[0].y;y<b.t[0].y+b.t[0].h;y++)for(int x=b.t[0].x;x<b.t[0].x+b.t[0].w;x++){
   label+=px[y*368+x]==theme_color_of(m,a,TA_ON);fill+=px[y*368+x]==theme_color_of(m,a,TA_FILL);}
  assert(label>300&&fill>label*4);
 }
 /* Home, every tile, Dark: black page, light labels, no leftovers; a closed tile stays readable */
 for(int t=0;t<HOME_TILES;t++){
  home_ui s;working(&s);s.tile=t;s.theme_mode=THEME_DARK;assert(home_render(&s,px,SPARKLES_PIXELS));
  assert(px[4*368+4]==0&&px[440*368+184]==0&&leftovers_outside(px,88,108,280,300)==0&&count565(px,theme_color_of(THEME_DARK,0,TC_HOME_INK))>200);
  s.connected=false;s.saved=true;assert(home_render(&s,px,SPARKLES_PIXELS));assert(leftovers_outside(px,88,108,280,300)==0&&px[4*368+4]==0);  /* loading pills */
 }
 /* Sensor page, Dark: black page, status hues kept (never the accent), no leftovers */
 {home_ui s;working(&s);s.page=SENSORS;s.input.stamp_us=now;s.sensors.attempts=1;s.sensors.http=200;
  s.sensors.data=(sensors_data){.valid=true,.state=SS_OK,.received_us=now};
  static const int16_t x10[SENSORS_COUNT]={234,410,10120,30,120,200};
  static const uint8_t q[SENSORS_COUNT]={SQ_GOOD,SQ_GOOD,SQ_GOOD,SQ_BAD,SQ_MEDIUM,SQ_GOOD};
  for(int i=0;i<SENSORS_COUNT;i++)s.sensors.data.r[i]=(sensors_row){.known=true,.x10=x10[i],.quality=q[i]};
  s.theme_mode=THEME_DARK;assert(home_render(&s,px,SPARKLES_PIXELS));
  assert(px[4*368+4]==0&&leftovers(px)==0);
  s.accent=3;assert(home_render(&s,px2,SPARKLES_PIXELS));assert(!memcmp(px,px2,sizeof px));  /* accent-free */
  theme_use(THEME_DARK,0);
  theme_rgb g=sn_fill_rgb(SQ_GOOD),m=sn_fill_rgb(SQ_MEDIUM),b=sn_fill_rgb(SQ_BAD);
  assert(g.b>g.g&&g.g>g.r&&m.r>m.b&&m.g>m.b&&b.r>b.g&&b.r>b.b);   /* blue / yellow / red, as in Light */
  assert(lum(g)<0.2&&lum(m)<0.2&&lum(b)<0.2);}                     /* dark tiles */
 /* Ask page, Dark: mostly black paper (orbs glow on it), dark glass bubbles, no light ink anywhere */
 {home_ui s;ask(&s);run(&s,2.2f);s.theme_mode=THEME_DARK;assert(home_render(&s,px,SPARKLES_PIXELS));
  int black=count565(px,0);printf("ASK_DARK black_px=%d of %d\n",black,SPARKLES_PIXELS);assert(black>SPARKLES_PIXELS/4);
  assert(!count565(px,theme_color_of(THEME_LIGHT,0,TC_INK))&&!count565(px,theme_color_of(THEME_LIGHT,0,TC_MUTED)));
  voice_command c={.status=VOICE_DONE};strcpy(c.id,"0123456789abcdef0123456789abcdef");strcpy(c.transcript,"What's on today?");
  strcpy(c.text,"Two meetings: a design review at 9:30 and a 1:1 at 11.");
  helper_press(&s.helper,now);run(&s,.8f);helper_release(&s.helper,now);run(&s,.2f);helper_apply(&s.helper,&c);run(&s,3.4f);
  assert(home_render(&s,px,SPARKLES_PIXELS));
  assert(count565(px,theme_color_of(THEME_DARK,0,TC_INK))>200&&count565(px,theme_color_of(THEME_DARK,0,TC_USER))>500);
  assert(!count565(px,theme_color_of(THEME_LIGHT,0,TC_INK))&&!count565(px,theme_color_of(THEME_LIGHT,0,TC_USER)));
  /* Light still draws exactly the light orbs (the LUT switches back) */
  s.theme_mode=THEME_LIGHT;assert(home_render(&s,px2,SPARKLES_PIXELS)&&orbs_lut_dark==0&&count565(px2,0)==0);}
 /* Dark text over the orbs is outlined: every ink pixel's 4 neighbours are ink or the black outline,
  * even on the brightest orb colour (so the "ask text/outline" pairs above are what the eye sees). */
 {theme_use(THEME_DARK,0);uint16_t orb=sp_pack_lcd(176,92,16),ink=HH_MUTED;
  for(int i=0;i<SPARKLES_PIXELS;i++)px[i]=orb;
  helper_text(px,40,200,"Checking your calendar",1,ink);helper_text(px,40,240,"Connecting",2,ink);
  int inked=0;
  for(int y=1;y<447;y++)for(int x=1;x<367;x++){if(px[y*368+x]!=ink)continue;inked++;
   static const int d[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
   for(int k=0;k<4;k++){uint16_t n=px[(y+d[k][1])*368+x+d[k][0]];assert(n==ink||n==HH_SHADOW);}}
  assert(inked>400&&HH_SHADOW==0);
  /* Light keeps the original single halo, down-right */
  theme_use(THEME_LIGHT,0);for(int i=0;i<SPARKLES_PIXELS;i++)px[i]=orb;
  helper_text(px,40,200,"Hi",1,HH_TEXT);
  int halo=count565(px,theme_color_of(THEME_LIGHT,0,TC_SHADOW));assert(halo>20);}
 /* Power-off overlay, Dark, every accent: dark card, accent digit lifted for text */
 for(int a=0;a<THEME_ACCENTS;a++){home_ui s;settings_state(12,&s);s.theme_mode=THEME_DARK;s.accent=(uint8_t)a;s.power.countdown=true;s.power.secs=3;
  assert(home_render(&s,px,SPARKLES_PIXELS));
  assert(px[(POWER_CARD_Y+POWER_CARD_H/2)*368+POWER_CARD_X+4]==theme_color_of(THEME_DARK,a,TC_POWER_CARD));
  assert(count565(px,theme_color_of(THEME_DARK,a,TA_TEXT))>300&&leftovers(px)==0);}
 /* Not themed: the Sparkles page (its own styles) draws the same pixels in every theme */
 {home_ui s;memset(&s,0,sizeof s);s.page=SPARKLES;s.input.scene.time=3;s.input.stamp_us=3000000;
  assert(home_render(&s,px,SPARKLES_PIXELS));s.theme_mode=THEME_DARK;s.accent=4;assert(home_render(&s,px2,SPARKLES_PIXELS));
  assert(!memcmp(px,px2,sizeof px));}
 /* Provider colours are fixed */
 theme_use(THEME_DARK,2);assert(home_live_provider_color(LIVE_PROVIDER_ANTHROPIC)==sp_pack_lcd(217,119,87));
 /* A theme change repaints every page; out-of-range values draw Light + Orange */
 {home_ui a;working(&a);home_ui b=a;assert(home_visual_equal(&a,&b));
  b.theme_mode=THEME_DARK;assert(!home_visual_equal(&a,&b));b=a;b.accent=2;assert(!home_visual_equal(&a,&b));
  a.page=b.page=SENSORS;b=a;b.accent=1;assert(!home_visual_equal(&a,&b));
  home_ui c=a;c.theme_mode=7;c.accent=9;assert(home_render(&a,px,SPARKLES_PIXELS)&&home_render(&c,px2,SPARKLES_PIXELS));
  assert(!memcmp(px,px2,sizeof px));}
 /* ---- 5. Light Home tiles follow the accent (SPEC3 Contract S): in carousel order each usable tile's
  *         paper is white mixed toward the accent at 16 / 24 / 32 / 40 % (repeating); the label and
  *         icon keep the dark ink, >= 4.5:1 on every shade of every accent; closed tiles stay the
  *         neutral grey; the Sparkles picture stays a picture; Dark stays black. ---- */
 {static const int pct[4]={16,24,32,40};double tile_low=99;char tile_low_at[96]="";
  for(int a=0;a<THEME_ACCENTS;a++){
   theme_rgb ac=theme_accent_color(a),ink=theme_rgb_of(THEME_LIGHT,a,TC_HOME_INK);
   uint16_t seen[8];
   for(int t=0;t<8;t++){
    int p=pct[t%4];  /* the independent oracle: round(255 - (255 - accent) * p / 100) per channel */
    theme_rgb want={(uint8_t)(255-((255-ac.r)*p+50)/100),(uint8_t)(255-((255-ac.g)*p+50)/100),(uint8_t)(255-((255-ac.b)*p+50)/100)};
    theme_rgb got=theme_tile_paper(a,t);assert(got.r==want.r&&got.g==want.g&&got.b==want.b);
    double c=contrast(ink,want);if(c<tile_low){tile_low=c;snprintf(tile_low_at,sizeof tile_low_at,"accent=%s %d%% bg=(%d,%d,%d)",theme_accent_name(a),p,want.r,want.g,want.b);}
    assert(c>=4.5);
    seen[t]=theme_565(want);
    if(t>=HOME_TILES)continue;
    /* drawn: the paper edge to edge (corners and the label row), the dark label on it */
    home_ui s;working(&s);s.tile=t;s.accent=(uint8_t)a;assert(home_render(&s,px,SPARKLES_PIXELS));
    assert(px[10*368+10]==seen[t]&&px[440*368+360]==seen[t]&&px[300*368+20]==seen[t]);
    assert(count565(px,theme_color_of(THEME_LIGHT,a,TC_HOME_INK))>200);
   }
   for(int t=0;t<4;t++)for(int u=0;u<t;u++)assert(seen[t]!=seen[u]);   /* four different shades */
   for(int t=4;t<8;t++)assert(seen[t]==seen[t-4]);                       /* then they repeat */
   /* a closed tile (signed out of Hermes: Ask grey) stays the neutral grey in every accent */
   {home_ui s;working(&s);s.tile=home_tile_index(HELPER);s.phone.st.state=PH_NONE;s.accent=(uint8_t)a;
    assert(home_tile_disabled(&s,s.tile)&&home_render(&s,px,SPARKLES_PIXELS));
    assert(px[10*368+10]==theme_color_of(THEME_LIGHT,a,TC_TILE_OFF_PAPER)&&px[10*368+10]==sp_pack_lcd(214,214,210));}
   /* Dark: every tile black in every accent */
   for(int t=0;t<HOME_TILES;t++){home_ui s;working(&s);s.tile=t;s.accent=(uint8_t)a;s.theme_mode=THEME_DARK;
    assert(home_render(&s,px,SPARKLES_PIXELS)&&px[10*368+10]==0&&px[440*368+360]==0&&px[300*368+20]==0);}
  }
  /* The Sparkles picture: only its paper changes (fully opaque picture pixels identical in every accent). */
  {int sp=home_tile_index(SPARKLES);home_ui s;working(&s);s.tile=sp;assert(home_render(&s,px,SPARKLES_PIXELS));
   int opaque=0,clear=0;
   for(int a=1;a<THEME_ACCENTS;a++){s.accent=(uint8_t)a;assert(home_render(&s,px2,SPARKLES_PIXELS));
    for(int y=0;y<SPARKLE_IMAGE_SIZE;y++)for(int x=0;x<SPARKLE_IMAGE_SIZE;x++){
     unsigned al=sparkle_image_alpha[y*SPARKLE_IMAGE_SIZE+x];int i=(108+y)*368+88+x;
     if(al==255){opaque++;assert(px[i]==px2[i]&&px[i]==sparkle_image_rgb[y*SPARKLE_IMAGE_SIZE+x]);}
     if(!al){clear++;assert(px2[i]==theme_565(theme_tile_paper(a,sp)));}}}
   assert(opaque>20000&&clear>100);}
  printf("TILE_SHADES 16/24/32/40%% x %d accents, ink (%d,%d,%d) lowest contrast %.2f (%s)\n",THEME_ACCENTS,
   theme_rgb_of(THEME_LIGHT,0,TC_HOME_INK).r,theme_rgb_of(THEME_LIGHT,0,TC_HOME_INK).g,theme_rgb_of(THEME_LIGHT,0,TC_HOME_INK).b,tile_low,tile_low_at);}
 printf("theme: palette pinned (Light + Orange = original, %d roles), %d accents, %d Dark text/background pairs >= 4.5:1 (lowest %.2f), NVS mode|accent<<4 (invalid = light + Orange), Dark pages black, Light tiles = accent shades: PASS\n",
  THEME_ROLES,THEME_ACCENTS,pairs,lowest);
 return 0;
}
