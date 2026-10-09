#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pair_state.h"
/* ---- in-memory identity store + deterministic RNG ---- */
typedef struct {unsigned char blob[32];int present,fail_put,fail_get,corrupt_readback,puts;} mem_store;
static int mem_get(void*ctx,unsigned char out[32]){mem_store*m=ctx;if(m->fail_get)return -1;if(!m->present)return 0;memcpy(out,m->blob,32);if(m->corrupt_readback)out[0]^=1;return 1;}
static bool mem_put(void*ctx,const unsigned char in[32]){mem_store*m=ctx;if(m->fail_put)return false;memcpy(m->blob,in,32);m->present=1;m->puts++;return true;}
static unsigned rng_calls,rng_weak;
static void rng(void*out,size_t n){unsigned char*p=out;rng_calls++;for(size_t i=0;i<n;i++)p[i]=rng_weak?0:(unsigned char)(i*7+rng_calls*13+1);if(rng_weak)rng_weak--;}
static void test_identity(void){
 mem_store m={0};pair_id_store s={mem_get,mem_put,&m};unsigned char a[32],b[32];
 rng_calls=0;assert(pair_identity_ensure(&s,rng,a)==PAIR_ID_GENERATED);assert(m.puts==1&&rng_calls==1);
 assert(pair_identity_valid(a)&&!memcmp(a,m.blob,32));
 /* second boot loads, never regenerates or rewrites */
 assert(pair_identity_ensure(&s,rng,b)==PAIR_ID_LOADED);assert(!memcmp(a,b,32)&&m.puts==1&&rng_calls==1);
 /* weak RNG output (all zero) is rejected and redrawn */
 mem_store w={0};pair_id_store ws={mem_get,mem_put,&w};rng_calls=0;rng_weak=2;
 assert(pair_identity_ensure(&ws,rng,a)==PAIR_ID_GENERATED&&rng_calls==3&&pair_identity_valid(a));
 /* RNG that never yields a valid token fails closed, stores nothing */
 mem_store z={0};pair_id_store zs={mem_get,mem_put,&z};rng_weak=100;memset(a,0x55,32);
 assert(pair_identity_ensure(&zs,rng,a)==PAIR_ID_ERROR&&!z.present);for(int i=0;i<32;i++)assert(a[i]==0);rng_weak=0;
 /* I/O error on read never overwrites a possibly-present identity */
 mem_store e={0};e.fail_get=1;pair_id_store es={mem_get,mem_put,&e};
 assert(pair_identity_ensure(&es,rng,a)==PAIR_ID_ERROR&&e.puts==0);
 /* failed put / failed readback -> error */
 mem_store f={0};f.fail_put=1;pair_id_store fs={mem_get,mem_put,&f};assert(pair_identity_ensure(&fs,rng,a)==PAIR_ID_ERROR);
 mem_store r={0};r.corrupt_readback=1;pair_id_store rs={mem_get,mem_put,&r};assert(pair_identity_ensure(&rs,rng,a)==PAIR_ID_ERROR);
 /* a stored weak identity is treated as corrupt, not used */
 mem_store bad={0};bad.present=1;pair_id_store bs={mem_get,mem_put,&bad};assert(pair_identity_ensure(&bs,rng,a)==PAIR_ID_ERROR);
 unsigned char same[32];memset(same,0xab,32);assert(!pair_identity_valid(same));
 /* rotation (after a failed/cancelled enrollment that transmitted the token, or Forget) */
 mem_store rot={0};pair_id_store rs2={mem_get,mem_put,&rot};unsigned char first[32],second[32];rng_calls=0;
 assert(pair_identity_ensure(&rs2,rng,first)==PAIR_ID_GENERATED);assert(pair_identity_rotate(&rs2,rng,second));
 assert(memcmp(first,second,32)&&!memcmp(rot.blob,second,32)&&rot.puts==2);
 mem_store rf={0};rf.fail_put=1;pair_id_store rfs={mem_get,mem_put,&rf};memset(second,0x77,32);assert(!pair_identity_rotate(&rfs,rng,second));for(int i=0;i<32;i++)assert(second[i]==0);
 /* short public id = first 4 bytes of SHA-256(token), same as the bridge registry shows */
 unsigned char tk[32];for(int i=0;i<32;i++)tk[i]=(unsigned char)(32+i);char sid[9];pair_short_id(tk,sid);assert(strlen(sid)==8);
 /* hex bearer is 64 lowercase chars */
 char hex[65];unsigned char t[32];for(int i=0;i<32;i++)t[i]=(unsigned char)(i*9);pair_hex(t,hex);assert(strlen(hex)==64&&!strncmp(hex,"0009121b",8));
}
static void test_code(void){
 unsigned char fp[32],tok[32];for(int i=0;i<32;i++){fp[i]=(unsigned char)i;tok[i]=(unsigned char)(32+i);}
 assert(pair_code(fp,tok)==546969); /* == bridge/waveshare_bridge/enroll.py comparison_code */
 memset(fp,0xff,32);memset(tok,0x11,32);assert(pair_code(fp,tok)==616758);
 fp[0]=0xfe;assert(pair_code(fp,tok)==162232); /* a different certificate changes the code */
 char text[8];pair_code_text(7,text);assert(!strcmp(text,"000 007"));pair_code_text(546969,text);assert(!strcmp(text,"546 969"));
}
static void test_hex32(void){
 unsigned char out[32];
 assert(pair_hex32("479ad1dc62451c82f1cb229bf5bf629f1e2eb88b79d0307b6dcf1898a0cce5f7",out)&&out[0]==0x47&&out[31]==0xf7);
 assert(pair_hex32("479AD1DC62451C82F1CB229BF5BF629F1E2EB88B79D0307B6DCF1898A0CCE5F7",out)&&out[1]==0x9a);
 assert(!pair_hex32("479ad1",out));assert(!pair_hex32(NULL,out));
 assert(!pair_hex32("479ad1dc62451c82f1cb229bf5bf629f1e2eb88b79d0307b6dcf1898a0cce5f7aa",out));
 assert(!pair_hex32("x79ad1dc62451c82f1cb229bf5bf629f1e2eb88b79d0307b6dcf1898a0cce5f7",out));
}
static void test_pin(void){
 const unsigned char der[]="leaf-der-bytes";size_t n=sizeof der-1;unsigned char pin[32];
 assert(pair_hex32("479ad1dc62451c82f1cb229bf5bf629f1e2eb88b79d0307b6dcf1898a0cce5f7",pin));
 assert(pair_pin_match(der,n,pin));
 pair_pin_ctx c;pair_pin_require(&c,pin);uint32_t flags=0x8|0x4; /* NOT_TRUSTED|CN_MISMATCH */
 assert(pair_verify_cert(&c,der,n,0,&flags)==0&&flags==0&&c.matched);
 /* intermediate/CA depths are not trusted on their own: flags cleared, leaf decides */
 flags=0x8;pair_pin_require(&c,pin);assert(pair_verify_cert(&c,(const unsigned char*)"ca",2,1,&flags)==0&&flags==0&&!c.matched);
 /* wrong leaf keeps (or adds) NOT_TRUSTED and never reports matched */
 flags=0;pair_pin_require(&c,pin);assert(pair_verify_cert(&c,(const unsigned char*)"other",5,0,&flags)==0&&flags!=0&&!c.matched);
 unsigned char zero[32]={0};pair_pin_require(&c,zero);flags=0;assert(pair_verify_cert(&c,der,n,0,&flags)==0&&flags!=0); /* empty pin never matches */
 /* TOFU capture (manual entry): accepts and records the observed fingerprint */
 pair_pin_capture(&c);flags=0x8;assert(pair_verify_cert(&c,der,n,0,&flags)==0&&flags==0&&c.seen&&!memcmp(c.observed,pin,32));
 /* after the first capture the observed certificate is pinned: a reconnect to a different cert fails */
 flags=0;assert(pair_verify_cert(&c,(const unsigned char*)"evil",4,0,&flags)==0&&flags!=0&&!c.matched&&!memcmp(c.observed,pin,32));
 flags=0x8;assert(pair_verify_cert(&c,der,n,0,&flags)==0&&flags==0&&c.matched);
 flags=0;assert(pair_verify_cert(&c,der,n,0,NULL)!=0); /* defensive */
 char s8[24];pair_fp_short(pin,s8);assert(!strcmp(s8,"479ad1dc 62451c82"));
}
static void test_base(void){
 assert(pair_base_valid("https://192.0.2.10:8098"));assert(pair_base_valid("https://bridge.local:443"));assert(pair_base_valid("https://bridge.local"));
 const char*bad[]={"http://192.0.2.10:8098","https://","https://a b","https://u@h:1","https://h:0","https://h:65536","https://h:80/path","https://h:80?x","https://h:x",NULL};
 for(int i=0;bad[i];i++)assert(!pair_base_valid(bad[i]));
 char url[PAIR_URL_MAX+16];assert(pair_url(url,sizeof url,"https://192.0.2.10:8098","/v1/live")&&!strcmp(url,"https://192.0.2.10:8098/v1/live"));
 assert(!pair_url(url,10,"https://192.0.2.10:8098","/v1/live"));
 char base[PAIR_URL_MAX];assert(pair_manual_base("192.0.2.10:8098",base)&&!strcmp(base,"https://192.0.2.10:8098"));
 assert(pair_manual_base("bridge.local",base)&&!strcmp(base,"https://bridge.local:8098"));
 assert(pair_manual_base("https://h:9",base)&&!strcmp(base,"https://h:9"));assert(!pair_manual_base("",base));assert(!pair_manual_base("h:99999",base));
}
static void test_list(void){
 pair_list l={0};const char*fp1="479ad1dc62451c82f1cb229bf5bf629f1e2eb88b79d0307b6dcf1898a0cce5f7";
 const char*fp2="0000000000000000000000000000000000000000000000000000000000000001";
 assert(pair_list_add(&l,"Studio host","192.0.2.10",8098,fp1)==1&&l.count==1);
 assert(!strcmp(l.items[0].base,"https://192.0.2.10:8098")&&!strcmp(l.items[0].name,"Studio host")&&l.items[0].fp[0]==0x47);
 assert(pair_list_add(&l,"Studio host","192.0.2.11",8098,fp1)==0&&l.count==1&&!strcmp(l.items[0].base,"https://192.0.2.11:8098")); /* same fp: update */
 assert(pair_list_add(&l,"No pin","192.0.2.12",8098,NULL)==-1&&l.count==1);          /* TXT fp required */
 assert(pair_list_add(&l,"Bad ip","192.0.2",8098,fp2)==-1);assert(pair_list_add(&l,"Bad port","192.0.2.1",0,fp2)==-1);
 assert(pair_list_add(&l,"x\x01y\x7f and a very long name that keeps going past 32","192.0.2.13",1,fp2)==1);
 assert(strlen(l.items[1].name)<=32&&!strchr(l.items[1].name,1)&&!strchr(l.items[1].name,0x7f));
 for(int i=0;i<10;i++){char f[65];snprintf(f,sizeof f,"%064x",i+2);pair_list_add(&l,"n","192.0.2.20",8098,f);}
 assert(l.count==PAIR_LIST_MAX);
 pair_list m={0};unsigned char fp[32];pair_hex32(fp1,fp);
 assert(pair_list_add_base(&m,"host","https://bridge.local:9000",fp)==1&&!strcmp(m.items[0].base,"https://bridge.local:9000"));
 assert(pair_list_add_base(&m,"host","http://bridge.local:9000",fp)==-1);unsigned char z[32]={0};assert(pair_list_add_base(&m,"host","https://h:1",z)==-1);
}
static void test_record(void){
 pair_record r;pair_record_init(&r);assert(pair_record_valid(&r)&&r.state==PAIR_NO_BRIDGE);
 pair_record unsupported=r;unsupported.flags=1;assert(!pair_record_valid(&unsupported));
 /* enrolled record */
 pair_record_init(&r);r.state=PAIR_ENROLLED_UNPAIRED;strcpy(r.base,"https://192.0.2.10:8098");strcpy(r.name,"host");memset(r.fp,1,32);
 assert(pair_record_valid(&r)&&pair_record_live(&r));
 r.state=PAIR_PAIRED;assert(pair_record_live(&r));
 r.state=9;assert(!pair_record_valid(&r));r.state=PAIR_ENROLLED_UNPAIRED;
 memset(r.fp,0,32);assert(!pair_record_valid(&r)); /* enrolled requires a pin */
 memset(r.fp,1,32);r.magic=0;assert(!pair_record_valid(&r));
 r.magic=PAIR_RECORD_MAGIC;memset(r.base,'x',sizeof r.base);assert(!pair_record_valid(&r));
}
static void test_enroll_wire(void){
 unsigned char tok[32];memset(tok,0x42,32);unsigned char body[PAIR_ENROLL_BODY];
 pair_enroll_body(tok,"Waveshare AI \x01 device",body);assert(!memcmp(body,"WEN1",4)&&!memcmp(body+4,tok,32));
 assert(!strcmp((char*)body+36,"Waveshare AI  device")||!strcmp((char*)body+36,"Waveshare AI ? device"));
 assert(body[PAIR_ENROLL_BODY-1]==0);
 assert(pair_enroll_status(202)==PAIR_ENROLL_PENDING);assert(pair_enroll_status(200)==PAIR_ENROLL_ACCEPTED);
 assert(pair_enroll_status(409)==PAIR_ENROLL_DENIED);assert(pair_enroll_status(410)==PAIR_ENROLL_EXPIRED);
 assert(pair_enroll_status(403)==PAIR_ENROLL_CLOSED);assert(pair_enroll_status(429)==PAIR_ENROLL_RETRY);
 assert(pair_enroll_status(0)==PAIR_ENROLL_UNREACHABLE);assert(pair_enroll_status(500)==PAIR_ENROLL_UNREACHABLE);
}
int main(void){test_identity();test_code();test_hex32();test_pin();test_base();test_list();test_record();test_enroll_wire();
 puts("pair_state: on-device identity, comparison code, TOFU pin, mDNS list, NVS record validation, enroll wire: PASS");return 0;}
