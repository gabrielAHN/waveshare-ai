#include <assert.h>
#include <stdio.h>
#include "home_render.h"
#include "live_transport.h"
/* WLS4 token-density level wire, WLS1/WLS2/WLS3 refused, fail-closed parsing,
 * stale TTL, level->scene density mapping and six distinct ambient steps. */
static unsigned char wire[LIVE_WIRE_MAX+16];
/* WLS4 records are (u64 id, u8 provider, u8 state). Other formats are rejected. */
static size_t build(const char*magic,unsigned count,unsigned b6,unsigned b7){
 unsigned stride=!memcmp(magic,"WLS4",4)?10:(!memcmp(magic,"WLS3",4)?9:8);
 memcpy(wire,magic,4);wire[4]=count&255;wire[5]=count>>8;wire[6]=b6;wire[7]=b7;
 for(unsigned i=0;i<count;i++){uint64_t id=1000+i*7;for(int j=0;j<8;j++){wire[8+i*stride+j]=(unsigned char)(id>>(8*j));}
  if(stride>=9)wire[16+i*stride]=0;
  if(stride==10)wire[17+i*stride]=1;}
 return 8+count*stride;
}
static uint16_t px[SPARKLES_PIXELS];
static long ambient(int density,float t){
 sparkles_state s={.time=t,.density=density,.usage=SP_USAGE_DEFAULT};
 sparkles_render_direct(&s,px,SPARKLES_PIXELS);long e=0;for(int i=0;i<SP_MASK_PIXELS;i++)e+=sp_mask[i];return e;
}
int main(void){
 live_state s={0};
 /* 1. WLS1/WLS2/WLS3 rosters are refused, whatever they carry. */
 for(unsigned c=0;c<=9;c++){size_t n=build("WLS1",c,0,0);assert(!live_decode(&s,wire,n,100));
  n=build("WLS2",c,c>5?5:c,0);assert(!live_decode(&s,wire,n,100));assert(!s.valid);
  n=build("WLS3",c,c?1:0,0);assert(!live_decode(&s,wire,n,100));}
 /* 2. WLS4: explicit level/flags. */
 size_t n;
 for(unsigned lv=0;lv<6;lv++){n=build("WLS4",3,lv,LIVE_FLAG_MEASURED);assert(live_decode(&s,wire,n,200));
  assert(s.level==lv&&s.count==3&&s.flags==LIVE_FLAG_MEASURED&&s.ids[2]==1014);}
 n=build("WLS4",0,0,0);assert(live_decode(&s,wire,n,300)&&s.count==0&&s.level==0);
 /* 3. Malformed WLS4 rejected without mutating the previous snapshot. */
 n=build("WLS4",2,4,0);assert(live_decode(&s,wire,n,400));live_state before=s;
 struct {const char*m;unsigned c,b6,b7;int trunc;int extra;} bad[]={
  {"WLS4",2,6,0,0,0},{"WLS4",2,255,0,0,0},{"WLS4",2,3,4,0,0},{"WLS4",2,3,0x80,0,0},
  {"WLS4",0,1,0,0,0},{"WLS4",2,3,0,1,0},{"WLS4",2,3,0,0,1},{"WLS4",129,3,0,0,0},
  {"WLS2",2,3,0,0,0},{"wls4",2,3,0,0,0},{"WLS4",2,3,0,0,0}};
 for(unsigned k=0;k<sizeof bad/sizeof *bad;k++){
  size_t m=bad[k].c<=LIVE_MAX?build(bad[k].m,bad[k].c,bad[k].b6,bad[k].b7):(build(bad[k].m,0,bad[k].b6,bad[k].b7),wire[4]=bad[k].c&255,wire[5]=bad[k].c>>8,8);
  if(k==sizeof bad/sizeof *bad-1){for(int j=18;j<26;j++){wire[j]=0;}} /* zero ID */
  m=m-bad[k].trunc+bad[k].extra;
  assert(!live_decode(&s,wire,m,500));assert(!memcmp(&s,&before,sizeof s));
 }
 /* unsorted / duplicate IDs */
 n=build("WLS4",2,2,0);memcpy(wire+18,wire+8,8);assert(!live_decode(&s,wire,n,500));
 assert(!live_decode(&s,wire,7,500)&&!live_decode(&s,NULL,8,500)&&!live_decode(NULL,wire,8,500));
 /* oversize body through the bounded transport */
 {live_http_body b={.media_ok=true};n=build("WLS4",LIVE_MAX,5,0);assert(live_body_append(&b,wire,(int)n));b.length=LIVE_WIRE_MAX;
  assert(!live_body_append(&b,wire,1));live_state t={0};assert(!live_body_finish(&b,200,1,&t)&&!t.valid);}
 /* 4. Stale TTL and toggle gate the scene density. */
 home_ui ui={.page=SPARKLES};n=build("WLS4",1,5,1);assert(live_decode(&ui.live,wire,n,1000));
 ui.live_now_us=1000+LIVE_TTL_US-1;assert(home_live_density(&ui)==6&&home_live_points(&ui)==1); /* 1 active session, level 5 */
 ui.live_now_us=1000+LIVE_TTL_US;assert(home_live_density(&ui)==0&&home_live_points(&ui)==0); /* stale -> calm */
 ui.live_now_us=999;assert(home_live_density(&ui)==0);
 ui.live_now_us=2000;ui.session_off=true;assert(home_live_density(&ui)==0&&home_live_points(&ui)==0);
 ui.session_off=false;ui.live.valid=false;assert(home_live_density(&ui)==0);
 /* 5. Density steps: density 0 == usage mapping (calm default unchanged). */
 {sp_density_step d=sp_density(0,SP_USAGE_DEFAULT);assert(d.spawn==sp_usage_spawn(SP_USAGE_DEFAULT)&&d.alpha==1&&d.scale==1&&d.cap==SP_USAGE_MAX_GLINTS);
  sp_density_step prev={0};
  for(int lv=1;lv<=6;lv++){d=sp_density(lv,0);assert(d.spawn>prev.spawn&&d.alpha>prev.alpha&&d.scale>prev.scale&&d.speed>prev.speed&&d.cap>=prev.cap&&d.cap<=SP_USAGE_MAX_GLINTS);prev=d;}
  d=sp_density(7,.5f);assert(d.spawn==sp_usage_spawn(.5f));d=sp_density(-1,.5f);assert(d.spawn==sp_usage_spawn(.5f));}
 /* 6. Six clearly distinct ambient intensities (averaged over time). */
 long e[7];for(int lv=0;lv<=6;lv++){e[lv]=0;for(int k=0;k<6;k++)e[lv]+=ambient(lv,2.f+k*.41f);}
 printf("ambient energy: default=%ld L0=%ld L1=%ld L2=%ld L3=%ld L4=%ld L5=%ld\n",e[0],e[1],e[2],e[3],e[4],e[5],e[6]);
 for(int lv=2;lv<=6;lv++)assert(e[lv]>e[lv-1]+e[lv-1]/10); /* each step >=10% more */
 assert(e[6]>e[1]*3);
 puts("WLS4 level wire, WLS1/WLS2/WLS3 refused, fail-closed parsing, stale/toggle gating and six distinct steps PASS");
 return 0;
}
