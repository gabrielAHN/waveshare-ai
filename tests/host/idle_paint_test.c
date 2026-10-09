#include <assert.h>
#include <stdio.h>
#include "home_render.h"
int main(void){home_ui a={0},b=a;assert(home_visual_equal(&a,&b));b.input.scene.time=100;b.input.samples=900;assert(home_visual_equal(&a,&b));b.drag_offset=1;assert(!home_visual_equal(&a,&b));b=a;b.tile=1;assert(!home_visual_equal(&a,&b));
 a.page=SPARKLES;b=a;b.input.scene.strength=1;b.input.scene.time=3;assert(!home_visual_equal(&a,&b)); /* blue fader now always animates */b.input.scene.theme=.5;assert(!home_visual_equal(&a,&b));
 a.page=SETTINGS;b=a;assert(home_visual_equal(&a,&b));
 #define CHANGED(field,value) b=a;b.field=value;assert(!home_visual_equal(&a,&b));
 CHANGED(busy,true);CHANGED(saved,true);CHANGED(connected,true);CHANGED(session_off,true);CHANGED(credentials.ssid[0],'x');CHANGED(status[0],'x');
 CHANGED(pair.state,PAIR_ENROLLED_UNPAIRED);CHANGED(pair.step,PV_SCANNING);CHANGED(pair.live_http,503);CHANGED(phone.showing,true);CHANGED(phone.confirm_signout,true);
 /* The password is not drawn, so it never forces a repaint. */
 b=a;b.credentials.password[0]='x';assert(home_visual_equal(&a,&b));
 b=a;b.page=HOME;assert(!home_visual_equal(&a,&b));puts("Event-driven paint invalidation covers visible navigation, styles and all Settings state: PASS");}
