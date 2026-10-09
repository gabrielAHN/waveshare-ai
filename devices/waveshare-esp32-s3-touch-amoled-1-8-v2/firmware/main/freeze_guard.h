#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Freeze guard (user report 2026-09-29: the Ask page froze, picture and touch, no crash). A panic
 * reboots the board, but a task blocked forever (a display transfer that never completes, a lock that
 * is never given back) just freezes it. The render owner and the touch poller each publish a
 * heartbeat; a separate guard task checks them without taking any lock. A heartbeat older than
 * FREEZE_STALL_US means frozen: the guard records why (survives the restart in RTC memory) and
 * restarts the board, so a freeze costs ~5 s instead of forever. */
#define FREEZE_STALL_US (5LL * 1000 * 1000)
#define FREEZE_MAGIC 0x46525A31u  /* "FRZ1" */
enum { FREEZE_NONE, FREEZE_OWNER, FREEZE_TOUCH, FREEZE_PRESENT };
/* Owner/presenter phase markers: where a frozen task was stuck. */
enum { PH_LOOP = 1, PH_SNAPSHOT, PH_RENDER, PH_WAIT_PANEL, PH_HANDOFF, PH_IDLE, PH_POWER };
enum { PP_WAITING = 1, PP_SENDING, PP_DRAW, PP_WAIT };  /* DRAW = inside esp_lcd_panel_draw_bitmap, WAIT = done semaphore */
typedef struct { uint32_t magic, reason, owner_phase, present_phase, page, uptime_s; } freeze_record;

static inline const char *freeze_reason_name(uint32_t r) {
  return r == FREEZE_OWNER ? "owner" : r == FREEZE_TOUCH ? "touch" : r == FREEZE_PRESENT ? "present" : "none";
}
/* Which task (if any) is frozen. A beat of 0 = not started yet (boot): never a freeze. */
static inline int freeze_check(int64_t now, int64_t owner_beat, int64_t touch_beat) {
  if (owner_beat && now - owner_beat > FREEZE_STALL_US) return FREEZE_OWNER;
  if (touch_beat && now - touch_beat > FREEZE_STALL_US) return FREEZE_TOUCH;
  return FREEZE_NONE;
}
