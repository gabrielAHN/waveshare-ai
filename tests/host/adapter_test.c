#include <assert.h>
#include <stdio.h>
#include "adapter.h"
int main(void) {
 uint16_t shadow[368*4]={0}; unsigned char out[16]={0};
 shadow[1*368+2]=0x1234; shadow[1*368+3]=0xabcd;
 shadow[2*368+2]=0x5678; shadow[2*368+3]=0xef01;
 pack_window(shadow,out,2,1,2,2);
 assert(out[0]==0x12 && out[1]==0x34 && out[2]==0xab && out[3]==0xcd);
 assert(out[4]==0x56 && out[5]==0x78 && out[6]==0xef && out[7]==1);
 puts("packing stride/endian PASS");
}
