#pragma once
/* Settings -> Hermes bridge enrollment model (state only; the Settings screen and its large buttons
 * live in home_ui.h, drawing in home_render.h). The UI never performs network I/O: it raises want_*
 * flags that the low-priority pairing worker (home_pair.c) consumes, and the worker mirrors results
 * back into this view under the UI lock. There is no on-device address entry: bridges are found by
 * mDNS or handed over USB (WLB2, tools/pair_usb.py bridge ...). */
#include "pair_state.h"
#define PAIR_ROWS 4               /* list rows reachable by WPC1 pick 0..3 (the screen shows 2) */
typedef enum {PV_IDLE,PV_SCANNING,PV_LIST,PV_ENROLLING,PV_DONE,PV_ERROR} pair_step;
typedef enum {PM_SEARCH,PM_ENROLLING,PM_SUMMARY} pair_mode;
typedef struct {
 uint8_t state;            /* pair_phase mirrored from NVS by the worker */
 uint8_t step;             /* pair_step */
 bool want_scan,want_cancel,want_forget,enroll_requested,confirm_forget,code_shown;
 int want_enroll;          /* list index, valid while enroll_requested */
 uint32_t code;            /* 6-digit comparison code, shown only while enrolling */
 int live_http;            /* last /v1/live status (0 = none yet) */
 bool live_ok;             /* the last /v1/live poll answered 200 (the Sparkles tile's Hermes link) */
 bool live_down;           /* no 200 for LIVE_GIVE_UP_US since the first failed poll ("Hermes unavailable") */
 uint8_t live_fails;       /* /v1/live polls in a row without a 200 (saturates) */
 int64_t live_fail_since_us; /* board time of the first failed poll in this run (0 = none) */
 pair_list list;
 char bridge[PAIR_NAME_MAX+1],base[PAIR_URL_MAX],note[40];
 unsigned char fp[32];
} pair_view;
/* The Sparkles tile says "Hermes unavailable" after 30 s without an answer, counted from the first
 * failed poll (an outage poll takes its 2 s timeout + the 1.5 s pause: counting polls would be ~70 s).
 * At least LIVE_GIVE_UP_MIN_FAILS polls must have failed, so one slow poll never flips it. */
#define LIVE_GIVE_UP_US (30LL*1000*1000)
#define LIVE_GIVE_UP_MIN_FAILS 3
static inline void pair_live_result(pair_view*v,int http,int64_t now_us){
 v->live_http=http;v->live_ok=http==200;
 if(v->live_ok){v->live_fails=0;v->live_fail_since_us=0;v->live_down=false;return;}
 if(v->live_fails<255)v->live_fails++;
 if(!v->live_fail_since_us)v->live_fail_since_us=now_us?now_us:1;
 v->live_down=v->live_fails>=LIVE_GIVE_UP_MIN_FAILS&&now_us-v->live_fail_since_us>=LIVE_GIVE_UP_US;
}
/* Link dropped (no Wi-Fi, session link switched off): forget the old answer, start over as loading. */
static inline void pair_live_forget(pair_view*v){v->live_ok=v->live_down=false;v->live_fails=0;v->live_fail_since_us=0;}
static inline pair_mode pair_view_mode(const pair_view*v){
 if(v->step==PV_ENROLLING)return PM_ENROLLING;
 if(v->state>=PAIR_ENROLLED_UNPAIRED&&(v->step==PV_IDLE||v->step==PV_DONE))return PM_SUMMARY;
 return PM_SEARCH;
}
static inline void pair_request_scan(pair_view*v){
 if(v->step==PV_SCANNING||v->step==PV_ENROLLING)return;
 v->want_scan=true;v->step=PV_SCANNING;v->list.count=0;v->note[0]=0;v->confirm_forget=false;
}
static inline bool pair_request_enroll(pair_view*v,int index){
 if(v->step!=PV_LIST||index<0||index>=v->list.count||index>=PAIR_ROWS)return false;
 v->want_enroll=index;v->enroll_requested=true;v->step=PV_ENROLLING;v->code_shown=false;v->code=0;v->note[0]=0;return true;
}
