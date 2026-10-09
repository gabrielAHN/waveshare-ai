#pragma once
/* Waveshare AI device-side pairing core (pure C, host-tested in tests/host/pair_state_test.c).
 *
 * - Board identity: 32 random bytes generated ON THE DEVICE on first boot (never compiled in
 *   or USB-provisioned), stored in NVS, sent only inside pinned TLS as the bearer token.
 * - Bridge trust: SHA-256 of the bridge's leaf certificate (DER), learned from the mDNS TXT
 *   record (or captured on first use for manual entry) and pinned in NVS.
 * - Enrollment: POST /v1/enroll {WEN1, token, device name}; the device and the host both show
 *   the same 6-digit comparison code derived from (pinned fingerprint, SHA-256(token)), so a
 *   man-in-the-middle with a different certificate produces a different code.
 * States (persisted): no_bridge -> enrolling -> enrolled_unpaired -> paired (Authelia user
 * pairing, next milestone). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sha256_tiny.h"
#include "provision.h"

#define PAIR_URL_MAX 96
#define PAIR_NAME_MAX 32
#define PAIR_LIST_MAX 8
#define PAIR_DEFAULT_PORT 8098
#define PAIR_RECORD_MAGIC 0x31524150u /* "PAR1" */
#define PAIR_FLAG_TOFU 2u              /* pin captured on first use (manual entry) */
typedef enum {PAIR_NO_BRIDGE=0,PAIR_ENROLLING=1,PAIR_ENROLLED_UNPAIRED=2,PAIR_PAIRED=3} pair_phase;
typedef struct {
 uint32_t magic;uint8_t version,state,flags,reserved;
 char base[PAIR_URL_MAX];      /* https://host[:port], no path */
 char name[PAIR_NAME_MAX+1];   /* bridge display name from mDNS / user */
 unsigned char fp[32];         /* pinned SHA-256(leaf DER) */
} pair_record;

/* ---------- identity ---------- */
typedef enum {PAIR_ID_ERROR=-1,PAIR_ID_LOADED=0,PAIR_ID_GENERATED=1} pair_id_result;
typedef struct {
 int(*get)(void*,unsigned char out[32]);   /* 1 found, 0 absent, -1 I/O error */
 bool(*put)(void*,const unsigned char in[32]);
 void*ctx;
} pair_id_store;
typedef void(*pair_rng)(void*out,size_t n);
static inline bool pair_identity_valid(const unsigned char t[32]){
 /* Rejects all-equal bytes (e.g. an RNG that returned zeros before Wi-Fi/RF entropy). */
 for(int i=1;i<32;i++)if(t[i]!=t[0])return true;
 return false;
}
static inline pair_id_result pair_identity_ensure(const pair_id_store*s,pair_rng rng,unsigned char out[32]){
 unsigned char check[32];int got=s->get(s->ctx,out);
 if(got==1){if(pair_identity_valid(out))return PAIR_ID_LOADED;provision_wipe(out,32);return PAIR_ID_ERROR;}
 if(got!=0){provision_wipe(out,32);return PAIR_ID_ERROR;} /* never overwrite on a read error */
 bool drawn=false;
 for(int attempt=0;attempt<3&&!drawn;attempt++){rng(out,32);drawn=pair_identity_valid(out);}
 if(!drawn||!s->put(s->ctx,out)||s->get(s->ctx,check)!=1||memcmp(check,out,32)){provision_wipe(out,32);provision_wipe(check,32);return PAIR_ID_ERROR;}
 provision_wipe(check,32);return PAIR_ID_GENERATED;
}
/* Replace the identity (the old token may have reached an unconfirmed peer). */
static inline bool pair_identity_rotate(const pair_id_store*s,pair_rng rng,unsigned char out[32]){
 unsigned char check[32];bool drawn=false;
 for(int attempt=0;attempt<3&&!drawn;attempt++){rng(out,32);drawn=pair_identity_valid(out);}
 bool ok=drawn&&s->put(s->ctx,out)&&s->get(s->ctx,check)==1&&!memcmp(check,out,32);
 provision_wipe(check,32);if(!ok)provision_wipe(out,32);return ok;
}
static inline void pair_hex(const unsigned char in[32],char out[65]){
 static const char d[]="0123456789abcdef";
 for(int i=0;i<32;i++){out[i*2]=d[in[i]>>4];out[i*2+1]=d[in[i]&15];}
 out[64]=0;
}
static inline int pair_nibble(char c){
 if(c>='0'&&c<='9')return c-'0';
 if(c>='a'&&c<='f')return c-'a'+10;
 if(c>='A'&&c<='F')return c-'A'+10;
 return -1;
}
static inline bool pair_hex32(const char*text,unsigned char out[32]){
 if(!text||strlen(text)!=64)return false;
 unsigned char tmp[32];
 for(int i=0;i<32;i++){int hi=pair_nibble(text[i*2]),lo=pair_nibble(text[i*2+1]);if(hi<0||lo<0)return false;tmp[i]=(unsigned char)(hi<<4|lo);}
 memcpy(out,tmp,32);return true;
}

/* Public short id: hex of SHA-256(token)[0..3]; the bridge registry shows the same prefix. */
static inline void pair_short_id(const unsigned char token[32],char out[9]){
 unsigned char d[32];sha256_tiny(token,32,d);snprintf(out,9,"%02x%02x%02x%02x",d[0],d[1],d[2],d[3]);
}
/* ---------- comparison code ---------- */
static inline uint32_t pair_code(const unsigned char fp[32],const unsigned char token[32]){
 static const char label[]="waveshare-ai-enroll-v1"; /* hashed WITH its NUL terminator */
 unsigned char th[32],d[32];sha256_tiny(token,32,th);
 sha256_ctx c;sha256_init(&c);sha256_update(&c,label,sizeof label);sha256_update(&c,fp,32);sha256_update(&c,th,32);sha256_final(&c,d);
 uint32_t v=(uint32_t)d[0]<<24|(uint32_t)d[1]<<16|(uint32_t)d[2]<<8|d[3];
 provision_wipe(th,32);return v%1000000u;
}
static inline void pair_code_text(uint32_t code,char out[8]){snprintf(out,8,"%03u %03u",(unsigned)(code/1000%1000),(unsigned)(code%1000));}

/* ---------- certificate pin (mbedTLS verify-callback core) ---------- */
#define PAIR_BADCERT_NOT_TRUSTED 0x08u /* == MBEDTLS_X509_BADCERT_NOT_TRUSTED */
typedef struct {unsigned char pin[32],observed[32];bool capture,matched,seen;} pair_pin_ctx;
static inline void pair_pin_require(pair_pin_ctx*c,const unsigned char pin[32]){memset(c,0,sizeof *c);memcpy(c->pin,pin,32);}
static inline void pair_pin_capture(pair_pin_ctx*c){memset(c,0,sizeof *c);c->capture=true;}
static inline bool pair_pin_match(const unsigned char*der,size_t n,const unsigned char pin[32]){
 unsigned char d[32];sha256_tiny(der,n,d);
 unsigned diff=0;bool empty=true;for(int i=0;i<32;i++){diff|=d[i]^pin[i];if(pin[i])empty=false;}
 return !empty&&diff==0;
}
/* Called for every certificate in the peer chain. Only the leaf (depth 0) can establish trust:
 * CA/intermediate flags are cleared because the pin is the sole trust anchor. */
static inline int pair_verify_cert(pair_pin_ctx*c,const unsigned char*der,size_t n,int depth,uint32_t*flags){
 if(!c||!flags||!der)return -1;
 if(depth>0){*flags=0;return 0;}
 unsigned char d[32];sha256_tiny(der,n,d);
 if(c->capture&&!c->seen){memcpy(c->observed,d,32);memcpy(c->pin,d,32);c->seen=true;} /* first use: pin what we saw */
 c->seen=true;
 if(pair_pin_match(der,n,c->pin)){c->matched=true;*flags=0;}
 else{c->matched=false;*flags|=PAIR_BADCERT_NOT_TRUSTED;}
 return 0;
}
static inline void pair_fp_short(const unsigned char fp[32],char out[24]){
 snprintf(out,24,"%02x%02x%02x%02x %02x%02x%02x%02x",fp[0],fp[1],fp[2],fp[3],fp[4],fp[5],fp[6],fp[7]);
}

/* ---------- bridge addresses ---------- */
static inline bool pair_port_text(const char*p,unsigned*port){
 if(!*p)return false;
 unsigned v=0;
 for(;*p;p++){if(*p<'0'||*p>'9'||v>65535)return false;v=v*10+(unsigned)(*p-'0');}
 if(v<1||v>65535)return false;
 *port=v;return true;
}
static inline bool pair_base_valid(const char*b){
 if(!b||strncmp(b,"https://",8))return false;
 size_t n=strlen(b);if(n>=PAIR_URL_MAX||n<=8)return false;
 const char*host=b+8,*colon=strchr(host,':');size_t hl=colon?(size_t)(colon-host):strlen(host);
 if(!hl||hl>64)return false;
 for(size_t i=0;i<hl;i++){char c=host[i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'))return false;}
 unsigned port;return !colon||pair_port_text(colon+1,&port);
}
static inline bool pair_url(char*out,size_t cap,const char*base,const char*path){
 int n=snprintf(out,cap,"%s%s",base,path);return n>0&&(size_t)n<cap;
}
/* Settings "Enter address": host[:port] or https://host[:port]; default port 8098. */
static inline bool pair_manual_base(const char*in,char out[PAIR_URL_MAX]){
 if(!in||!*in)return false;
 char tmp[PAIR_URL_MAX+16];const char*host=strncmp(in,"https://",8)?in:in+8;
 int n=strchr(host,':')?snprintf(tmp,sizeof tmp,"https://%s",host):snprintf(tmp,sizeof tmp,"https://%s:%d",host,PAIR_DEFAULT_PORT);
 if(n<=0||(size_t)n>=sizeof tmp||!pair_base_valid(tmp))return false;
 memcpy(out,tmp,(size_t)n+1);return true;
}
static inline void pair_clean_name(const char*in,char out[PAIR_NAME_MAX+1]){
 size_t j=0;for(;in&&*in&&j<PAIR_NAME_MAX;in++)if(*in>=32&&*in<127)out[j++]=*in;
 out[j]=0;
}

/* ---------- discovered bridges (mDNS _waveshare-ai._tcp) ---------- */
typedef struct {char name[PAIR_NAME_MAX+1],base[PAIR_URL_MAX];unsigned char fp[32];} pair_bridge;
typedef struct {pair_bridge items[PAIR_LIST_MAX];int count;} pair_list;
static inline bool pair_ipv4_valid(const char*ip){
 int parts=0;const char*p=ip;
 while(parts<4){unsigned v=0;int digits=0;while(*p>='0'&&*p<='9'){v=v*10+(unsigned)(*p++-'0');if(++digits>3||v>255)return false;}
  if(!digits)return false;
  parts++;
  if(parts<4){if(*p!='.')return false;p++;}}
 return *p==0;
}
/* 1 added, 0 updated (same fingerprint), -1 rejected (no/invalid pin, bad address, list full). */
static inline bool pair_fp_set(const unsigned char fp[32]);
static inline int pair_list_add_base(pair_list*l,const char*name,const char*base,const unsigned char fp[32]){
 if(!base||!pair_base_valid(base)||!fp||!pair_fp_set(fp))return -1;
 pair_bridge b;memset(&b,0,sizeof b);snprintf(b.base,sizeof b.base,"%s",base);memcpy(b.fp,fp,32);
 pair_clean_name(name,b.name);if(!b.name[0])snprintf(b.name,sizeof b.name,"%s",base+8);
 for(int i=0;i<l->count;i++)if(!memcmp(l->items[i].fp,b.fp,32)){l->items[i]=b;return 0;}
 if(l->count>=PAIR_LIST_MAX)return -1;
 l->items[l->count++]=b;return 1;
}
static inline int pair_list_add(pair_list*l,const char*name,const char*ipv4,unsigned port,const char*fp_hex){
 unsigned char fp[32];char base[PAIR_URL_MAX];
 if(!ipv4||!pair_ipv4_valid(ipv4)||port<1||port>65535||!pair_hex32(fp_hex,fp))return -1;
 snprintf(base,sizeof base,"https://%s:%u",ipv4,port);
 return pair_list_add_base(l,name&&*name?name:ipv4,base,fp);
}

/* ---------- persisted record ---------- */
static inline void pair_record_init(pair_record*r){memset(r,0,sizeof *r);r->magic=PAIR_RECORD_MAGIC;r->version=1;r->state=PAIR_NO_BRIDGE;}
static inline bool pair_fp_set(const unsigned char fp[32]){for(int i=0;i<32;i++)if(fp[i])return true;return false;}
static inline bool pair_record_valid(const pair_record*r){
 if(!r||r->magic!=PAIR_RECORD_MAGIC||r->state>PAIR_PAIRED||(r->flags&~PAIR_FLAG_TOFU))return false;
 if(!memchr(r->base,0,sizeof r->base)||!memchr(r->name,0,sizeof r->name))return false;
 if(r->base[0]&&!pair_base_valid(r->base))return false;
 if(r->state>=PAIR_ENROLLED_UNPAIRED&&(!r->base[0]||!pair_fp_set(r->fp)))return false;
 return true;
}
/* Enrolled (or paired) with a pinned bridge: live feed and voice may use it. */
static inline bool pair_record_live(const pair_record*r){return pair_record_valid(r)&&r->state>=PAIR_ENROLLED_UNPAIRED;}
/* ---------- enrollment wire ---------- */
#define PAIR_ENROLL_BODY (4+32+PAIR_NAME_MAX+1)
static inline void pair_enroll_body(const unsigned char token[32],const char*device_name,unsigned char out[PAIR_ENROLL_BODY]){
 memset(out,0,PAIR_ENROLL_BODY);memcpy(out,"WEN1",4);memcpy(out+4,token,32);
 pair_clean_name(device_name,(char*)out+36);
}
typedef enum {PAIR_ENROLL_UNREACHABLE,PAIR_ENROLL_PENDING,PAIR_ENROLL_ACCEPTED,PAIR_ENROLL_DENIED,PAIR_ENROLL_EXPIRED,PAIR_ENROLL_CLOSED,PAIR_ENROLL_RETRY} pair_enroll_result;
static inline pair_enroll_result pair_enroll_status(int http){
 switch(http){
  case 200:return PAIR_ENROLL_ACCEPTED;
  case 202:return PAIR_ENROLL_PENDING;
  case 403:return PAIR_ENROLL_CLOSED;
  case 409:return PAIR_ENROLL_DENIED;
  case 410:return PAIR_ENROLL_EXPIRED;
  case 429:return PAIR_ENROLL_RETRY;
  default:return PAIR_ENROLL_UNREACHABLE;
 }
}
