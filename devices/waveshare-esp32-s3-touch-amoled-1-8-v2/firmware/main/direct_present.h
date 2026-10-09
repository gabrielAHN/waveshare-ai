#pragma once
#include <stdlib.h>
#include "adapter.h"
#define DIRECT_STRIPE 16
#define DIRECT_DMA_BYTES (368*DIRECT_STRIPE*2)
/* One transfer in flight. Pack into the OTHER internal DMA buffer while it
 * runs, then wait for its callback before submission. No queued ownership
 * ambiguity; final wait is part of successful presentation. On ANY failure
 * caller must halt without freeing/reusing either buffer or the shadow. */
typedef int (*direct_send_fn)(void*,int,int,const uint8_t*);
typedef int (*direct_wait_fn)(void*);
static inline int direct_present(const uint16_t *shadow,uint8_t *a,uint8_t *b,size_t capacity,
 direct_send_fn send,direct_wait_fn wait,void *ctx){
 if(!shadow||!a||!b||a==b||capacity<DIRECT_DMA_BYTES||!send||!wait)return 0;
 int flight=0;
 for(int y=0;y<448;y+=DIRECT_STRIPE){
  uint8_t *next=((y/DIRECT_STRIPE)&1)?b:a;
  pack_window(shadow,next,0,y,368,DIRECT_STRIPE);
  if(flight&&!wait(ctx))return 0;
  if(!send(ctx,y,DIRECT_STRIPE,next))return 0;
  flight=1;
 }
 return wait(ctx);
}
