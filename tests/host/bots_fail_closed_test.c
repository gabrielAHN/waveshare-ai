#include <assert.h>
#include "bots_view.h"
int main(void){
 assert(!strcmp(bots_reason_name(BR_UPSTREAM),"upstream"));
 assert(BOTS_POLL_US <= 5LL*1000*1000);
 assert(BOTS_RETRY_US <= 5LL*1000*1000);
 assert(BOTS_EVIDENCE_US <= 12LL*1000*1000);
 bots_view v={0};v.data.valid=true;v.data.count=1;v.data.received_us=100;
 bots_entry *e=&v.data.b[0];e->available=true;
 assert(bots_block(&v,0,100)==BR_NONE);
 v.fails=1;assert(bots_block(&v,0,100)==BR_UPSTREAM);v.fails=0;
 e->reason=BR_UPSTREAM;assert(bots_block(&v,0,100)==BR_UPSTREAM);
 e->reason=BR_NONE;e->stale=true;assert(bots_block(&v,0,100)==BR_NONE);e->stale=false;
 e->available=false;e->reason=BR_EXHAUSTED;e->reset_s=0;
 assert(bots_block(&v,0,100)==BR_EXHAUSTED);
 e->reason=BR_NONE;assert(bots_block(&v,0,100)==BR_UPSTREAM);
 v.data.count=0;assert(bots_block(&v,0,100)==BR_UPSTREAM);
 return 0;
}
