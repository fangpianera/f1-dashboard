#include "status.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "driver/i2c_master.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "status";

/* ---------------- AXP2101 power management IC ---------------- */
#define AXP_ADDR              0x34
#define AXP_REG_STATUS1       0x00
#define AXP_REG_STATUS2       0x01
#define AXP_REG_IC_TYPE       0x03
#define AXP_REG_ADC_CH_CTRL   0x30
#define AXP_REG_BAT_VOLT_H    0x34
#define AXP_REG_BAT_PERCENT   0xA4
#define AXP_CHIP_ID           0x4A

static i2c_master_dev_handle_t s_axp;
static bool s_axp_ready;

static bool axp_read(uint8_t reg, uint8_t *buf, size_t len)
{
    if (!s_axp_ready) return false;
    return i2c_master_transmit_receive(s_axp, &reg, 1, buf, len, 100) == ESP_OK;
}

static bool axp_write(uint8_t reg, uint8_t val)
{
    if (!s_axp_ready) return false;
    uint8_t b[2] = {reg, val};
    return i2c_master_transmit(s_axp, b, 2, 100) == ESP_OK;
}

static void axp_probe(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (!bus) {
        ESP_LOGW(TAG, "no I2C bus, battery icon will stay empty");
        return;
    }

    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP_ADDR,
        .scl_speed_hz = 100000,
    };
    if (i2c_master_bus_add_device(bus, &cfg, &s_axp) != ESP_OK) {
        ESP_LOGW(TAG, "AXP2101 not added to bus");
        return;
    }
    s_axp_ready = true;

    uint8_t id = 0;
    if (!axp_read(AXP_REG_IC_TYPE, &id, 1) || id != AXP_CHIP_ID) {
        ESP_LOGW(TAG, "AXP2101 not found (id=0x%02x)", id);
        s_axp_ready = false;
        return;
    }

    /* Enable only the battery-voltage ADC channel. Never touch the DCDC/LDO
     * registers here: the display rail is powered by this PMU. */
    uint8_t adc = 0;
    if (axp_read(AXP_REG_ADC_CH_CTRL, &adc, 1)) {
        if (!(adc & 0x01)) axp_write(AXP_REG_ADC_CH_CTRL, adc | 0x01);
    }
    ESP_LOGI(TAG, "AXP2101 ready");
}

/* Li-ion discharge curve -> percent, used when the fuel gauge is not ready. */
static int pct_from_mv(int mv)
{
    static const struct { int mv; int pct; } T[] = {
        {4150, 100}, {4050, 90}, {3950, 78}, {3850, 63}, {3750, 45},
        {3650, 28},  {3550, 12}, {3450, 5},  {3300, 0},
    };
    for (size_t i = 0; i + 1 < sizeof(T) / sizeof(T[0]); i++) {
        if (mv >= T[i].mv) return T[i].pct;
        if (mv >= T[i + 1].mv) {
            int span = T[i].mv - T[i + 1].mv;
            int part = T[i].pct - T[i + 1].pct;
            return T[i + 1].pct + (mv - T[i + 1].mv) * part / span;
        }
    }
    return 0;
}

bool status_batt_read(status_batt_t *out)
{
    memset(out, 0, sizeof(*out));
    out->percent = -1;
    if (!s_axp_ready) return false;

    uint8_t st1 = 0, st2 = 0;
    if (!axp_read(AXP_REG_STATUS1, &st1, 1)) return false;
    axp_read(AXP_REG_STATUS2, &st2, 1);

    out->present = (st1 >> 3) & 0x01;
    out->vbus = (st1 >> 5) & 0x01;
    out->charging = ((st2 >> 5) & 0x07) == 0x01;

    int pct = -1;
    uint8_t gp = 0;
    if (axp_read(AXP_REG_BAT_PERCENT, &gp, 1) && gp >= 1 && gp <= 100) pct = gp;

    if (pct < 0) {
        uint8_t v[2] = {0, 0};
        if (axp_read(AXP_REG_BAT_VOLT_H, v, 2)) {
            int mv = ((v[0] & 0x1F) << 8) | v[1];
            if (mv > 2500) pct = pct_from_mv(mv);
        }
    }
    if (out->present) out->percent = pct;
    return true;
}

/* ---------------- WiFi signal ---------------- */
int status_wifi_level(void)
{
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return 0;
    int rssi = ap.rssi;
    if (rssi >= -55) return 4;
    if (rssi >= -65) return 3;
    if (rssi >= -75) return 2;
    if (rssi >= -88) return 1;
    return 0;
}

/* ---------------- clock ---------------- */
static bool s_sntp_started;

void status_init(void)
{
    /* newlib parses POSIX TZ strings directly, no tzdata file needed. */
    setenv("TZ", "CST-8", 1);
    tzset();
    axp_probe();
}

void status_time_sync_start(void)
{
    if (s_sntp_started) return;
    s_sntp_started = true;
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com");
    cfg.smooth_sync = false;
    if (esp_netif_sntp_init(&cfg) != ESP_OK) {
        ESP_LOGW(TAG, "SNTP start failed");
        return;
    }
    ESP_LOGI(TAG, "SNTP started (ntp.aliyun.com)");
}

bool status_time_valid(void)
{
    /* ~2023-11-14; anything below means the clock has not been set yet. */
    return time(NULL) > 1700000000;
}
