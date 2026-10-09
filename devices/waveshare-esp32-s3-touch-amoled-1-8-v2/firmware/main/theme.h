#pragma once
/* Colour themes (Settings > Display): Light or Dark, plus one of five accent colours.
 *
 * Every themed colour of the renderer (home_render.h, power_ui.h) is a ROLE looked up in the small
 * const tables below: one row of neutral roles per mode, one row of accent roles per (mode, accent).
 * Light + accent 0 (Orange) is exactly the palette the firmware always had (pixel-identical; the
 * light column is today's literal colours). Dark keeps page backgrounds pure black (AMOLED pixels
 * are off, which saves battery), cards/rows dark grey and ink light; tests/host/theme_test.c computes the
 * WCAG contrast of every text/background pair a dark page uses (>= 4.5:1, every accent).
 *
 * The accent colours primary buttons, the switch "on" track, the Settings tab indicator, spinners,
 * the Ask listening bars / link highlights and the power-off countdown digit. Not themed: the Sparkles
 * page (its own Sea/Sunset styles), the bot art, provider colours, the QR modules (black on white).
 * Sensor status tints keep their hues in both modes (mixed darker for Dark), never the accent.
 *
 * State: home_ui.theme_mode / home_ui.accent, persisted in NVS (namespace home_wifi, key "theme",
 * u8 = mode | accent << 4; missing or invalid = light + accent 0). The renderer runs on one task
 * (the display owner): home_render_page() calls theme_use() first, and the role macros read the
 * RGB565 cache below (a few dozen bytes, no stack use). */
#include <stdbool.h>
#include <stdint.h>

#define THEME_LIGHT 0
#define THEME_DARK 1
#define THEME_MODES 2
#define THEME_ACCENTS 5
#define THEME_NVS_KEY "theme"

typedef struct {uint8_t r,g,b;} theme_rgb;
/* Same packing as sparkles.h sp_pack_lcd (native LCD RGB565, red in the high bits). */
static inline uint16_t theme_565(theme_rgb c){return (uint16_t)(((c.r>>3)<<11)|((c.g>>2)<<5)|(c.b>>3));}

/* Accents (index: name, RGB). 0 Orange (today's HH_ORANGE) is the default. */
static const char*const theme_accent_names[THEME_ACCENTS]={"Orange","Blue","Green","Pink","Purple"};
static const theme_rgb theme_accent_rgb[THEME_ACCENTS]={{238,112,28},{64,132,240},{46,164,104},{226,82,146},{138,96,228}};
static inline int theme_accent_clamp(int a){return a>=0&&a<THEME_ACCENTS?a:0;}
static inline int theme_mode_clamp(int m){return m==THEME_DARK?THEME_DARK:THEME_LIGHT;}
static inline const char*theme_accent_name(int a){return theme_accent_names[theme_accent_clamp(a)];}
static inline theme_rgb theme_accent_color(int a){return theme_accent_rgb[theme_accent_clamp(a)];}
static inline const char*theme_mode_name(int m){return theme_mode_clamp(m)==THEME_DARK?"Dark":"Light";}

/* NVS value: mode in the low nibble, accent in the high nibble. Anything else decodes as light +
 * accent 0 and returns false (a corrupt or future value never picks a colour by accident). */
static inline uint8_t theme_nvs_encode(int mode,int accent){
 return (uint8_t)(theme_mode_clamp(mode)|(theme_accent_clamp(accent)<<4));
}
static inline bool theme_nvs_decode(uint8_t v,uint8_t*mode,uint8_t*accent){
 unsigned m=v&0x0fu,a=(unsigned)v>>4;bool ok=m<THEME_MODES&&a<THEME_ACCENTS;
 *mode=ok?(uint8_t)m:(uint8_t)THEME_LIGHT;*accent=ok?(uint8_t)a:(uint8_t)0;
 return ok;
}

/* Roles. TC_* are neutral (one value per mode); TA_* follow the accent (one value per mode and accent). */
typedef enum {
 TC_PAGE,          /* Settings page (PHONE_BG) */
 TC_ROW,           /* Settings rows / non-primary buttons (PHONE_PILL) */
 TC_DOT,           /* Settings tab dots (inactive), the search ring, spinner tracks */
 TC_INK,           /* main text (HH_TEXT) */
 TC_MUTED,         /* secondary text (HH_MUTED) */
 TC_ERROR,         /* error text (HH_ERROR) */
 TC_CARD,          /* raised surfaces: pairing code card */
 TC_KNOB,          /* switch knob, accent swatch rim */
 TC_OFF,           /* switch off track, a blocked bot's dot, the battery "?" (HH_OFF_INK) */
 TC_SHADOW,        /* halo / outline behind text drawn over the Ask orbs (HH_SHADOW) */
 TC_GLASS,         /* Ask bubble + bot pill glass tint (HH_BOT) */
 TC_PAPER,         /* the orbs' paper (strip uncovered while the Ask page slides) */
 TC_USER,          /* your words: the user bubble (HH_USER); USER, USER_DOT, USER_SHADOW follow the accent */
 TC_USER_INK,      /* text in the user bubble */
 TC_USER_DOT,      /* the user bubble's waiting dots (dim phase) */
 TC_USER_SHADOW,   /* the user bubble's drop shadow */
 TC_SCROLL,        /* chat scroll bar */
 TC_BOT_DOT,       /* Ask header: the other bots' dots */
 TC_HOME_BG,       /* Home background behind the tiles */
 TC_HOME_INK,      /* Home tile label + Material icon ink (HC_TEXT) */
 TC_TILE_OFF_PAPER,/* a closed (Hermes-required) tile */
 TC_TILE_OFF_INK,
 TC_TILE_OFF_PILL, /* the tile's status pill */
 TC_TILE_SPIN,     /* the pill spinner's track */
 TC_BATT_SHELL,    /* Settings > Battery case (known charge) */
 TC_BATT_DEEP,
 TC_BATT_GROUND,   /* its soft ground shadow */
 TC_BATT_SHELL_OFF,/* case, unknown charge */
 TC_BATT_DEEP_OFF,
 TC_BATT_WINDOW,   /* the window the charge fills */
 TC_BATT_GLOSS,    /* gloss streak on the case */
 TC_BATT_DARK,     /* deep shade under the charge + the bolt's outline */
 TC_POWER_CARD,    /* power-off countdown card */
 TC_POWER_INK,     /* its "Turning off" */
 TC_SN_PAGE,       /* Sensor page */
 TC_SN_INK,
 TC_SN_MUTED,
 TC_SN_CARD,       /* neutral reading tile (no verdict) */
 TC_SN_RULE,       /* neutral gauge track */
 TC_SN_CORE,       /* gauge marker core */
 TC_SN_TINT,       /* the base the status tints are mixed from */
 TC_NEUTRALS,
 TA_FILL=TC_NEUTRALS, /* the accent: primary buttons, switch on, tab dot, spinner head, listening bars */
 TA_TINT,          /* soft accent tint: pressed controls, dim waiting dots */
 TA_ON,            /* text on an accent fill (primary button label / caption) */
 TA_TEXT,          /* the accent as text or a glyph on the page (hints, countdown digit) */
 TA_FULL,          /* Settings > Battery charge at 100 % */
 TA_NOTE,          /* power-off countdown note ("Release to cancel") */
 THEME_ROLES
} theme_role;
#define THEME_ACCENT_ROLES (THEME_ROLES-TC_NEUTRALS)

static const theme_rgb theme_neutral[THEME_MODES][TC_NEUTRALS]={
 { /* Light: the original warm palette, value for value */
  [TC_PAGE]={255,248,238},[TC_ROW]={255,228,196},[TC_DOT]={255,228,196},[TC_INK]={74,38,16},[TC_MUTED]={150,86,44},
  [TC_ERROR]={196,52,24},[TC_CARD]={255,255,255},[TC_KNOB]={255,255,255},[TC_OFF]={160,150,140},[TC_SHADOW]={255,248,240},
  [TC_GLASS]={255,255,255},[TC_PAPER]={255,252,247},[TC_USER]={240,104,24},[TC_USER_INK]={255,255,255},
  [TC_USER_DOT]={255,196,140},[TC_USER_SHADOW]={236,196,166},[TC_SCROLL]={214,190,168},[TC_BOT_DOT]={236,214,190},
  [TC_HOME_BG]={181,207,213},[TC_HOME_INK]={48,68,80},
  [TC_TILE_OFF_PAPER]={214,214,210},[TC_TILE_OFF_INK]={112,112,108},[TC_TILE_OFF_PILL]={236,236,232},[TC_TILE_SPIN]={200,196,190},
  [TC_BATT_SHELL]={255,206,160},[TC_BATT_DEEP]={240,170,120},[TC_BATT_GROUND]={236,196,160},[TC_BATT_SHELL_OFF]={255,228,196},
  [TC_BATT_DEEP_OFF]={240,206,170},[TC_BATT_WINDOW]={255,255,255},[TC_BATT_GLOSS]={255,255,255},[TC_BATT_DARK]={74,38,16},
  [TC_POWER_CARD]={74,38,16},[TC_POWER_INK]={255,255,255},
  [TC_SN_PAGE]={250,244,232},[TC_SN_INK]={47,52,74},[TC_SN_MUTED]={132,126,146},[TC_SN_CARD]={241,233,219},
  [TC_SN_RULE]={222,212,196},[TC_SN_CORE]={255,252,246},[TC_SN_TINT]={255,254,250},
 },
 { /* Dark: black pages, dark grey cards and rows, light ink (contrast: tests/host/theme_test.c) */
  [TC_PAGE]={0,0,0},[TC_ROW]={40,38,36},[TC_DOT]={92,88,84},[TC_INK]={240,236,230},[TC_MUTED]={176,170,162},
  [TC_ERROR]={255,138,112},[TC_CARD]={52,50,48},[TC_KNOB]={250,248,244},[TC_OFF]={140,134,128},[TC_SHADOW]={0,0,0},
  [TC_GLASS]={30,28,26},[TC_PAPER]={0,0,0},[TC_USER]={176,70,14},[TC_USER_INK]={255,255,255},
  [TC_USER_DOT]={236,160,110},[TC_USER_SHADOW]={60,26,6},[TC_SCROLL]={112,106,100},[TC_BOT_DOT]={84,80,76},
  [TC_HOME_BG]={0,0,0},[TC_HOME_INK]={240,240,244},
  [TC_TILE_OFF_PAPER]={0,0,0},[TC_TILE_OFF_INK]={150,150,146},[TC_TILE_OFF_PILL]={36,36,36},[TC_TILE_SPIN]={78,78,76},
  [TC_BATT_SHELL]={72,66,60},[TC_BATT_DEEP]={52,48,44},[TC_BATT_GROUND]={30,28,26},[TC_BATT_SHELL_OFF]={56,54,52},
  [TC_BATT_DEEP_OFF]={40,38,36},[TC_BATT_WINDOW]={12,12,12},[TC_BATT_GLOSS]={130,124,118},[TC_BATT_DARK]={12,12,12},
  [TC_POWER_CARD]={44,42,40},[TC_POWER_INK]={240,236,230},
  [TC_SN_PAGE]={0,0,0},[TC_SN_INK]={228,232,240},[TC_SN_MUTED]={150,146,160},[TC_SN_CARD]={30,30,34},
  [TC_SN_RULE]={66,64,72},[TC_SN_CORE]={20,20,24},[TC_SN_TINT]={18,18,22},
 },
};
/* Per accent: FILL, TINT, ON, TEXT, FULL, NOTE. Light Orange = the original HH_ORANGE / HH_PEACH /
 * white / HH_ORANGE / battery-full orange / HH_PEACH. Dark: black labels on the accent fill, and the
 * accent lifted 35 % toward white wherever it is text on a dark surface. */
static const theme_rgb theme_accent_tbl[THEME_MODES][THEME_ACCENTS][THEME_ACCENT_ROLES]={
 {
  {{238,112,28},{255,196,140},{255,255,255},{238,112,28},{250,150,40},{255,196,140}},   /* Orange */
  {{64,132,240},{160,194,248},{255,255,255},{64,132,240},{112,163,244},{160,194,248}},  /* Blue */
  {{46,164,104},{150,210,180},{255,255,255},{46,164,104},{98,187,142},{150,210,180}},   /* Green */
  {{226,82,146},{240,168,200},{255,255,255},{226,82,146},{233,125,173},{240,168,200}},  /* Pink */
  {{138,96,228},{196,176,242},{255,255,255},{138,96,228},{167,136,235},{196,176,242}},  /* Purple */
 },
 {
  {{238,112,28},{107,50,13},{0,0,0},{244,162,107},{250,150,40},{176,170,162}},
  {{64,132,240},{29,59,108},{0,0,0},{131,175,245},{112,163,244},{176,170,162}},
  {{46,164,104},{21,74,47},{0,0,0},{119,196,157},{98,187,142},{176,170,162}},
  {{226,82,146},{102,37,66},{0,0,0},{236,143,184},{233,125,173},{176,170,162}},
  {{138,96,228},{62,43,103},{0,0,0},{179,152,237},{167,136,235},{176,170,162}},
 },
};
/* Your words on the Ask page follow the accent too: bubble, waiting dots (dim phase), drop shadow per
 * (mode, accent). Orange keeps the original values; the others are the accent fill (Light, white text at
 * least as readable as the original orange) or the accent darkened until white text is >= 5.3:1 (Dark). */
static const theme_rgb theme_user_tbl[THEME_MODES][THEME_ACCENTS][3]={
 {{{240,104,24},{255,196,140},{236,196,166}},{{64,132,240},{160,194,248},{184,198,223}},{{46,164,104},{150,210,180},{179,207,182}},
  {{226,82,146},{240,168,200},{233,183,195}},{{138,96,228},{196,176,242},{207,187,220}}},
 {{{176,70,14},{236,160,110},{60,26,6}},{{51,106,194},{131,175,245},{16,33,60}},{{34,122,77},{119,196,157},{11,41,26}},
  {{183,66,118},{236,143,184},{56,20,36}},{{125,87,207},{179,152,237},{34,24,57}}},
};
/* Sensor tiles: the status hue mixed into the tint base (Light 50 %, Dark 34 %), the label the hue mixed
 * toward the ink (Light 70 % toward the dark ink, Dark 45 % toward the light ink). */
static const int theme_sn_fill_pct[THEME_MODES]={50,34};
static const int theme_sn_label_pct[THEME_MODES]={70,45};
static inline int theme_sn_fill(int mode){return theme_sn_fill_pct[theme_mode_clamp(mode)];}
static inline int theme_sn_label(int mode){return theme_sn_label_pct[theme_mode_clamp(mode)];}

static inline theme_rgb theme_rgb_of(int mode,int accent,int role){
 mode=theme_mode_clamp(mode);accent=theme_accent_clamp(accent);
 if(role<0||role>=THEME_ROLES)role=TC_INK;
 if(role==TC_USER)return theme_user_tbl[mode][accent][0];
 if(role==TC_USER_DOT)return theme_user_tbl[mode][accent][1];
 if(role==TC_USER_SHADOW)return theme_user_tbl[mode][accent][2];
 return role<TC_NEUTRALS?theme_neutral[mode][role]:theme_accent_tbl[mode][accent][role-TC_NEUTRALS];
}
static inline uint16_t theme_color_of(int mode,int accent,int role){return theme_565(theme_rgb_of(mode,accent,role));}

/* The theme being drawn (set by home_render_page from home_ui; light + Orange until then). */
static struct {bool ready;uint8_t mode,accent;uint16_t c[THEME_ROLES];} theme_now;
static inline void theme_use(int mode,int accent){
 mode=theme_mode_clamp(mode);accent=theme_accent_clamp(accent);
 if(theme_now.ready&&theme_now.mode==mode&&theme_now.accent==accent)return;
 for(int r=0;r<THEME_ROLES;r++)theme_now.c[r]=theme_color_of(mode,accent,r);
 theme_now.mode=(uint8_t)mode;theme_now.accent=(uint8_t)accent;theme_now.ready=true;
}
static inline uint16_t theme_c(int role){
 if(!theme_now.ready)theme_use(THEME_LIGHT,0);
 return theme_now.c[role>=0&&role<THEME_ROLES?role:TC_INK];
}
static inline theme_rgb theme_rgb_now(int role){
 if(!theme_now.ready)theme_use(THEME_LIGHT,0);
 return theme_rgb_of(theme_now.mode,theme_now.accent,role);
}
static inline bool theme_dark(void){return theme_now.ready&&theme_now.mode==THEME_DARK;}
static inline int theme_mode_now(void){return theme_now.ready?theme_now.mode:THEME_LIGHT;}
static inline int theme_accent_now(void){return theme_now.ready?theme_now.accent:0;}

/* Light Home tiles (SPEC3 Contract S): each usable tile's paper is a different light shade of the accent,
 * in carousel order white mixed toward the accent at 16, 24, 32, 40 % (then again from 16 %). The tile
 * ink (TC_HOME_INK) stays >= 4.5:1 on every shade of every accent (tests/host/theme_test.c). Closed tiles keep
 * TC_TILE_OFF_PAPER; Dark tiles stay TC_HOME_BG (black). */
#define THEME_TILE_SHADES 4
static const uint8_t theme_tile_pct[THEME_TILE_SHADES]={16,24,32,40};
static inline uint8_t theme_tile_mix(uint8_t c,int pct){return (uint8_t)(255-((255-c)*pct+50)/100);}
static inline theme_rgb theme_tile_paper(int accent,int tile){
 theme_rgb a=theme_accent_color(accent);int pct=theme_tile_pct[(tile<0?0:tile)%THEME_TILE_SHADES];
 theme_rgb r={theme_tile_mix(a.r,pct),theme_tile_mix(a.g,pct),theme_tile_mix(a.b,pct)};return r;
}

/* Ask glass (bubbles, bot pill): Light halves what is underneath and adds half the tint (the original
 * blend); Dark keeps a quarter of it under 3/4 of the tint, so a light ink stays readable over the
 * brightest orb (tests/host/theme_test.c checks every dark orbs colour). `t` = theme_glass_tint(tint). */
static inline uint16_t theme_glass_tint(uint16_t tint,bool dark){
 if(!dark)return (uint16_t)((tint>>1)&0x7BEF);
 unsigned r=(tint>>11)*3u/4u,g=((tint>>5)&63u)*3u/4u,b=(tint&31u)*3u/4u;
 return (uint16_t)((r<<11)|(g<<5)|b);
}
static inline uint16_t theme_glass_px(uint16_t under,uint16_t t,bool dark){
 return dark?(uint16_t)(((under>>2)&0x39E7)+t):(uint16_t)(((under>>1)&0x7BEF)+t);
}
