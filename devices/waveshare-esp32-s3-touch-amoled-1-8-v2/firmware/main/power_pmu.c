/* AXP2101 power key registers (see power_pmu.h). Values from power_button.h (host-tested). */
#include "power_pmu.h"
#include "power_button.h"
#include "pmu_diag.h"
#include "esp_log.h"

static const char *TAG = "power";

/* Read-modify-write one register; `readback` = the value read back afterwards (-1 on I2C error). */
static bool rmw(uint8_t reg, uint8_t (*value)(uint8_t), int *readback) {
  int old = pmu_reg_read(reg);
  *readback = -1;
  if (old < 0) return false;
  uint8_t want = value((uint8_t)old);
  if (want != (uint8_t)old && !pmu_reg_write(reg, want)) return false;
  *readback = pmu_reg_read(reg);
  return *readback == want;
}

bool power_pmu_init(void) {
  int cfg = -1, off = -1, en = -1;
  bool timing = rmw(AXP_REG_KEY_TIMING, axp_key_timing, &cfg);
  bool poweroff = rmw(AXP_REG_PWROFF_EN, axp_key_poweroff, &off);
  bool irqs = rmw(AXP_REG_IRQ_EN2, axp_key_irq_enable, &en);
  /* Drop PWRON IRQs latched before we were listening (the power-on press itself). */
  bool cleared = pmu_reg_write(AXP_REG_IRQ_ST2, PWR_IRQ_KEY);
  bool ok = timing && poweroff && irqs && cleared;
  ESP_LOGI(TAG, "POWER_PMU cfg=0x%02x pwroff_en=0x%02x irq_en1=0x%02x ok=%d", (unsigned)(cfg & 0xff), (unsigned)(off & 0xff),
           (unsigned)(en & 0xff), ok);
  return ok;
}

int power_pmu_irq(void) {
  int v = pmu_reg_read(AXP_REG_IRQ_ST2);
  if (v > 0 && (v & PWR_IRQ_KEY)) (void)pmu_reg_write(AXP_REG_IRQ_ST2, (uint8_t)(v & PWR_IRQ_KEY));  /* write 1 to clear */
  return v;
}

int power_pmu_vbus(void) {
  int s1 = pmu_reg_read(0x00), s2 = pmu_reg_read(0x01);
  if (s1 < 0 || s2 < 0) return -1;
  return (s1 & 0x20) && !(s2 & 0x08) ? 1 : 0;  /* same rule as pmu_read() */
}

bool power_pmu_off(void) {
  int old = pmu_reg_read(AXP_REG_SOFT_OFF);
  if (old < 0) return false;
  return pmu_reg_write(AXP_REG_SOFT_OFF, axp_soft_off((uint8_t)old));
}
