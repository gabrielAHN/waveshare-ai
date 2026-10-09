#include <assert.h>
#include <stdio.h>
#include "live_view.h"
static int send(live_view_parser*p,unsigned char v,unsigned char*page){unsigned char k[9]={'W','L','V','1',v};uint32_t crc=provision_crc(k+4,1);for(int i=0;i<4;i++)k[5+i]=(unsigned char)(crc>>(8*i));int r=0;for(int i=0;i<9;i++){r=live_view_feed(p,k[i],page);if(i<8)assert(r==0);}return r;}
int main(void){live_view_parser p={0};unsigned char page=9;
 for(unsigned char v=0;v<=3;v++){assert(send(&p,v,&page)==1&&page==v);}
 assert(send(&p,6,&page)==1&&page==6);  /* Sensor page */
 page=7;assert(send(&p,4,&page)==-1&&page==7);assert(send(&p,5,&page)==-1&&page==7);assert(send(&p,200,&page)==-1&&page==7);
 unsigned char bad[9]={'W','L','V','1',1,0,0,0,0};int r=0;for(int i=0;i<9;i++)r=live_view_feed(&p,bad[i],&page);assert(r==-1&&page==7);
 puts("Physical USB service selection (0-3 incl. Settings, 6 Sensor) is bounded, CRC-checked and separate from touch: PASS");}
