/* Freeze guard: a stale render/touch heartbeat is a freeze; fresh or not-yet-started beats are not. */
#include <assert.h>
#include <stdio.h>
#include "freeze_guard.h"

int main(void) {
  const int64_t s = 1000000;
  assert(freeze_check(10 * s, 0, 0) == FREEZE_NONE);                 /* boot: nothing started yet */
  assert(freeze_check(10 * s, 9 * s, 9 * s + 500) == FREEZE_NONE);   /* both beating */
  assert(freeze_check(10 * s, 5 * s, 9 * s) == FREEZE_NONE);         /* exactly the limit: not yet */
  assert(freeze_check(10 * s, 5 * s - 1, 9 * s) == FREEZE_OWNER);    /* render owner stuck */
  assert(freeze_check(20 * s, 19 * s, 10 * s) == FREEZE_TOUCH);      /* touch poller stuck */
  assert(freeze_check(20 * s, 1 * s, 1 * s) == FREEZE_OWNER);        /* both: the owner is named */
  assert(freeze_check(20 * s, 0, 1 * s) == FREEZE_TOUCH);
  freeze_record r = {FREEZE_MAGIC, FREEZE_OWNER, PH_WAIT_PANEL, PP_SENDING, 3, 250};
  assert(r.magic == FREEZE_MAGIC && freeze_reason_name(r.reason)[0] == 'o');
  assert(freeze_reason_name(FREEZE_PRESENT)[0] == 'p' && freeze_reason_name(99)[0] == 'n');
  puts("freeze_guard_test ok");
  return 0;
}
