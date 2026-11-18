#pragma once

#include "lvgl.h"
#include "f1_api.h"

/* Create the three-page F1 dashboard on a 466x466 round screen. */
lv_obj_t *f1_ui_create(void);

/* Push latest standings/calendar data (safe from any task; deferred to LVGL). */
void f1_ui_update(const f1_standings_t *data);

/* Register callback for long-press on center (immediate refresh). */
void f1_ui_set_refresh_cb(void (*cb)(void));
