#include "pmu_diag.h"
#include <stdio.h>
#include <string.h>
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_system.h"
/* Narrow declaration (BSP umbrella header drags in LVGL): shared I2C bus, already created by touch. */
i2c_master_bus_handle_t bsp_i2c_get_handle(void);
#define PMU_ADDR 0x34
static i2c_master_dev_handle_t pmu;
static char boot_text[112];

static int rd(uint8_t reg) {
  uint8_t v = 0;
  if (!pmu || i2c_master_transmit_receive(pmu, &reg, 1, &v, 1, 50) != ESP_OK) return -1;
  return v;
}
static int rd14(uint8_t hi, uint8_t lo, uint8_t mask) {
  int h = rd(hi), l = rd(lo);
  return h < 0 || l < 0 ? -1 : ((h & mask) << 8) | l;
}
static const char *reset_name(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "poweron";
    case ESP_RST_EXT: return "ext";
    case ESP_RST_SW: return "sw";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "int_wdt";
    case ESP_RST_TASK_WDT: return "task_wdt";
    case ESP_RST_WDT: return "wdt";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    case ESP_RST_USB: return "usb";
    case ESP_RST_JTAG: return "jtag";
    case ESP_RST_EFUSE: return "efuse";
    case ESP_RST_PWR_GLITCH: return "pwr_glitch";
    case ESP_RST_CPU_LOCKUP: return "cpu_lockup";
    default: return "unknown";
  }
}
/* REG 0x21 PWROFF status: bit7 over-temp, 6 DCDC over-V, 5 DCDC under-V, 4 VBUS over-V,
 * 3 VSYS under-V, 2 PWRON always low (EN mode), 1 software, 0 PWRON long-press (power key). */
static const char *off_name(int v) {
  if (v < 0) return "?";
  if (v & 0x80) return "over_temp";
  if (v & 0x40) return "dcdc_ov";
  if (v & 0x20) return "dcdc_uv";
  if (v & 0x10) return "vbus_ov";
  if (v & 0x08) return "vsys_uv";
  if (v & 0x04) return "en_low";
  if (v & 0x02) return "software";
  if (v & 0x01) return "pwrkey";
  return "none";
}
/* REG 0x20 PWRON status: bit5 always-high, 4 battery insert, 3 battery charged >3.3 V,
 * 2 VBUS insert, 1 IRQ low, 0 power key. */
static const char *on_name(int v) {
  if (v < 0) return "?";
  if (v & 0x04) return "vbus";
  if (v & 0x01) return "pwrkey";
  if (v & 0x10) return "bat_insert";
  if (v & 0x08) return "bat_charged";
  if (v & 0x02) return "irq";
  if (v & 0x20) return "always_high";
  return "none";
}

bool pmu_diag_init(void) {
  i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
  i2c_device_config_t cfg = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = PMU_ADDR, .scl_speed_hz = 400000};
  if (!bus || i2c_master_bus_add_device(bus, &cfg, &pmu) != ESP_OK) { pmu = NULL; }
  /* Waveshare's own AXP2101 bring-up for this board (examples/esp-idf/90_axp2101_pmu): this board has
   * no battery NTC, so TS-pin measurement must be OFF or the PMU flags a battery temperature fault and
   * will not charge -- the board then only lives on USB. Also enable the VBAT/VBUS/VSYS ADCs so the
   * voltages below are real. Read-modify-write of REG 0x30 only; charge current/voltage untouched. */
  int adc = rd(0x30);
  if (adc >= 0) {
    uint8_t want = (uint8_t)((adc & ~0x02) | 0x01 | 0x04 | 0x08);
    if (want != adc) {
      uint8_t w[2] = {0x30, want};
      esp_err_t e = i2c_master_transmit(pmu, w, 2, 50);
      ESP_LOGI("pmu", "PMU_ADC set=0x%02x from=0x%02x ok=%d note=ts-pin-off-vendor-default", want, adc & 0xff, e == ESP_OK);
    }
  }
  pmu_sample s = pmu_read();
  snprintf(boot_text, sizeof boot_text, "rst=%s off=%s on=%s vbus=%d bat=%d vbat=%d pct=%d", reset_name(esp_reset_reason()),
           off_name(s.pwroff), on_name(s.pwron), s.vbus, s.battery, s.vbat_mv, s.percent);
  ESP_LOGI("pmu", "PMU_BOOT %s ok=%d status1=0x%02x status2=0x%02x pwron=0x%02x pwroff=0x%02x adc=0x%02x vsys=%d chg=%d icc=0x%02x cv=0x%02x",
           boot_text, s.ok, s.status1 & 0xff, s.status2 & 0xff, s.pwron & 0xff, s.pwroff & 0xff, s.adc & 0xff, s.vsys_mv, s.charging,
           rd(0x62) & 0xff, rd(0x64) & 0xff);
  return s.ok;
}

pmu_sample pmu_read(void) {
  pmu_sample s = {.vbat_mv = -1, .vsys_mv = -1, .percent = -1};
  s.status1 = rd(0x00);
  s.status2 = rd(0x01);
  s.pwron = rd(0x20);
  s.pwroff = rd(0x21);
  s.adc = rd(0x30);
  s.ok = s.status1 >= 0 && s.status2 >= 0;
  if (!s.ok) return s;
  s.vbus = (s.status1 & 0x20) && !(s.status2 & 0x08);
  s.battery = s.status1 & 0x08;
  s.charging = ((s.status2 >> 5) & 0x03) == 0x01;
  if (s.battery && s.adc >= 0 && (s.adc & 0x01)) s.vbat_mv = rd14(0x34, 0x35, 0x1f);
  if (s.adc >= 0 && (s.adc & 0x08)) s.vsys_mv = rd14(0x3a, 0x3b, 0x3f);
  if (s.battery) s.percent = rd(0xa4);
  return s;
}

void pmu_boot_summary(char *out, unsigned cap) { snprintf(out, cap, "%s", boot_text[0] ? boot_text : "rst=? off=?"); }
/* Raw register access for the power key (power_pmu.c): same device handle, same 50 ms timeout. */
int pmu_reg_read(uint8_t reg) { return rd(reg); }
bool pmu_reg_write(uint8_t reg, uint8_t value) {
  uint8_t w[2] = {reg, value};
  return pmu && i2c_master_transmit(pmu, w, 2, 50) == ESP_OK;
}
