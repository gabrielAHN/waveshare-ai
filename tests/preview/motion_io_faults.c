/* Exercise the real preview; inject one stdio failure only after its real call succeeds. */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static FILE *frame_output;
static const char *fault;
static int injected;
static FILE *fault_fopen(const char *path, const char *mode);
static int fault_fprintf(FILE *f, const char *fmt, ...);
static size_t fault_fwrite(const void *ptr, size_t size, size_t count, FILE *f);
static int fault_printf(const char *fmt, ...);
int fault_fflush(FILE *f);
static int fault_fclose(FILE *f);
#define fopen fault_fopen
#define fprintf fault_fprintf
#define fwrite fault_fwrite
#define printf fault_printf
#define fflush fault_fflush
#define fclose fault_fclose
#define main motion_main
#include "motion_preview.c"
#undef main
#undef fopen
#undef fprintf
#undef fwrite
#undef printf
#undef fflush
#undef fclose

static FILE *fault_fopen(const char *path, const char *mode) {
  FILE *f = fopen(path, mode);
  if (!strcmp(mode, "wb")) frame_output = f;
  return f;
}
static int selected(FILE *f, const char *op) {
  if (!fault || injected || strncmp(fault, op, strlen(op)) || fault[strlen(op)] != ':') return 0;
  const char *sink = fault + strlen(op) + 1;
  return (!strcmp(sink, "labels") && f == labels) ||
         (!strcmp(sink, "frame") && f == frame_output) ||
         (!strcmp(sink, "stdout") && f == stdout);
}
static void attest(const char *op, long real) {
  injected = 1;
  fprintf(stderr, "MOTION_IO_FAULT op=%s sink=%s real=%ld injected=-1\n", op, strchr(fault, ':') + 1, real);
  errno = EIO;
}
static int fault_fprintf(FILE *f, const char *fmt, ...) {
  va_list ap; va_start(ap, fmt); int rc = vfprintf(f, fmt, ap); va_end(ap);
  if (selected(f, "fprintf") && rc >= 0) { attest("fprintf", rc); return -1; }
  return rc;
}
static size_t fault_fwrite(const void *ptr, size_t size, size_t count, FILE *f) {
  size_t rc = fwrite(ptr, size, count, f);
  if (selected(f, "fwrite") && rc == count && count) { attest("fwrite", (long)rc); return count - 1; }
  return rc;
}
static int fault_printf(const char *fmt, ...) {
  va_list ap; va_start(ap, fmt); int rc = vprintf(fmt, ap); va_end(ap);
  if (selected(stdout, "printf") && rc >= 0) { attest("printf", rc); return -1; }
  return rc;
}
int fault_fflush(FILE *f) {
  int rc = fflush(f);
  if (selected(f, "fflush") && rc == 0) { attest("fflush", rc); return EOF; }
  return rc;
}
static int fault_fclose(FILE *f) {
  int fail = selected(f, "fclose");
  int rc = fclose(f);
  if (fail && rc == 0) { attest("fclose", rc); return EOF; }
  return rc;
}
int main(int argc, char **argv) {
  fault = getenv("MOTION_IO_FAULT");
  return motion_main(argc, argv);
}
