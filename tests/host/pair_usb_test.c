#include <assert.h>
#include <stdio.h>
#include "pair_usb.h"
static int feed_all(pair_cmd_parser*p,const unsigned char*b,size_t n,unsigned char*out){int r=0;for(size_t i=0;i<n;i++){r=pair_cmd_feed(p,b[i],out);if(i+1<n)assert(r==0);}return r;}
int main(void){
 /* WPC1: USB equivalent of Connect-page taps (open+scan, pick row i, cancel, forget). */
 unsigned char f[9],v=0;pair_cmd_parser p={0};
 unsigned char ok[]={PAIR_CMD_OPEN_SCAN,PAIR_CMD_ROW0,PAIR_CMD_ROW0+3,PAIR_CMD_CANCEL,PAIR_CMD_FORGET,PAIR_CMD_BUTTON_A,PAIR_CMD_BUTTON_B,PAIR_CMD_FORGET_WIFI};
 for(size_t k=0;k<sizeof ok;k++){pair_cmd_encode(ok[k],f);assert(feed_all(&p,f,9,&v)==1&&v==ok[k]);}
 pair_cmd_encode(PAIR_CMD_ROW0+4,f);assert(feed_all(&p,f,9,&v)==-1); /* only 4 visible rows */
 pair_cmd_encode(0,f);assert(feed_all(&p,f,9,&v)==-1);
 pair_cmd_encode(8,f);assert(feed_all(&p,f,9,&v)==-1);  /* 8 = the retired iPhone (Matter) handover */
 pair_cmd_encode(PAIR_CMD_CANCEL,f);f[6]^=1;assert(feed_all(&p,f,9,&v)==-1);
 /* stray prefix resynchronises */
 unsigned char noisy[12]={'W','P','X','W'};pair_cmd_encode(PAIR_CMD_OPEN_SCAN,f);memcpy(noisy+3,f,9);
 int r=0;for(int i=0;i<12;i++)r=pair_cmd_feed(&p,noisy[i],&v);assert(r==1&&v==PAIR_CMD_OPEN_SCAN);
 /* apply: same state machine as touch */
 home_ui s={0};s.page=HOME;assert(pair_cmd_apply(&s,PAIR_CMD_OPEN_SCAN)&&s.page==SETTINGS&&s.pair.want_scan&&s.pair.step==PV_SCANNING);
 s.pair.want_scan=false;s.pair.step=PV_LIST;pair_list_add(&s.pair.list,"host","192.0.2.10",8098,"479ad1dc62451c82f1cb229bf5bf629f1e2eb88b79d0307b6dcf1898a0cce5f7");
 assert(!pair_cmd_apply(&s,PAIR_CMD_ROW0+1));assert(pair_cmd_apply(&s,PAIR_CMD_ROW0)&&s.pair.enroll_requested&&s.pair.want_enroll==0);
 assert(pair_cmd_apply(&s,PAIR_CMD_CANCEL)&&s.pair.want_cancel);
 s.pair.step=PV_DONE;s.pair.state=PAIR_ENROLLED_UNPAIRED;assert(pair_cmd_apply(&s,PAIR_CMD_FORGET)&&s.pair.want_forget);
 /* WLB2: documented USB fallback carrying bridge address + certificate pin (no board secret). */
 pair_usb_bridge b={0},out={0};strcpy(b.base,"https://192.0.2.10:8098");strcpy(b.fp,"479ad1dc62451c82f1cb229bf5bf629f1e2eb88b79d0307b6dcf1898a0cce5f7");strcpy(b.name,"Studio host");
 assert(pair_usb_bridge_valid(&b));unsigned char frame[PAIR_USB_BRIDGE_FRAME];pair_usb_bridge_encode(&b,frame);assert(!memcmp(frame,"WLB2",4));
 pair_usb_bridge_parser bp={0};r=0;for(size_t i=0;i<sizeof frame;i++){r=pair_usb_bridge_feed(&bp,frame[i],&out);if(i+1<sizeof frame)assert(r==0);}assert(r==1&&!memcmp(&b,&out,sizeof b));
 frame[20]^=1;r=0;for(size_t i=0;i<sizeof frame;i++)r=pair_usb_bridge_feed(&bp,frame[i],&out);assert(r==-1);
 pair_usb_bridge bad=b;strcpy(bad.base,"http://x");assert(!pair_usb_bridge_valid(&bad));bad=b;bad.fp[3]='z';assert(!pair_usb_bridge_valid(&bad));
 bad=b;memset(bad.name,'x',sizeof bad.name);assert(!pair_usb_bridge_valid(&bad));
 /* WLB2 only injects a candidate into the Connect list (as if discovered): nothing is stored or trusted until the host confirms. */
 home_ui u={0};u.page=HOME;assert(pair_usb_bridge_apply(&u,&b)&&u.page==SETTINGS&&u.pair.step==PV_LIST&&u.pair.list.count==1);
 assert(!strcmp(u.pair.list.items[0].base,b.base)&&u.pair.list.items[0].fp[0]==0x47&&!strcmp(u.pair.list.items[0].name,"Studio host")&&u.pair.state==PAIR_NO_BRIDGE);
 u.pair.step=PV_ENROLLING;assert(!pair_usb_bridge_apply(&u,&b)); /* never interrupts an enrollment */
 puts("pair_usb: WPC1 connect-page hook, WLB2 address+pin fallback, CRC + resync: PASS");return 0;
}
