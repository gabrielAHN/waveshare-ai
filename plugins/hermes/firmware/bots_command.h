#pragma once
#include "provision.h"
/* WBS1: test-only USB hook for the Ask-page bot swipe, in the existing CRC-checked USB family
 * (WLV1/WVC1). LOCAL host->board frame; drives the SAME helper_swipe() as a finger and is
 * logged `source=usb_serial`, never as touch. See tools/swipe_bot.py.
 * Frame (9 bytes): 'W' 'B' 'S' '1' <dir:1> <crc32(dir):4 LE>; dir 1 = next bot (swipe left),
 * 2 = previous bot (swipe right). Anything else, a bad CRC or a malformed prefix is rejected (-1). */
typedef struct {unsigned char data[9];size_t used;} bots_cmd_parser;
static inline int bots_cmd_feed(bots_cmd_parser*p,unsigned char byte,unsigned char*dir){
 if(p->used<4&&byte!=(unsigned char)"WBS1"[p->used]){p->used=byte=='W'?1:0;if(p->used)p->data[0]=byte;return 0;}
 p->data[p->used++]=byte;if(p->used<sizeof p->data)return 0;
 uint32_t crc=0;for(int i=0;i<4;i++)crc|=(uint32_t)p->data[5+i]<<(8*i);
 bool ok=(p->data[4]==1||p->data[4]==2)&&crc==provision_crc(p->data+4,1);if(ok)*dir=p->data[4];provision_wipe(p,sizeof *p);return ok?1:-1;
}

/* WGS1: test-only USB Ask-page gesture, same frame shape as WBS1 ('W' 'G' 'S' '1' <g:1> <crc32(g):4 LE>).
 * The board replays it as REAL touch samples through home_sample() (the finger path):
 *   1 = chat drag up (pull for a new chat; only from the newest message), 2 = chat drag down (older),
 *   3 = the Home pull (drag down from the lower part; go Home), 4 = report only (HELPER_CHAT line, no gesture),
 *   5 = on Home: tile swipes to the Ask tile + tap it (open Ask the way a finger does),
 *   15 = on Ask: pull down from the top band to the centre (new chat).
 * The touch sequences live in gesture_replay.h. */
#define GESTURE_NEW 1
#define GESTURE_OLDER 2
#define GESTURE_HOME 3
#define GESTURE_REPORT 4
#define GESTURE_OPEN_ASK 5
#define GESTURE_SPARKLE_NEXT 13
#define GESTURE_SPARKLE_PREV 14
#define GESTURE_TOP_NEW 15      /* Ask page: swipe from the top edge down to the centre = new chat */
#define GESTURE_LAST GESTURE_TOP_NEW
typedef struct {unsigned char data[9];size_t used;} gesture_cmd_parser;
static inline int gesture_cmd_feed(gesture_cmd_parser*p,unsigned char byte,unsigned char*g){
 if(p->used<4&&byte!=(unsigned char)"WGS1"[p->used]){p->used=byte=='W'?1:0;if(p->used)p->data[0]=byte;return 0;}
 p->data[p->used++]=byte;if(p->used<sizeof p->data)return 0;
 uint32_t crc=0;for(int i=0;i<4;i++)crc|=(uint32_t)p->data[5+i]<<(8*i);
 bool ok=((p->data[4]>=1&&p->data[4]<=5)||(p->data[4]>=13&&p->data[4]<=GESTURE_LAST))&&crc==provision_crc(p->data+4,1);if(ok)*g=p->data[4];provision_wipe(p,sizeof *p);return ok?1:-1;
}
