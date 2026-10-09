#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Read-only AXP2101 PMU diagnostics (I2C 0x34 on the shared BSP bus) plus the ESP reset reason.
 * Answers "why did it die when USB was not plugged in": the PMU keeps the source of its last
 * power-off / power-on in registers 0x21 / 0x20 across ESP resets. Only write here: REG 0x30 ADC/TS
 * enables (vendor bring-up, TS off so the NTC-less battery charges); the power key registers are
 * written by power_pmu.c through pmu_reg_read/pmu_reg_write. */
typedef struct {
  bool ok;
  int status1, status2, pwron, pwroff, adc;
  bool vbus, battery, charging;
  int vbat_mv, vsys_mv, percent;
} pmu_sample;
bool pmu_diag_init(void);
pmu_sample pmu_read(void);
/* Short, secret-free text for logs and the bridge health header, e.g.
 * "rst=brownout off=vsys_uv on=pwrkey vbus=0 bat=1 vbat=3712 pct=64". */
void pmu_boot_summary(char *out, unsigned cap);
/* Raw AXP2101 register access for the power key (power_pmu.c). -1 / false = no PMU or I2C error. */
int pmu_reg_read(uint8_t reg);
bool pmu_reg_write(uint8_t reg, uint8_t value);
