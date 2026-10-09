#pragma once
#include "home_ui.h"
/* Input suppression shared by the real poller and host regressions. */
static inline void panel_ui_suppress(home_ui *s,int64_t now){
 /* Sleeping cancels only an unsubmitted recording; submitted work/replies survive. */
 helper_leave(&s->helper);
 home_settings_tick(s);
 if(s->page==HELPER){helper_tick(&s->helper,now);home_bots_sync(s,now);}
 direct_sample(&s->input,now,false,0,0);
 s->consumed=true;s->down=false;s->drag_offset=0;
 s->page_y=0;s->slide_kind=HOME_MOTION_NONE;s->home_slide_from=0;  /* slides end where they were going */
 s->helper.touching=s->helper.armed=s->helper.swiping=false;
}
