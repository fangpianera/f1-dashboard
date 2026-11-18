idf_component_register(
    SRCS "main.c" "f1_api.c" "ui.c" "status.c"
         "fonts/font_num56.c" "fonts/font_title40.c" "fonts/font_num28.c"
         "fonts/font_mono20.c" "fonts/font_18.c" "fonts/font_14.c"
    INCLUDE_DIRS "." "fonts"
    REQUIRES lvgl esp_http_client esp_http_server json esp_wifi esp_netif
             nvs_flash esp_timer driver esp_driver_i2c esp-tls mbedtls
             waveshare_esp32_s3_touch_amoled_1_75c
)

# Generated fonts include "lvgl/lvgl.h" unless this macro is set; the ESP-IDF
# component exposes the header as plain "lvgl.h".
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE=1)
