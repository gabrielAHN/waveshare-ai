#pragma once
/* Pure Settings UI vocabulary. State transitions stay in home_ui.h; rendering stays in
 * home_render.h. There is deliberately no keyboard or editable credential field. */
#define SET_EDGE 16
#define SET_PANEL_R 56
/* The bottom SET_HOME_BAND rows stay empty on every Settings screen (the panel's rounded bottom; the
 * lowest rows of the Home pull zone, home_ui.h HOME_PULL_ZONE_Y). */
#define SET_HOME_BAND 28
#define SET_BOTTOM_Y (448-SET_HOME_BAND)
#define SET_X 24
#define SET_W 320
#define SET_PAD 16
#define SET_BTN_MIN_H 72
#define SET_BTN_MIN_W 140
#define SET_BTN_H 76
#define SET_ROW_H 96
#define SET_QR_BACK_W 140
#define SET_QR_CODE_W (SET_W-SET_QR_BACK_W-12)
#define SET_SLOT_A 248
#define SET_SLOT_B 336
#define SETTINGS_SWIPE_PX 56
#define SETTINGS_CANCEL_PX 14

/* Swipe order: the device core (Wi-Fi; Display, the colour theme (theme.h): Light / Dark and the accent;
 * Sound when a Hermes tile is built; Battery, which shows the charge and, while charging, the time to
 * full), then one tab per provider built (plugins.h), each that provider's own phone sign-in, reachable
 * once Wi-Fi is up. */
typedef enum {SETTINGS_WIFI,SETTINGS_DISPLAY,SETTINGS_SOUND,SETTINGS_BATTERY,SETTINGS_HERMES,SETTINGS_HOME_ASSISTANT,SETTINGS_TABS} settings_page;
typedef enum {SS_NO_WIFI,SS_JOINING,SS_FIND,SS_CODE,SS_QR,SS_STATUS,SS_SIGNOUT} settings_screen;
typedef enum {SA_NONE,SA_SCAN,SA_PICK0,SA_PICK1,SA_CANCEL,SA_RETRY,SA_BACK,SA_SIGNIN,
 SA_SIGNOUT_ASK,SA_SIGNOUT_YES,SA_SIGNOUT_NO,SA_SOUND,SA_SPARKLE,SA_THEME,SA_ACCENT} settings_action;
/* What a row draws in the switch slot instead of a switch (Settings > Display). */
typedef enum {SET_SWATCH_NONE,SET_SWATCH_THEME,SET_SWATCH_ACCENT} settings_swatch;

#define SETTINGS_MAX_BUTTONS 3
#define SET_LABEL_LEN 36
typedef struct {
 int x,y,w,h,action;
 bool primary,toggle,on;
 unsigned char swatch;  /* settings_swatch */
 const char*caption;
 char label[SET_LABEL_LEN];
} settings_target;
typedef struct {int count;settings_target t[SETTINGS_MAX_BUTTONS];} settings_buttons;

static inline bool set_safe_px(int x,int y){
 if(x<SET_EDGE||x>=368-SET_EDGE||y<SET_EDGE||y>=SET_BOTTOM_Y)return false;
 int cx=x<SET_PANEL_R?SET_PANEL_R:(x>=368-SET_PANEL_R?368-SET_PANEL_R-1:x);
 int cy=y<SET_PANEL_R?SET_PANEL_R:(y>=448-SET_PANEL_R?448-SET_PANEL_R-1:y);
 int dx=x-cx,dy=y-cy,r=SET_PANEL_R-SET_EDGE;return dx*dx+dy*dy<=r*r;
}
/* Tab names; a provider tab's name is also its X-Provider header value on the bridge's sign-in routes. */
static inline const char*settings_tab_name(int p){
 static const char*n[]={"wifi","display","sound","battery","hermes","home_assistant"};return p>=SETTINGS_WIFI&&p<SETTINGS_TABS?n[p]:"?";
}
