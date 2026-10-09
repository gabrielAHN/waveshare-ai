#pragma once
#include "live_state.h"
typedef struct {unsigned char data[LIVE_WIRE_MAX];size_t length;bool media_ok,overflow;} live_http_body;
static inline bool live_body_append(live_http_body*b,const void*data,int n){
 if(b->overflow||n<0||b->length>sizeof b->data||(size_t)n>sizeof b->data-b->length||(!data&&n)){b->overflow=true;return false;}
 if(n){memcpy(b->data+b->length,data,(size_t)n);}
 b->length+=(size_t)n;return true;
}
static inline bool live_body_finish(const live_http_body*b,int status,int64_t now,live_state*s){
 if(status!=200||b->overflow||!b->media_ok||!live_decode(s,b->data,b->length,now)){s->valid=false;return false;}return true;
}
