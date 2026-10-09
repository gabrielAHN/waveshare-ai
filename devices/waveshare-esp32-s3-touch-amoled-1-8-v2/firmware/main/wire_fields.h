#pragma once
/* Shared bounded fields for the current bridge frames and relative reset countdowns. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define WIRE_NONE32 0xFFFFFFFFu
static inline uint32_t wire_u32(const unsigned char*p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static inline bool wire_text(char*out,const unsigned char*p,size_t n){
 if(p[n-1])return false;
 size_t len=0;while(len<n&&p[len])len++;
 for(size_t i=0;i<len;i++)if(p[i]<32||p[i]>126)return false;
 memcpy(out,p,len);memset(out+len,0,n-len);return true;
}
/* Seconds left, counted down locally since the body arrived (monotonic, never negative). */
static inline uint32_t wire_remaining(uint32_t reset_s,int64_t received_us,int64_t now_us){
 if(reset_s==WIRE_NONE32)return WIRE_NONE32;
 int64_t gone=now_us>received_us?(now_us-received_us)/1000000:0;
 return gone>=(int64_t)reset_s?0:(uint32_t)(reset_s-gone);
}
static inline void wire_duration(uint32_t s,char*out,size_t cap){
 if(s>=86400u)snprintf(out,cap,"%lud %02luh",(unsigned long)(s/86400u),(unsigned long)(s%86400u/3600u));
 else if(s>=3600u)snprintf(out,cap,"%luh %02lum",(unsigned long)(s/3600u),(unsigned long)(s%3600u/60u));
 else if(s>=60u)snprintf(out,cap,"%lum",(unsigned long)(s/60u));
 else snprintf(out,cap,"<1m");
}
static inline void wire_reset_text(uint32_t s,char*out,size_t cap){
 if(!out||!cap)return;
 if(s==WIRE_NONE32){snprintf(out,cap,"no reset");return;}
 if(!s){snprintf(out,cap,"resetting now");return;}
 /* Bounded concat (no snprintf %s into a smaller region: IDF builds with -Werror=format-truncation). */
 static const char prefix[]="resets in ";size_t n=sizeof prefix-1;
 if(cap<=n){memcpy(out,prefix,cap-1);out[cap-1]=0;return;}
 memcpy(out,prefix,n);wire_duration(s,out+n,cap-n);
}
