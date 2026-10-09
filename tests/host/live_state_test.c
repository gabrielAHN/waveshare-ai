#include <assert.h>
#include <stdio.h>
#include "live_state.h"
int main(void){live_state s={0};unsigned char p[]={ 'W','L','S','4',2,0,0,0,1,0,0,0,0,0,0,0,0,1,2,0,0,0,0,0,0,0,0,1};
 assert(!live_active(&s,100));assert(live_decode(&s,p,sizeof p,100));assert(s.count==2&&s.ids[0]==1&&s.ids[1]==2);assert(live_active(&s,101));assert(live_active(&s,100+LIVE_TTL_US-1));assert(!live_active(&s,100+LIVE_TTL_US));assert(!live_active(&s,99));
 live_state before=s;p[4]=129;assert(!live_decode(&s,p,sizeof p,200));assert(!memcmp(&s,&before,sizeof s));p[4]=2;
 p[18]=1;assert(!live_decode(&s,p,sizeof p,200));p[18]=2;assert(!live_decode(&s,p,sizeof p-1,200));p[6]=6;assert(!live_decode(&s,p,sizeof p,200));p[6]=0;
 p[4]=0;assert(live_decode(&s,p,8,300));assert(live_fresh(&s,301)&&!live_active(&s,301));
 unsigned char v3[]={'W','L','S','4',2,0,2,0,1,0,0,0,0,0,0,0,1,1,2,0,0,0,0,0,0,0,2,1};
 assert(live_decode(&s,v3,sizeof v3,400));assert(s.providers[0]==1&&s.providers[1]==2);
 v3[16]=4;assert(!live_decode(&s,v3,sizeof v3,401));
 /* the WLS1/WLS2 rosters (8-byte records, no provider) are refused */
 unsigned char old[]={'W','L','S','1',1,0,0,0,1,0,0,0,0,0,0,0};assert(!live_decode(&s,old,sizeof old,500));
 old[3]='2';assert(!live_decode(&s,old,sizeof old,500));
 puts("Strict bounded live snapshot, unique sorted IDs, empty state, monotonic expiry, WLS1/WLS2 refused: PASS");}
