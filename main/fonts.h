#pragma once
#include "lvgl.h"

/* F1-style fonts generated from Titillium (F1's long-time digital typeface).
 * Digits are tabular (equal width) so the countdown never jitters. */
LV_FONT_DECLARE(font_num56);    /* 56px leader number */
LV_FONT_DECLARE(font_title40);  /* 40px circuit / team code */
LV_FONT_DECLARE(font_num28);    /* 28px countdown, tabular */
LV_FONT_DECLARE(font_mono20);   /* 20px standings columns */
LV_FONT_DECLARE(font_18);       /* 18px secondary */
LV_FONT_DECLARE(font_14);       /* 14px hints */
