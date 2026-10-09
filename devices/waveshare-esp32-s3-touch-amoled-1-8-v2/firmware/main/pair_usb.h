#pragma once
/* USB-serial frames for pairing (same CRC-checked family as WLV1/WVC1; LOCAL host->board).
 *
 * WPC1 (9 bytes): 'WPC1' <action:1> <crc32(action):4 LE>. Drives the SAME Settings state machine
 *   as touch (open+search, pick row, cancel, open the phone sign-in QR, press the 1st/2nd target) and
 *   carries the maintenance actions that have no on-screen control any more (forget the bridge,
 *   forget Wi-Fi). Logged as source=usb_serial. It carries no secret and cannot confirm an
 *   enrollment: the host-side confirmation is always required.
 * WLB2: 'WLB2' <pair_usb_bridge> <crc32:4 LE>. Address-and-pin provisioning when mDNS is unavailable:
 *   gives the board a bridge address + certificate pin. The board still enrolls with its own
 *   on-device identity and the host must still confirm the comparison code. */
#include "home_ui.h"
#define PAIR_CMD_OPEN_SCAN 1
#define PAIR_CMD_CANCEL 2
#define PAIR_CMD_FORGET 3
#define PAIR_CMD_PHONE 4          /* open the phone sign-in QR (fresh code); enrolled boards only */
#define PAIR_CMD_BUTTON_A 5       /* press the first (top) large Settings target (test hook, like a tap) */
#define PAIR_CMD_BUTTON_B 6       /* press the second large Settings target */
#define PAIR_CMD_FORGET_WIFI 7    /* forget the saved Wi-Fi network (no on-screen control) */
/* Command values outside the current allowlist are rejected. */
#define PAIR_CMD_ROW0 0x10
typedef struct {unsigned char data[9];size_t used;} pair_cmd_parser;
static inline bool pair_cmd_known(unsigned char a){return a==PAIR_CMD_OPEN_SCAN||a==PAIR_CMD_CANCEL||a==PAIR_CMD_FORGET||a==PAIR_CMD_PHONE||a==PAIR_CMD_BUTTON_A||a==PAIR_CMD_BUTTON_B||a==PAIR_CMD_FORGET_WIFI||(a>=PAIR_CMD_ROW0&&a<PAIR_CMD_ROW0+PAIR_ROWS);}
static inline void pair_cmd_encode(unsigned char a,unsigned char out[9]){memcpy(out,"WPC1",4);out[4]=a;uint32_t crc=provision_crc(&a,1);for(int i=0;i<4;i++)out[5+i]=(unsigned char)(crc>>(8*i));}
static inline int pair_cmd_feed(pair_cmd_parser*p,unsigned char byte,unsigned char*action){
 if(p->used<4&&byte!=(unsigned char)"WPC1"[p->used]){p->used=byte=='W'?1:0;if(p->used)p->data[0]=byte;return 0;}
 p->data[p->used++]=byte;if(p->used<sizeof p->data)return 0;
 uint32_t crc=0;for(int i=0;i<4;i++)crc|=(uint32_t)p->data[5+i]<<(8*i);
 bool ok=pair_cmd_known(p->data[4])&&crc==provision_crc(p->data+4,1);if(ok)*action=p->data[4];provision_wipe(p,sizeof *p);return ok?1:-1;
}
/* Caller holds the UI lock. Returns whether the action changed anything. */
static inline bool pair_cmd_apply(home_ui*s,unsigned char a){
 pair_view*v=&s->pair;
 if(a==PAIR_CMD_OPEN_SCAN){s->page=SETTINGS;s->auto_scan=true;s->consumed=true;bool was=v->want_scan;pair_request_scan(v);return v->want_scan&&!was;}
 if(a==PAIR_CMD_BUTTON_A||a==PAIR_CMD_BUTTON_B){
  if(!home_settings_page(s)){s->page=SETTINGS;s->consumed=true;}
  home_settings_tick(s);return home_settings_press(s,a==PAIR_CMD_BUTTON_A?0:1);
 }
 if(a==PAIR_CMD_FORGET_WIFI){if(s->busy||!s->saved)return false;s->action=HOME_FORGET;s->busy=true;return true;}
 if(a==PAIR_CMD_CANCEL){if(pair_view_mode(v)!=PM_ENROLLING)return false;v->want_cancel=true;return true;}
 if(a==PAIR_CMD_FORGET){if(pair_view_mode(v)!=PM_SUMMARY)return false;v->want_forget=true;v->confirm_forget=false;
  for(int k=SETTINGS_HERMES;k<=SETTINGS_HOME_ASSISTANT;k++){phone_view*ph=home_tab_phone(s,k);if(ph)ph->want_forget=true;}  /* also signs the board out */
  return true;}
 if(a==PAIR_CMD_PHONE){
  /* The open provider tab's QR, else the first provider with a sign-in (Hermes, then Home Assistant); a
   * shared sign-in has one tab, the Hermes one (home_signin_tab). */
  int tab=home_signin_tab(s,home_settings_page(s)&&home_provider_tab(s->settings_tab)?s->settings_tab:(home_provider_signin(SETTINGS_HERMES)?SETTINGS_HERMES:SETTINGS_HOME_ASSISTANT));
  const phone_view*ph=home_tab_phone_c(s,tab);
  if(!ph||!home_provider_signin(tab)||ph->absent)return false;
  if(v->state<PAIR_ENROLLED_UNPAIRED||v->step==PV_ENROLLING||v->step==PV_SCANNING)return false;
  if(phone_signed_in(ph))return false;  /* a new flow would put an authorized board back to "pending" */
  s->page=SETTINGS;s->settings_tab=(settings_page)tab;s->auto_qr=true;s->consumed=true;v->step=PV_IDLE;v->confirm_forget=false;
  home_phone_start(s,tab);return true;
 }
 return pair_request_enroll(v,a-PAIR_CMD_ROW0);
}
typedef struct {char base[PAIR_URL_MAX],fp[65],name[PAIR_NAME_MAX+1];} pair_usb_bridge;
#define PAIR_USB_BRIDGE_FRAME (4+sizeof(pair_usb_bridge)+4)
typedef struct {unsigned char data[PAIR_USB_BRIDGE_FRAME];size_t used;} pair_usb_bridge_parser;
static inline bool pair_usb_bridge_valid(const pair_usb_bridge*b){
 unsigned char fp[32];
 if(!memchr(b->base,0,sizeof b->base)||!memchr(b->fp,0,sizeof b->fp)||!memchr(b->name,0,sizeof b->name))return false;
 return pair_base_valid(b->base)&&pair_hex32(b->fp,fp)&&pair_fp_set(fp);
}
static inline void pair_usb_bridge_encode(const pair_usb_bridge*b,unsigned char*out){memcpy(out,"WLB2",4);memcpy(out+4,b,sizeof *b);uint32_t crc=provision_crc(b,sizeof *b);for(int i=0;i<4;i++)out[4+sizeof *b+i]=(unsigned char)(crc>>(8*i));}
static inline int pair_usb_bridge_feed(pair_usb_bridge_parser*p,unsigned char byte,pair_usb_bridge*out){
 if(p->used<4&&byte!=(unsigned char)"WLB2"[p->used]){p->used=byte=='W'?1:0;if(p->used)p->data[0]=byte;return 0;}
 p->data[p->used++]=byte;if(p->used<PAIR_USB_BRIDGE_FRAME)return 0;
 pair_usb_bridge b;memcpy(&b,p->data+4,sizeof b);uint32_t crc=0;for(int i=0;i<4;i++)crc|=(uint32_t)p->data[4+sizeof b+i]<<(8*i);
 bool ok=crc==provision_crc(&b,sizeof b)&&pair_usb_bridge_valid(&b);if(ok)*out=b;provision_wipe(p,sizeof *p);return ok?1:-1;
}
/* Inject the bridge as a Settings-list candidate (as if found by mDNS). Nothing is stored or trusted
 * until the user picks it and confirms the comparison code on the host. Caller holds the UI lock. */
static inline bool pair_usb_bridge_apply(home_ui*s,const pair_usb_bridge*b){
 unsigned char fp[32];pair_view*v=&s->pair;
 if(!pair_usb_bridge_valid(b)||v->step==PV_ENROLLING||v->step==PV_SCANNING||!pair_hex32(b->fp,fp))return false;
 if(v->step!=PV_LIST)v->list.count=0;
 if(pair_list_add_base(&v->list,b->name,b->base,fp)<0)return false;
 s->page=SETTINGS;s->auto_scan=true;s->consumed=true;v->step=PV_LIST;v->note[0]=0;return true;
}
