#include <assert.h>
#include "panel_ui.h"
int main(void){
 static home_ui s;
 s.page=HELPER;helper_press(&s.helper,1);s.helper.want_record=false;
 assert(s.helper.state==HV_LISTENING);
 panel_ui_suppress(&s,100000);
 assert(s.helper.state==HV_IDLE && s.helper.want_cancel && !s.helper.holding);
 assert(!s.helper.want_send && !s.helper.want_stop);
 s=(home_ui){.page=HELPER};s.helper.state=HV_RUNNING;
 strcpy(s.helper.id,"0123456789abcdef0123456789abcdef");
 panel_ui_suppress(&s,100000);
 assert(s.helper.state==HV_RUNNING&&!s.helper.want_stop&&!s.helper.want_cancel);
 voice_command c={.status=VOICE_DONE};strcpy(c.id,s.helper.id);strcpy(c.text,"Preserved reply");
 helper_apply(&s.helper,&c);panel_ui_suppress(&s,200000);
 assert(s.helper.state==HV_DONE&&!strcmp(s.helper.reply,"Preserved reply"));
 home_sample(&s,300000,false,0,0);
 assert(s.helper.state==HV_DONE&&!strcmp(s.helper.reply,"Preserved reply"));
 return 0;
}
