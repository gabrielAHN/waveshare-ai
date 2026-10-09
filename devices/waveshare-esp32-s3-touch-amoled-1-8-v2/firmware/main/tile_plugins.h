#pragma once
/* Home tiles as plugins: one row per tile, in swipe order; Settings is always last. Every tile but
 * Settings belongs to a provider (plugins.h): Sparkles and Ask to Hermes, Sensor to Home Assistant.
 *
 * Add a tile:
 *   1. a build switch: main/Kconfig.projbuild (a CONFIG_WAVESHARE_AI_<PROVIDER>_<NAME> option under its
 *      provider) -> main/CMakeLists.txt (WAVESHARE_AI_PLUGIN_<NAME>=0|1, plus any sources only it needs)
 *      -> plugins.h (WAVESHARE_AI_PLUGIN_<NAME>, default = its provider);
 *   2. a page id (home_page below; ids are the USB WLV1 numbers, append only) + home_ui.h home_page_enabled();
 *   3. its tile icon (tile_icon below, drawn by home_render.h home_render()) and its page renderer;
 *   4. one row here inside #if WAVESHARE_AI_PLUGIN_<NAME>.
 * Turn a tile off: build without it (`idf.py menuconfig` -> Waveshare AI providers). The tile, its page and
 * its network worker are then not compiled.
 *
 * Every tile needs Wi-Fi, which comes with the flash (.env on the host -> tools/flash.sh -> USB).
 * `need` says how the tile depends on its provider (the bridge next to Hermes, the phone sign-in):
 *   TILE_STANDALONE       never gated, no link state                       (Settings)
 *   TILE_HERMES_OPTIONAL  works without Hermes; shows the Wi-Fi / Hermes   (Sparkles: the water runs
 *                         link state on its tile and page, always opens     offline, glows need the host)
 *   TILE_HERMES_REQUIRED  grey and closed until Wi-Fi, the host and its     (Ask, Sensor)
 *                         provider's phone sign-in say yes; `gate` = which sign-in
 * home_ui.h home_tile_status() turns this into ON / LOADING / OFF plus the tile's status line. */
#include <stdint.h>
#include "plugins.h"
/* Page ids are also the USB WLV1 page numbers: keep existing values stable, append new pages. */
typedef enum {HOME=0,SPARKLES=1,SETTINGS=2,HELPER=3,SENSORS=6} home_page;
typedef enum {TILE_STANDALONE,TILE_HERMES_OPTIONAL,TILE_HERMES_REQUIRED} tile_need;
/* Whose tile it is: its sign-in view, its Settings tab and its "Not set up" (home_ui.h). */
typedef enum {TILE_PROVIDER_NONE,TILE_PROVIDER_HERMES,TILE_PROVIDER_HOME_ASSISTANT} tile_provider;
typedef enum {
 TILE_GATE_NONE,
 TILE_GATE_SIGNIN,          /* the Hermes provider's sign-in (home_ui.phone, phone_qr.h phone_gate) */
 TILE_GATE_HOME_ASSISTANT,  /* the Home Assistant provider's own sign-in (home_ui.phone_ha: signed in) */
} tile_gate;
typedef enum {TILE_ICON_SPARKLES,TILE_ICON_BOTS,TILE_ICON_SENSORS,TILE_ICON_SETTINGS} tile_icon;  /* BOTS: the Ask bots (bot_art.h) */
typedef struct {
 home_page page;        /* opened by a tap */
 const char*name;       /* big label, <= 10 characters */
 tile_provider provider;
 tile_need need;
 tile_gate gate;
 tile_icon icon;        /* (its paper while usable: Light = a shade of the accent by carousel position,
                         * theme.h theme_tile_paper; Dark = black; closed = grey) */
} tile_plugin;
static const tile_plugin home_tiles[]={
#if WAVESHARE_AI_PLUGIN_SPARKLES
 {SPARKLES,"Sparkles",TILE_PROVIDER_HERMES,TILE_HERMES_OPTIONAL,TILE_GATE_NONE,TILE_ICON_SPARKLES},
#endif
#if WAVESHARE_AI_PLUGIN_AI
 {HELPER,"Ask",TILE_PROVIDER_HERMES,TILE_HERMES_REQUIRED,TILE_GATE_SIGNIN,TILE_ICON_BOTS},
#endif
#if WAVESHARE_AI_PLUGIN_HOME_ASSISTANT
 {SENSORS,"Sensor",TILE_PROVIDER_HOME_ASSISTANT,TILE_HERMES_REQUIRED,TILE_GATE_HOME_ASSISTANT,TILE_ICON_SENSORS},
#endif
 {SETTINGS,"Settings",TILE_PROVIDER_NONE,TILE_STANDALONE,TILE_GATE_NONE,TILE_ICON_SETTINGS},
};
#define HOME_TILES ((int)(sizeof home_tiles/sizeof home_tiles[0]))
#define HOME_TILE_SETTINGS (HOME_TILES-1)
/* Tile index of a page on Home, or -1 when its plugin is not built. */
static inline int home_tile_index(home_page page){
 for(int i=0;i<HOME_TILES;i++)if(home_tiles[i].page==page)return i;
 return -1;
}
