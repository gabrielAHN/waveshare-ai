#include <assert.h>
#include <stdio.h>
#include "live_transport.h"
int main(void){unsigned char p[]={'W','L','S','4',1,0,0,0,42,0,0,0,0,0,0,0,0,1};live_http_body b={.media_ok=true};live_state s={0};assert(live_body_append(&b,p,7));assert(live_body_append(&b,p+7,11));assert(live_body_finish(&b,200,100,&s));assert(s.count==1&&s.ids[0]==42);
 assert(!live_body_finish(&b,401,200,&s));assert(!s.valid);b.media_ok=false;assert(!live_body_finish(&b,200,200,&s));b.media_ok=true;b.length=LIVE_WIRE_MAX;assert(!live_body_append(&b,p,1));assert(!live_body_finish(&b,200,200,&s));b=(live_http_body){.media_ok=true};assert(!live_body_append(&b,p,-1));assert(b.overflow);
 puts("Chunked bounded HTTP decoder rejects errors, bad media and overflow without false activity: PASS");}
