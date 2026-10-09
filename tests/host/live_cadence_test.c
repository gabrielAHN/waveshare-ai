#include <assert.h>
#include "live_state.h"
int main(void){live_state s={.valid=true,.count=1,.received_us=100};
 /* 4.3s LAN fetch + 2.5s serialized phone poll + 1.5s cadence is normal. */
 assert(live_active(&s,100+8500LL*1000));
 assert(!live_active(&s,100+10LL*1000*1000));
 s.valid=false;assert(!live_active(&s,101));
 return 0;}
