#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Battery snapshot read from the on-board AXP2101 PMU (I2C 0x34). */
typedef struct {
    int percent;     /* 0..100, -1 when unknown */
    bool charging;   /* charge in progress */
    bool present;    /* battery physically connected */
    bool vbus;       /* USB / VBUS present */
} status_batt_t;

/* Set timezone, open the PMU device node. Call once after BSP display start. */
void status_init(void);

/* Start SNTP. Safe to call repeatedly; only the first call does work. */
void status_time_sync_start(void);

/* True when the clock has been set to something plausible (post-SNTP). */
bool status_time_valid(void);

/* WiFi signal bars 0..4 (0 = not connected / unknown). */
int status_wifi_level(void);

/* Read battery state; returns false when the PMU did not answer. */
bool status_batt_read(status_batt_t *out);
