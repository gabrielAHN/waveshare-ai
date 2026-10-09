#pragma once
/* iPhone hotspot names are the iPhone's name, which iOS writes with a curly apostrophe (U+2019, UTF-8
 * E2 80 99), e.g. "Sam’s iPhone"; people type a straight one. ssid_apostrophe_variant() swaps every
 * straight <-> curly apostrophe. Returns false when the name has none or the result is over 32 bytes. */
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
static inline bool ssid_apostrophe_variant(const char*in,char*out,size_t cap){
 size_t o=0;bool changed=false;
 for(size_t i=0;in[i];){
  const unsigned char*u=(const unsigned char*)in+i;
  if(u[0]=='\''){
   if(o+3>32||o+3>=cap)return false;
   out[o++]=(char)0xE2;out[o++]=(char)0x80;out[o++]=(char)0x99;i++;changed=true;
  }else if(u[0]==0xE2&&u[1]==0x80&&u[2]==0x99){
   if(o+1>32||o+1>=cap)return false;
   out[o++]='\'';i+=3;changed=true;
  }else{
   if(o+1>32||o+1>=cap)return false;
   out[o++]=in[i++];
  }
 }
 out[o]=0;return changed;
}
