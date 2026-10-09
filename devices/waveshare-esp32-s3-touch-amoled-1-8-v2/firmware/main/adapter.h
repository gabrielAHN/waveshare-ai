#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
/* Panel byte order for one window of the 368-wide RGB565 shadow frame (direct_present.h): big-endian
   pixel bytes, row by row. No LCD/DMA access here. */
static inline void pack_window(const uint16_t *shadow, uint8_t *out, int x, int y, int w, int h) {
 for(int j=0;j<h;j++) for(int i=0;i<w;i++) {
  uint16_t p=shadow[(y+j)*368+x+i]; *out++=p>>8; *out++=p&255;
 }
}
