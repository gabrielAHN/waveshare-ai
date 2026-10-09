#include <assert.h>
#include <stdio.h>
#include "provision.h"
int main(void){
 provision_config c={0},out={0};strcpy(c.wifi.ssid,"test-fixture");memset(c.wifi.password,'x',8);
 provision_parser p={0};unsigned char frame[PROVISION_FRAME_SIZE];provision_encode(&c,frame);
 for(size_t i=0;i<sizeof frame;i++)assert(provision_feed(&p,frame[i],&out)==(i==sizeof frame-1?1:0));
 assert(!memcmp(&c,&out,sizeof c));assert(p.used==0);
 frame[20]^=1;int result=0;for(size_t i=0;i<sizeof frame;i++)result=provision_feed(&p,frame[i],&out);assert(result==-1);
 c.reserved[224]=1;assert(!provision_valid(&c));c.reserved[224]=0;assert(provision_valid(&c));
 /* reserved[0] = which network (user rule 2026-09-30): 0 home (old frames), 1 iPhone hotspot. */
 c.reserved[0]=PROVISION_NET_HOTSPOT;assert(provision_valid(&c)&&provision_network(&c)==PROVISION_NET_HOTSPOT);
 c.reserved[0]=2;assert(!provision_valid(&c));
 c.reserved[0]=0;assert(provision_network(&c)==PROVISION_NET_HOME);
 memset(c.wifi.ssid,'x',sizeof c.wifi.ssid);assert(!provision_valid(&c));
 for(int i=0;i<2000;i++)assert(provision_feed(&p,'x',&out)==0);
 puts("USB provisioning framing, CRC, bounds, reserved-byte validation and resynchronization: PASS");
}
