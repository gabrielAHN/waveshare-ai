#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
typedef struct {char ssid[33],password[64];} home_credentials;
static inline bool home_credentials_valid(const home_credentials*c){
 if(!c||!memchr(c->ssid,0,sizeof c->ssid)||!memchr(c->password,0,sizeof c->password))return false;
 size_t n=strlen(c->ssid),p=strlen(c->password);
 return n>0&&n<=32&&(p==0||(p>=8&&p<=63));
}
typedef struct {
 /* get: 1 found, 0 absent, -1 I/O or format error. */
 int(*get)(void*,home_credentials*);
 bool(*put)(void*,const home_credentials*);
 bool(*erase)(void*);
 void*ctx;
} home_store;
static inline bool home_store_load(const home_store*s,home_credentials*c){
 home_credentials temp={0};bool ok=s&&s->get&&s->get(s->ctx,&temp)==1&&home_credentials_valid(&temp);
 if(c){memset(c,0,sizeof *c);if(ok)*c=temp;}else ok=false;
 memset(&temp,0,sizeof temp);return ok;
}
static inline bool home_store_save(const home_store*s,const home_credentials*c){
 if(!s||!s->put||!home_credentials_valid(c)||!s->put(s->ctx,c))return false;
 home_credentials check={0};bool ok=home_store_load(s,&check)&&memcmp(c,&check,sizeof check)==0;
 memset(&check,0,sizeof check);return ok;
}
static inline bool home_store_forget(const home_store*s){
 if(!s||!s->erase||!s->erase(s->ctx))return false;
 home_credentials check={0};bool absent=s->get&&s->get(s->ctx,&check)==0;
 memset(&check,0,sizeof check);return absent;
}
