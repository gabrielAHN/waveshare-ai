#include <assert.h>
#include <stdio.h>
#include "home_credentials.h"
static home_credentials disk;
static int fail,present,read_error;
static int get(void*c,home_credentials*v){(void)c;if(fail||read_error)return -1;if(!present)return 0;*v=disk;return 1;}
static bool put(void*c,const home_credentials*v){(void)c;if(fail)return false;disk=*v;present=1;return true;}
static bool erase(void*c){(void)c;if(fail)return false;memset(&disk,0,sizeof disk);present=0;return true;}
int main(void){
 home_credentials c={0},out={0};home_store store={get,put,erase,NULL};
 assert(!home_credentials_valid(&c));strcpy(c.ssid,"test-only-not-a-network");assert(home_credentials_valid(&c));
 strcpy(c.password,"short");assert(!home_credentials_valid(&c));
 memset(c.password,'x',63);c.password[63]=0;assert(home_credentials_valid(&c));
 memset(c.ssid,'s',32);c.ssid[32]=0;assert(home_credentials_valid(&c));
 assert(home_store_save(&store,&c));assert(home_store_load(&store,&out));assert(!memcmp(&c,&out,sizeof c));
 fail=1;assert(!home_store_save(&store,&c));assert(!home_store_forget(&store));fail=0;
 read_error=1;assert(!home_store_forget(&store));read_error=0;
 assert(home_store_forget(&store));assert(!home_store_load(&store,&out));
 memset(c.ssid,'z',sizeof c.ssid);assert(!home_credentials_valid(&c));
 memset(c.password,'z',sizeof c.password);assert(!home_credentials_valid(&c));
 puts("credential byte bounds, validation, persistence readback/failure/forget: PASS");
}
