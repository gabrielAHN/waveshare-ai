#include <assert.h>
#include <stdio.h>
#include "home_ui.h"
/* Opening Ask (the mic tile) while signed in must show an enabled bot at once,
 * not "Hermes unavailable" until a fresh /v1/bots poll lands or a touch arrives. */
int main(void){
 const int64_t S=1000000;
 phone_status auth={.valid=true,.state=PH_AUTHORIZED,.flags=PHONE_FLAG_REQUIRED};
 home_ui s={0};s.page=HELPER;s.connected=true;
 s.bots.data=(bots_data){.valid=true,.count=3,.received_us=1*S};
 for(int i=0;i<3;i++)s.bots.data.b[i].available=true;
 s.bots.attempts=1;s.bots.attempt_us=1*S;
 home_phone_apply(&s,&auth,true);
 /* 1: minutes-old good frame, signed in, on Wi-Fi -> enabled immediately on entry. */
 for(int i=0;i<3;i++){s.helper.bot=i;home_bots_sync(&s,300*S);assert(s.helper.block==BR_NONE);}
 /* 2: signed in but no bot frame yet (just booted) -> LOADING (spinner), then enabled on the first
  *    answer (user rule 2026-09-30: loading while the board finds out, not disabled). */
 home_ui b={0};b.page=HELPER;b.connected=true;home_phone_apply(&b,&auth,true);
 home_bots_sync(&b,5*S);assert(b.helper.block==BR_LOADING);
 b.bots.data=s.bots.data;b.bots.data.received_us=5*S;home_bots_sync(&b,5*S);assert(b.helper.block==BR_NONE);
 /* 3: one transient poll failure does not flicker the mic off. */
 s.bots.fails=1;s.helper.bot=0;home_bots_sync(&s,300*S);assert(s.helper.block==BR_NONE);
 /* 4: two failures = reconnecting (loading); BOTS_GIVE_UP_FAILS in a row (~30 s, bridge really down) -> off. */
 s.bots.fails=2;home_bots_sync(&s,300*S);assert(s.helper.block==BR_LOADING);
 s.bots.fails=BOTS_GIVE_UP_FAILS;home_bots_sync(&s,300*S);assert(s.helper.block==BR_UPSTREAM);s.bots.fails=0;
 /* 5: real quota exhaustion still says No quota, fresh or stale. */
 s.bots.data.b[1].available=false;s.bots.data.b[1].reason=BR_EXHAUSTED;
 s.helper.bot=1;home_bots_sync(&s,2*S);assert(s.helper.block==BR_EXHAUSTED);
 home_bots_sync(&s,300*S);assert(s.helper.block==BR_EXHAUSTED);
 /* 6: a fresh frame that says the route is down is respected. */
 s.bots.data.b[2].available=false;s.bots.data.b[2].reason=BR_NONE;
 s.helper.bot=2;home_bots_sync(&s,2*S);assert(s.helper.block==BR_UPSTREAM);
 s.bots.data.b[2].reason=BR_SIGNIN;home_bots_sync(&s,300*S);assert(s.helper.block==BR_SIGNIN);
 /* 7: not signed in -> disabled; no Wi-Fi -> disabled. */
 home_ui n={0};n.page=HELPER;n.connected=true;n.bots=s.bots;n.helper.bot=0;
 home_bots_sync(&n,300*S);assert(n.helper.block==BR_UPSTREAM);
 n.phone.st=(phone_status){.valid=true,.state=PH_NONE,.flags=PHONE_FLAG_REQUIRED};
 home_bots_sync(&n,300*S);assert(n.helper.block==BR_PHONE);
 s.connected=false;s.helper.bot=0;home_bots_sync(&s,300*S);assert(s.helper.block==BR_UPSTREAM);
 puts("signed-in entry enables stale-good/no-frame bots; no quota, route-down, sign-out and offline stay blocked: PASS");
 return 0;
}
