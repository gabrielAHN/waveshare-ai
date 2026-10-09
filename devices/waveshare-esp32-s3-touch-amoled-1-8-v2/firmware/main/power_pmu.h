#pragma once
#include <stdbool.h>
#include <stdint.h>
/* AXP2101 power key (I2C 0x34 on the shared BSP bus, through pmu_diag.c's device handle).
 * The values come from power_button.h (host-tested). The AXP2101 IRQ pin is not wired to an ESP32
 * GPIO on this board, so REG 0x49 is polled (power_main.c). */
#define AXP_REG_SOFT_OFF 0x10   /* bit0 = software power off */
#define AXP_REG_PWROFF_EN 0x22  /* bit1 = PWRON > OFFLEVEL powers off, bit0 = 0 off / 1 restart */
#define AXP_REG_KEY_TIMING 0x27 /* IRQLEVEL / OFFLEVEL / ONLEVEL */
#define AXP_REG_IRQ_EN2 0x41    /* PWRON IRQ enables (bits 0-3) */
#define AXP_REG_IRQ_ST2 0x49    /* PWRON IRQ status (write 1 to clear) */
/* Boot: REG 0x27 = 1 s IRQ / 6 s hardware off backstop / 128 ms power on, REG 0x22 long press = power
 * off, REG 0x41 the four PWRON IRQs on, stale REG 0x49 PWRON bits cleared. Logs
 * "POWER_PMU cfg=0x.. pwroff_en=0x.. irq_en1=0x.. ok=0|1" (read back). */
bool power_pmu_init(void);
/* REG 0x49 as read (-1 = I2C error); its PWRON bits are cleared when set. */
int power_pmu_irq(void);
/* 1 = VBUS good (USB power), 0 = battery, -1 = unknown. */
int power_pmu_vbus(void);
/* REG 0x10 bit0: everything but VRTC goes off. Returns only if the write failed or power stayed on. */
bool power_pmu_off(void);
