#include <assert.h>
#include <stdio.h>
#include "home_ui.h"
int main(void){
 home_ui s={0};s.page=HELPER;s.connected=true;
 s.bots.data=(bots_data){.valid=true,.count=3,.received_us=1};
 for(int i=0;i<3;i++)s.bots.data.b[i].reason=BR_PHONE;
 s.phone.st=(phone_status){.valid=true,.state=PH_NONE,.flags=PHONE_FLAG_REQUIRED};
 home_bots_sync(&s,2);assert(s.helper.block==BR_PHONE);
 phone_status auth={.valid=true,.state=PH_AUTHORIZED,.flags=PHONE_FLAG_REQUIRED};
 home_phone_apply(&s,&auth,true);
 for(int i=0;i<3;i++){s.helper.bot=i;home_bots_sync(&s,3);assert(s.helper.block==BR_NONE);}
 assert(s.bots.refresh);
 s.bots.data.b[2].reason=BR_EXHAUSTED;home_bots_sync(&s,3);assert(s.helper.block==BR_EXHAUSTED);
 s.phone.st.state=PH_NONE;home_bots_sync(&s,3);assert(s.helper.block==BR_PHONE);
 puts("phone authorization supersedes old phone gate; no-quota and sign-out remain blocked: PASS");
}
