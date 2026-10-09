#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "direct_present.h"
static const uint8_t *flight;static uint8_t saved[368*16*2];
static int sent,waited,fail_send,fail_wait;
static int send_frame(void *ctx,int y,int h,const uint8_t*p){
 (void)ctx;assert(!flight);assert(y==sent*16&&h==16);sent++;
 if(sent==fail_send)return 0;
 flight=p;memcpy(saved,p,sizeof(saved));return 1;
}
static int wait_frame(void *ctx){
 (void)ctx;assert(flight);assert(!memcmp(flight,saved,sizeof(saved)));waited++;
 if(waited==fail_wait)return 0;
 flight=NULL;return 1;
}
int main(void){
 uint16_t *shadow=malloc(368*448*2);assert(shadow);
 for(int i=0;i<368*448;i++)shadow[i]=(uint16_t)i;
 uint8_t *a=malloc(sizeof(saved)+8),*b=malloc(sizeof(saved)+8);memset(a,0xa5,sizeof(saved)+8);memset(b,0xa5,sizeof(saved)+8);
 assert(direct_present(shadow,a+4,b+4,sizeof(saved),send_frame,wait_frame,NULL));
 assert(sent==28&&waited==28&&!flight);
 for(int i=0;i<4;i++)assert(a[i]==0xa5&&b[i]==0xa5&&a[sizeof(saved)+4+i]==0xa5&&b[sizeof(saved)+4+i]==0xa5);
 sent=waited=0;fail_wait=2;assert(!direct_present(shadow,a+4,b+4,sizeof(saved),send_frame,wait_frame,NULL));assert(sent==2&&waited==2&&flight);
 flight=NULL;sent=waited=0;fail_wait=0;fail_send=2;assert(!direct_present(shadow,a+4,b+4,sizeof(saved),send_frame,wait_frame,NULL));assert(sent==2&&waited==1);
 sent=0;assert(!direct_present(shadow,a+4,b+4,sizeof(saved)-1,send_frame,wait_frame,NULL));assert(sent==0);
 free(shadow);free(a);free(b);puts("double stripe lifetime, final completion, guards and fail-closed transfer passed");
}
