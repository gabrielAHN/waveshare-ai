#pragma once
#include "provision.h"
typedef struct {unsigned char data[9];size_t used;} live_view_parser;
/* Physical boot-window command selects a service, never synthesizes contact. */
static inline int live_view_feed(live_view_parser*p,unsigned char byte,unsigned char*page){
 if(p->used<4&&byte!=(unsigned char)"WLV1"[p->used]){p->used=byte=='W'?1:0;if(p->used)p->data[0]=byte;return 0;}
 p->data[p->used++]=byte;if(p->used<sizeof p->data)return 0;
 uint32_t crc=0;for(int i=0;i<4;i++)crc|=(uint32_t)p->data[5+i]<<(8*i);
 /* Public page IDs: 0 Home, 1 Sparkles, 2 Settings, 3 Ask, 6 Sensor. */
 bool ok=(p->data[4]<=3||p->data[4]==6)&&crc==provision_crc(p->data+4,1);if(ok)*page=p->data[4];provision_wipe(p,sizeof *p);return ok?1:-1;
}
