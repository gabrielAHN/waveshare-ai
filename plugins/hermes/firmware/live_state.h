#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#define LIVE_MAX 128
/* Bounded allowance for one serialized phone/bot fetch plus live TLS and poll
 * cadence. Explicit transport failure/empty roster still clears immediately. */
#define LIVE_TTL_US (10LL*1000*1000)
#define LIVE_WIRE_MAX (8+LIVE_MAX*10)
/* Session token-density level 0..5 (six steps), carried in the WLS4 header. */
#define LIVE_LEVELS 6
#define LIVE_FLAG_MEASURED 1u  /* level derived from measured token deltas   */
#define LIVE_FLAG_DEGRADED 2u  /* usage sampling currently failing/stale     */
#define LIVE_FLAGS_KNOWN (LIVE_FLAG_MEASURED|LIVE_FLAG_DEGRADED)
enum {LIVE_PROVIDER_UNKNOWN=0,LIVE_PROVIDER_ANTHROPIC=1,LIVE_PROVIDER_OPENAI_CODEX=2,LIVE_PROVIDER_OPENROUTER=3,LIVE_PROVIDER_MAX=3};
/* count = every open session; idle[i] = 1 while that session is not running a turn.
 * idle_count = how many are idle. The ambient density follows only the working ones. */
typedef struct {uint64_t ids[LIVE_MAX];uint8_t providers[LIVE_MAX],idle[LIVE_MAX];int64_t received_us;uint16_t count,idle_count;uint8_t level,flags;bool valid;} live_state;
static inline unsigned live_working(const live_state*s){return s->count>s->idle_count?(unsigned)(s->count-s->idle_count):0u;}
static inline bool live_fresh(const live_state*s,int64_t now){return s->valid&&now>=s->received_us&&now-s->received_us<LIVE_TTL_US;}
static inline bool live_active(const live_state*s,int64_t now){return live_fresh(s,now)&&live_working(s)>0;}
/* TLS and endpoint authorization are checked by the transport before decoding.
 * Invalid/overflow/incomplete responses never masquerade as an empty roster.
 * Wire: 'WLS4', u16 count, u8 level<=5, u8 flags (only LIVE_FLAGS_KNOWN bits),
 * then ascending unique nonzero (u64 id, u8 provider, u8 state) records.
 * Provider 0 is unknown; state bit 0 = working (other bits 0).
 * With no working session the frame must carry level 0 (calm). */
static inline bool live_decode(live_state*s,const unsigned char*p,size_t n,int64_t now){
 if(!s||!p||n<8)return false;
 if(memcmp(p,"WLS4",4))return false;
 unsigned count=p[4]|((unsigned)p[5]<<8),stride=10;if(count>LIVE_MAX||n!=8+count*stride)return false;
 unsigned level=p[6],flags=p[7];
 if(level>LIVE_LEVELS-1||(flags&~LIVE_FLAGS_KNOWN)||(!count&&level))return false;
 uint64_t previous=0;unsigned working=0;
 for(unsigned i=0;i<count;i++){uint64_t id=0;for(unsigned j=0;j<8;j++)id|=(uint64_t)p[8+i*stride+j]<<(8*j);
  if(!id||(i&&id<=previous)||p[16+i*stride]>LIVE_PROVIDER_MAX)return false;
  if(p[17+i*stride]>1)return false;
  working+=p[17+i*stride];
  previous=id;}
 if(!working&&level)return false;
 memset(s,0,sizeof *s);s->count=(uint16_t)count;s->idle_count=(uint16_t)(count-working);s->received_us=now;s->valid=true;
 s->level=(uint8_t)level;s->flags=(uint8_t)flags;
 for(unsigned i=0;i<count;i++){for(unsigned j=0;j<8;j++)s->ids[i]|=(uint64_t)p[8+i*stride+j]<<(8*j);
  s->providers[i]=p[16+i*stride];
  s->idle[i]=!p[17+i*stride];}
 return true;
}
