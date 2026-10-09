/* iPhone hotspot names use the iPhone's name, which iOS writes with a curly apostrophe (U+2019,
 * UTF-8 E2 80 99): "Sam’s iPhone". People type a straight one ("Sam's iPhone"). The board keeps one saved
 * hotspot; when the exact name isn't in range it tries the other apostrophe. RED first. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ssid_variant.h"
int main(void){
 char out[33];
 /* straight -> curly */
 assert(ssid_apostrophe_variant("Sam's iPhone",out,sizeof out)&&!strcmp(out,"Sam\xE2\x80\x99s iPhone"));
 /* curly -> straight */
 assert(ssid_apostrophe_variant("Sam\xE2\x80\x99s iPhone",out,sizeof out)&&!strcmp(out,"Sam's iPhone"));
 /* every apostrophe flips; nothing else changes */
 assert(ssid_apostrophe_variant("a'b'c",out,sizeof out)&&!strcmp(out,"a\xE2\x80\x99""b\xE2\x80\x99""c"));
 /* no apostrophe: no variant */
 assert(!ssid_apostrophe_variant("iot_home",out,sizeof out));
 /* the variant must fit an SSID (32 bytes): a straight one grows by 2 bytes */
 assert(!ssid_apostrophe_variant("0123456789012345678901234567'901",out,sizeof out));   /* 32 -> 34 */
 assert(ssid_apostrophe_variant("012345678901234567890123456'89",out,sizeof out)&&strlen(out)==32);
 /* mixed: both directions flip together */
 assert(ssid_apostrophe_variant("x'\xE2\x80\x99y",out,sizeof out)&&!strcmp(out,"x\xE2\x80\x99'y"));
 puts("ssid_variant: straight/curly apostrophe variant for iPhone hotspot names: PASS");
}
