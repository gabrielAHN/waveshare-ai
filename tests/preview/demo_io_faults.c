/* Close/flush fault injection around the real demo, complementary to RLIMIT_FSIZE probes.
 * All writes still reach real stdio; only the selected final result is changed to EOF. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static FILE *frame_output;
static FILE *fault_fopen(const char *path, const char *mode);
static int fault_fflush(FILE *f);
static int fault_fclose(FILE *f);
#define fopen fault_fopen
#define fflush fault_fflush
#define fclose fault_fclose
#define main demo_main
#include "demo_video.c"
#undef main
#undef fopen
#undef fflush
#undef fclose

static FILE *fault_fopen(const char *path, const char *mode) {
  const char *fault = getenv("DEMO_IO_FAULT");
  /* Flush/close probes exercise real stdio, but need no per-frame files. A broken implementation
   * may finish the entire storyboard, so discard pixels instead of filling the scratch disk. */
  bool discard = !strcmp(mode, "wb") && fault &&
                 (!strcmp(fault, "flush:frame") || !strcmp(fault, "close:frame"));
  FILE *f = fopen(discard ? "/dev/null" : path, mode);
  if (!strcmp(mode, "wb")) frame_output = f;
  return f;
}
static bool selected(FILE *f, const char *op) {
  const char *fault = getenv("DEMO_IO_FAULT");
  if (!fault || strncmp(fault, op, strlen(op)) || fault[strlen(op)] != ':') return false;
  const char *sink = fault + strlen(op) + 1;
  return (!strcmp(sink, "stdout") && f == stdout) || (!strcmp(sink, "storyboard") && f == story) ||
         (!strcmp(sink, "gif") && f == gif) || (!strcmp(sink, "frame") && f == frame_output);
}
static int fault_fflush(FILE *f) {
  int rc = fflush(f);
  if (selected(f, "flush")) { errno = EIO; return EOF; }
  return rc;
}
static int fault_fclose(FILE *f) {
  bool fail = selected(f, "close");
  int rc = fclose(f);
  if (fail) { errno = EIO; return EOF; }
  return rc;
}
int main(int argc, char **argv) { return demo_main(argc, argv); }
