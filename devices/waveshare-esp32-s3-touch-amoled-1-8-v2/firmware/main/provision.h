#pragma once
#include <stdint.h>
#include "home_credentials.h"
typedef struct {home_credentials wifi;unsigned char reserved[225];} provision_config;
#define PROVISION_FRAME_SIZE (4+sizeof(provision_config)+4)
typedef struct {unsigned char bytes[PROVISION_FRAME_SIZE];size_t used;} provision_parser;
static inline void provision_wipe(void*p,size_t n){volatile unsigned char*q=p;while(n--)*q++=0;}
static inline uint32_t provision_crc(const void*ptr,size_t size){const unsigned char*p=ptr;uint32_t crc=~0u;while(size--){crc^=*p++;for(int k=0;k<8;k++)crc=(crc>>1)^((0u-(crc&1))&0xedb88320u);}return ~crc;}
/* reserved[0] = which saved network this is (user rule 2026-09-30: home Wi-Fi first, iPhone
 * Personal Hotspot as the backup). Network 0 = home, network 1 = hotspot. */
#define PROVISION_NET_HOME 0
#define PROVISION_NET_HOTSPOT 1
static inline int provision_network(const provision_config*c){return c->reserved[0];}
static inline bool provision_valid(const provision_config*c){
 if(!home_credentials_valid(&c->wifi))return false;
 if(c->reserved[0]>PROVISION_NET_HOTSPOT)return false;
 for(size_t i=1;i<sizeof c->reserved;i++)if(c->reserved[i])return false;
 return true;
}
static inline void provision_encode(const provision_config*c,unsigned char*out){
 memcpy(out,"WSP1",4);memcpy(out+4,c,sizeof *c);uint32_t crc=provision_crc(c,sizeof *c);
 for(int i=0;i<4;i++)out[4+sizeof *c+i]=(unsigned char)(crc>>(8*i));
}
static inline int provision_feed(provision_parser*p,unsigned char byte,provision_config*out){
 if(p->used<4&&byte!=(unsigned char)"WSP1"[p->used]){p->used=byte=='W'?1:0;if(p->used)p->bytes[0]=byte;return 0;}
 p->bytes[p->used++]=byte;if(p->used<PROVISION_FRAME_SIZE)return 0;
 provision_config temp;memcpy(&temp,p->bytes+4,sizeof temp);uint32_t crc=0;for(int i=0;i<4;i++)crc|=(uint32_t)p->bytes[4+sizeof temp+i]<<(8*i);
 bool ok=crc==provision_crc(&temp,sizeof temp)&&provision_valid(&temp);
 if(ok)*out=temp;
 provision_wipe(&temp,sizeof temp);provision_wipe(p,sizeof *p);return ok?1:-1;
}
