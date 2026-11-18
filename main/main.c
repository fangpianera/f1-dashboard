#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_system.h"

#include "lvgl.h"
#include "bsp/esp-bsp.h"

#include "f1_api.h"
#include "status.h"
#include "ui.h"

static const char *TAG = "f1_main";


static f1_standings_t g_standings;
static volatile bool g_refresh_requested = false;

static void do_refresh(void)
{
    if (g_refresh_requested) return;
    g_refresh_requested = true;   /* fetch task picks it up */
}

static void fetch_task(void *arg)
{
    TickType_t delay = pdMS_TO_TICKS(2000);
    int kick = 0;
    while (1) {
        vTaskDelay(delay);
        if (!f1_wifi_is_connected()) {
            if (++kick >= 3) { kick = 0; f1_wifi_kick(); }
            delay = pdMS_TO_TICKS(5000);
            continue;
        }
        kick = 0;
        if (!status_time_valid()) status_time_sync_start();
        bool forced = g_refresh_requested;
        g_refresh_requested = false;
        f1_fetch_result_t r = f1_fetch_standings(&g_standings);
        if (r == F1_FETCH_OK) {
            f1_ui_update(&g_standings);
            delay = forced ? pdMS_TO_TICKS(3000) : pdMS_TO_TICKS(5 * 60 * 1000);
        } else {
            ESP_LOGW(TAG, "fetch failed: %d", r);
            delay = pdMS_TO_TICKS(30000);
        }
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    bsp_display_start();
    /* No bsp_display_backlight_on() here: the panel init sequence already sets
     * brightness 0x51 = 0xFF, and calling it now races with the LVGL task's SPI
     * polling transaction ("Cannot acquire bus..."), which wedged app_main.
     * Runtime dimming for screen-off still goes through the BSP. */

    status_init();   /* timezone + PMU (battery) + I2C node, before any UI read */

    bsp_display_lock(-1);
    f1_ui_create();
    f1_ui_set_refresh_cb(do_refresh);
    bsp_display_unlock();

    f1_wifi_init();
    xTaskCreate(fetch_task, "f1_fetch", 8192, NULL, 5, NULL);

    ESP_LOGI(TAG, "F1 dashboard running");
}
