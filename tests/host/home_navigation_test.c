#include <assert.h>
#include <stdio.h>
#include "home_ui.h"
static int64_t t;
static void sample(home_ui *s,bool down,int x,int y){home_sample(s,t+=10000,down,x,y);if(!down&&s->page==HOME)home_sample(s,t+=250000,false,0,0);}
static void tap(home_ui*s,int x,int y){sample(s,true,x,y);sample(s,false,x,y);}
int main(void){
 home_ui s=(home_ui){.connected=true,.saved=true,.pair={.state=PAIR_ENROLLED_UNPAIRED},.phone={.st={.valid=true,.state=PH_AUTHORIZED,.flags=PHONE_FLAG_REQUIRED}}};assert(s.page==HOME);  /* Sensor ON: Wi-Fi, linked host, signed in (loading_state_test) */
 tap(&s,180,220);assert(s.page==SPARKLES);
 for(int i=0;i<200;i++)sample(&s,true,90,150);
 assert(s.input.scene.strength>.99f);float strength=s.input.scene.strength;
 sample(&s,false,0,0);assert(s.input.scene.strength<strength&&s.input.scene.strength>.8f);
 float theme=s.input.scene.theme;
 sample(&s,true,180,300);sample(&s,true,180,200);sample(&s,false,180,200);
 assert(s.page==SPARKLES&&s.input.scene.theme>theme);
 theme=s.input.scene.theme;
 sample(&s,true,180,440);sample(&s,true,184,224);sample(&s,false,184,224);  /* bottom-UP Home: never paints */
 assert(s.page==HOME&&s.input.scene.theme==theme);
 sample(&s,true,280,200);sample(&s,true,80,200);assert(s.drag_offset<0);
 sample(&s,false,80,200);assert(s.tile==1&&s.page==HOME);
 /* Four tiles: Sparkles / Helper / Sensor / Settings. */
 sample(&s,true,280,200);sample(&s,true,80,200);sample(&s,false,80,200);assert(s.tile==2&&s.page==HOME);
 tap(&s,180,220);assert(s.page==SENSORS);
 sample(&s,true,100,440);sample(&s,true,184,224);sample(&s,false,184,224);assert(s.page==HOME&&s.tile==2);
 sample(&s,true,280,200);sample(&s,true,80,200);sample(&s,false,80,200);assert(s.tile==3&&s.page==HOME);
 tap(&s,180,220);assert(s.page==SETTINGS);
 sample(&s,true,100,440);sample(&s,true,184,224);sample(&s,false,184,224);assert(s.page==HOME);
 sample(&s,true,90,210);sample(&s,true,270,210);sample(&s,false,270,210);assert(s.tile==2);
 sample(&s,true,90,210);sample(&s,true,270,210);sample(&s,false,270,210);assert(s.tile==1);
 sample(&s,true,90,210);sample(&s,true,270,210);sample(&s,false,270,210);assert(s.tile==0);
 /* Out-of-panel, cancelled taps and long holds cannot open services. */
 tap(&s,500,220);assert(s.page==HOME);
 sample(&s,true,180,220);sample(&s,true,500,220);assert(s.page==HOME);
 sample(&s,true,180,220);for(int i=0;i<80;i++)sample(&s,true,180,220);sample(&s,false,180,220);assert(s.page==HOME);
 sample(&s,true,180,220);sample(&s,true,200,260);sample(&s,false,180,220);assert(s.page==HOME);
 puts("home navigation, tap/hold, edge arbitration and sustained contact: PASS");
}
