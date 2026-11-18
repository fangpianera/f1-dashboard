#include "ui.h"
#include "fonts.h"
#include "status.h"
#include "bsp/esp-bsp.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* Idle screen-off for AMOLED burn-in protection */
#define IDLE_OFF_MS  20000
#define BRIGHT_ON    100
#define BRIGHT_OFF   0

/* F1 palette — no large pure-white blocks */
#define C_BG      0x000000
#define C_TEXT    0xE8E8E8
#define C_MUTED   0x9AA0A6
#define C_DIM     0x5A6068
#define C_RED     0xE10600

static lv_obj_t *g_page[3];
static int g_page_idx = 0;

/* ---- page 1: next race dial ---- */
static lv_obj_t *p1_arc, *p1_ring, *p1_code, *p1_count, *p1_tloc, *p1_thome, *p1_hdr;
static lv_obj_t *p1_bar[3], *p1_barlbl[3];

/* ---- page 2: drivers ---- */
static lv_obj_t *p2_arc[5];
static lv_obj_t *p2_box, *p2_ring, *p2_code, *p2_pts, *p2_sub, *p2_top;
static lv_obj_t *p2_row[4];

/* ---- page 3: constructors ---- */
static lv_obj_t *p3_arc[5];
static lv_obj_t *p3_box, *p3_ring, *p3_code, *p3_pts, *p3_sub;
static lv_obj_t *p3_last1, *p3_last2;
static lv_obj_t *p3_row[4];

static bool p2_list_on = false;
static bool p3_list_on = false;

/* status strip: clock / date / wifi bars / battery */
static lv_obj_t *st_time, *st_date, *st_wifi[4];
static lv_obj_t *st_bat_cell, *st_bat_fill, *st_bat_nub;
static int st_last_min = -1, st_last_day = -1, st_last_wifi = -1, st_last_pct = -2;
static bool st_last_chg = false;
static uint32_t st_next_batt_ms = 0;

static f1_standings_t g_data;
static bool g_has_data = false;
static void (*g_refresh_cb)(void) = NULL;

static bool g_screen_off = false;
static int g_burn_phase = 0;

static lv_color_t col(uint32_t hex) { return lv_color_hex(hex); }

static lv_obj_t *mklabel(lv_obj_t *parent, const char *txt, const lv_font_t *font,
                         uint32_t color, lv_text_align_t align)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col(color), 0);
    lv_obj_set_style_text_align(l, align, 0);
    return l;
}

static lv_obj_t *mkarc(lv_obj_t *parent, int radius, uint32_t color, int width, bool rounded)
{
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_remove_style_all(a);
    lv_obj_set_size(a, radius * 2, radius * 2);
    lv_obj_center(a);
    lv_arc_set_bg_angles(a, 0, 360);
    lv_arc_set_angles(a, 0, 0);
    lv_obj_set_style_arc_color(a, col(color), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
    /* Rounded caps on the thick rings leave stray coloured dots on the dial,
     * so only the thin progress arc uses them. */
    lv_obj_set_style_arc_rounded(a, rounded, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, col(0x1A1A1E), LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    return a;
}

/* The centre badge is shared by all three pages: same diameter, same border
 * weight, same typography - only the colour and content differ. */
static lv_obj_t *mkbadge(lv_obj_t *parent, uint32_t border)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 150, 150);
    lv_obj_set_style_radius(b, 75, 0);
    lv_obj_set_style_bg_color(b, col(C_BG), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(b, col(border), 0);
    lv_obj_set_style_border_width(b, 3, 0);
    lv_obj_center(b);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
    return b;
}

/* ---------------- page 1: next race ---------------- */
static void build_page1(lv_obj_t *page)
{
    p1_arc = mkarc(page, 190, C_RED, 3, true);

    /* constructor points, above the dial */
    for (int i = 0; i < 3; i++) {
        p1_bar[i] = lv_obj_create(page);
        lv_obj_remove_style_all(p1_bar[i]);
        lv_obj_set_size(p1_bar[i], 80, 7);
        lv_obj_set_style_radius(p1_bar[i], 4, 0);
        lv_obj_set_style_bg_color(p1_bar[i], col(C_DIM), 0);
        lv_obj_set_style_bg_opa(p1_bar[i], LV_OPA_COVER, 0);
        lv_obj_align(p1_bar[i], LV_ALIGN_CENTER, -20, -168 + i * 20);
        lv_obj_remove_flag(p1_bar[i], LV_OBJ_FLAG_CLICKABLE);

        p1_barlbl[i] = mklabel(page, "---", &font_14, C_MUTED, LV_TEXT_ALIGN_LEFT);
        lv_obj_align(p1_barlbl[i], LV_ALIGN_CENTER, 40, -168 + i * 20);
    }

    /* circuit + round header */
    p1_hdr = mklabel(page, "---  R--/--", &font_14, C_MUTED, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(p1_hdr, LV_ALIGN_CENTER, 0, -104);

    p1_ring = mkbadge(page, C_RED);
    p1_code = mklabel(p1_ring, "---", &font_title40, C_RED, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(p1_code);

    p1_count = mklabel(page, "--d --h --m", &font_num28, C_TEXT, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(p1_count, LV_ALIGN_CENTER, 0, 96);
    /* session start: circuit local time and the wearer's local time */
    p1_tloc = mklabel(page, "LOC -- --- --:--", &font_14, C_TEXT, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(p1_tloc, LV_ALIGN_CENTER, 0, 126);
    p1_thome = mklabel(page, "HOM -- --- --:--", &font_14, C_DIM, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(p1_thome, LV_ALIGN_CENTER, 0, 147);
}

/* ---------------- page 2: drivers ---------------- */
static void build_page2(lv_obj_t *page)
{
    for (int i = 0; i < 5; i++) p2_arc[i] = mkarc(page, 190 - i * 9, C_DIM, 9, false);

    p2_top = mklabel(page, "---", &font_14, C_MUTED, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(p2_top, LV_ALIGN_CENTER, 0, -100);

    /* clickable centre: tap toggles leader / P2-P5 list */
    p2_box = lv_obj_create(page);
    lv_obj_remove_style_all(p2_box);
    lv_obj_set_size(p2_box, 164, 164);
    lv_obj_center(p2_box);
    lv_obj_remove_flag(p2_box, LV_OBJ_FLAG_SCROLLABLE);

    p2_ring = mkbadge(p2_box, C_RED);
    p2_code = mklabel(p2_ring, "--", &font_title40, C_TEXT, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(p2_code);

    p2_pts = mklabel(page, "-- PTS", &font_18, C_TEXT, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(p2_pts, LV_ALIGN_CENTER, 0, 96);
    p2_sub = mklabel(page, "DRIVERS", &font_14, C_DIM, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(p2_sub, LV_ALIGN_CENTER, 0, 124);

    for (int i = 0; i < 4; i++) {
        p2_row[i] = mklabel(page, "", &font_mono20, C_MUTED, LV_TEXT_ALIGN_CENTER);
        lv_obj_align(p2_row[i], LV_ALIGN_CENTER, 0, -45 + i * 30);
        lv_obj_add_flag(p2_row[i], LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---------------- page 3: constructors ---------------- */
static void build_page3(lv_obj_t *page)
{
    for (int i = 0; i < 5; i++) p3_arc[i] = mkarc(page, 190 - i * 9, C_DIM, 9, false);

    p3_last1 = mklabel(page, "LAST RACE ---", &font_14, C_MUTED, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(p3_last1, LV_ALIGN_CENTER, 0, -104);
    p3_last2 = mklabel(page, "WIN ---  FAST ---", &font_14, C_DIM, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(p3_last2, LV_ALIGN_CENTER, 0, -82);

    p3_box = lv_obj_create(page);
    lv_obj_remove_style_all(p3_box);
    lv_obj_set_size(p3_box, 164, 164);
    lv_obj_center(p3_box);
    lv_obj_remove_flag(p3_box, LV_OBJ_FLAG_SCROLLABLE);

    p3_ring = mkbadge(p3_box, C_RED);
    p3_code = mklabel(p3_ring, "---", &font_title40, C_TEXT, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(p3_code);

    p3_pts = mklabel(page, "-- PTS", &font_18, C_TEXT, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(p3_pts, LV_ALIGN_CENTER, 0, 96);
    p3_sub = mklabel(page, "CONSTRUCTORS", &font_14, C_DIM, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(p3_sub, LV_ALIGN_CENTER, 0, 124);

    for (int i = 0; i < 4; i++) {
        p3_row[i] = mklabel(page, "", &font_mono20, C_MUTED, LV_TEXT_ALIGN_CENTER);
        lv_obj_align(p3_row[i], LV_ALIGN_CENTER, 0, -45 + i * 30);
        lv_obj_add_flag(p3_row[i], LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---------------- leader / list toggle ---------------- */
static void p2_apply(bool list_on)
{
    p2_list_on = list_on;
    lv_obj_t *head[] = {p2_ring, p2_pts, p2_sub, p2_top};
    for (int i = 0; i < 4; i++) {
        if (list_on) lv_obj_add_flag(head[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(head[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < 4; i++) {
        if (list_on) lv_obj_remove_flag(p2_row[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(p2_row[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void p3_apply(bool list_on)
{
    p3_list_on = list_on;
    lv_obj_t *head[] = {p3_ring, p3_pts, p3_sub, p3_last1, p3_last2};
    for (int i = 0; i < 5; i++) {
        if (list_on) lv_obj_add_flag(head[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(head[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < 4; i++) {
        if (list_on) lv_obj_remove_flag(p3_row[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(p3_row[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void p2_center_cb(lv_event_t *e)
{
    (void)e;
    p2_apply(!p2_list_on);
}

static void p3_center_cb(lv_event_t *e)
{
    (void)e;
    p3_apply(!p3_list_on);
}

/* ---------------- status strip ---------------- */
static void build_status(lv_obj_t *scr)
{
    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, 24);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 7, 0);
    /* y=13: at this height the round bezel still leaves ~200px of width */
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 13);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    st_time = mklabel(row, "--:--", &font_18, C_TEXT, LV_TEXT_ALIGN_CENTER);
    st_date = mklabel(row, "--/--", &font_14, C_MUTED, LV_TEXT_ALIGN_CENTER);

    lv_obj_t *wf = lv_obj_create(row);
    lv_obj_remove_style_all(wf);
    lv_obj_set_size(wf, 21, 16);
    lv_obj_remove_flag(wf, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(wf, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = lv_obj_create(wf);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 3, 4 + i * 4);
        lv_obj_set_style_radius(b, 1, 0);
        lv_obj_set_style_bg_color(b, col(0x3A3F45), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, i * 6, 0);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
        st_wifi[i] = b;
    }

    lv_obj_t *bt = lv_obj_create(row);
    lv_obj_remove_style_all(bt);
    lv_obj_set_size(bt, 27, 14);
    lv_obj_remove_flag(bt, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(bt, LV_OBJ_FLAG_SCROLLABLE);

    st_bat_cell = lv_obj_create(bt);
    lv_obj_remove_style_all(st_bat_cell);
    lv_obj_set_size(st_bat_cell, 22, 12);
    lv_obj_align(st_bat_cell, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(st_bat_cell, 3, 0);
    lv_obj_set_style_border_width(st_bat_cell, 1, 0);
    lv_obj_set_style_border_color(st_bat_cell, col(0x6E747C), 0);
    lv_obj_remove_flag(st_bat_cell, LV_OBJ_FLAG_CLICKABLE);

    st_bat_fill = lv_obj_create(bt);
    lv_obj_remove_style_all(st_bat_fill);
    lv_obj_set_size(st_bat_fill, 0, 6);
    lv_obj_align(st_bat_fill, LV_ALIGN_LEFT_MID, 3, 0);
    lv_obj_set_style_radius(st_bat_fill, 1, 0);
    lv_obj_set_style_bg_color(st_bat_fill, col(C_TEXT), 0);
    lv_obj_set_style_bg_opa(st_bat_fill, LV_OPA_COVER, 0);
    lv_obj_remove_flag(st_bat_fill, LV_OBJ_FLAG_CLICKABLE);

    st_bat_nub = lv_obj_create(bt);
    lv_obj_remove_style_all(st_bat_nub);
    lv_obj_set_size(st_bat_nub, 3, 6);
    lv_obj_align(st_bat_nub, LV_ALIGN_LEFT_MID, 23, 0);
    lv_obj_set_style_radius(st_bat_nub, 1, 0);
    lv_obj_set_style_bg_color(st_bat_nub, col(0x6E747C), 0);
    lv_obj_set_style_bg_opa(st_bat_nub, LV_OPA_COVER, 0);
    lv_obj_remove_flag(st_bat_nub, LV_OBJ_FLAG_CLICKABLE);
}

static void draw_status_clock(void)
{
    static bool showing_blank = false;

    if (!status_time_valid()) {
        if (!showing_blank) {
            lv_label_set_text(st_time, "--:--");
            lv_label_set_text(st_date, "--/--");
            showing_blank = true;
            st_last_min = st_last_day = -1;
        }
        return;
    }
    showing_blank = false;

    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    int day = t.tm_mon * 100 + t.tm_mday;
    if (t.tm_min != st_last_min) {
        char s[8];
        snprintf(s, sizeof(s), "%02d:%02d", t.tm_hour, t.tm_min);
        lv_label_set_text(st_time, s);
        st_last_min = t.tm_min;
    }
    if (day != st_last_day) {
        char s[12];
        snprintf(s, sizeof(s), "%02d/%02d", t.tm_mon + 1, t.tm_mday);
        lv_label_set_text(st_date, s);
        st_last_day = day;
    }
}

static void draw_status_wifi(void)
{
    int lvl = status_wifi_level();
    if (lvl == st_last_wifi) return;
    for (int i = 0; i < 4; i++)
        lv_obj_set_style_bg_color(st_wifi[i], col(i < lvl ? C_TEXT : 0x3A3F45), 0);
    st_last_wifi = lvl;
}

static void draw_status_batt(void)
{
    status_batt_t b;
    if (!status_batt_read(&b)) {
        lv_obj_set_width(st_bat_fill, 0);
        lv_obj_set_style_border_color(st_bat_cell, col(0x3A3F45), 0);
        st_last_pct = -1;
        return;
    }
    if (b.percent == st_last_pct && b.charging == st_last_chg) return;

    uint32_t c;
    if (!b.present)           c = 0x3A3F45;
    else if (b.charging)      c = 0x2ECC71;
    else if (b.percent <= 15) c = C_RED;
    else if (b.percent <= 40) c = 0xF5A524;
    else                      c = C_TEXT;

    lv_obj_set_style_border_color(st_bat_cell, col(b.present ? c : 0x3A3F45), 0);
    lv_obj_set_style_bg_color(st_bat_fill, col(c), 0);
    int w = 0;
    if (b.present && b.percent > 0) {
        w = 16 * b.percent / 100;
        if (w < 2) w = 2;
    }
    lv_obj_set_width(st_bat_fill, w);
    st_last_pct = b.percent;
    st_last_chg = b.charging;
}

/* ---------------- page dots ---------------- */
static lv_obj_t *g_dot[3];

static void build_dots(lv_obj_t *scr)
{
    for (int i = 0; i < 3; i++) {
        lv_obj_t *d = lv_obj_create(scr);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 6, 6);
        lv_obj_set_style_radius(d, 3, 0);
        lv_obj_set_style_bg_color(d, col(i == 0 ? C_RED : 0x3A3F45), 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_align(d, LV_ALIGN_BOTTOM_MID, (i - 1) * 14, -14);
        lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE);
        g_dot[i] = d;
    }
}

static void dots_update(void)
{
    for (int i = 0; i < 3; i++)
        lv_obj_set_style_bg_color(g_dot[i], col(i == g_page_idx ? C_RED : 0x3A3F45), 0);
}

static void page_show(int idx)
{
    if (idx < 0) idx = 0;
    if (idx > 2) idx = 2;
    g_page_idx = idx;
    for (int i = 0; i < 3; i++) {
        if (i == idx) lv_obj_remove_flag(g_page[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(g_page[i], LV_OBJ_FLAG_HIDDEN);
    }
    /* leaving a page always returns it to its leader view */
    p2_apply(false);
    p3_apply(false);
    /* force a full repaint of the page we are entering: label text is set while
     * the page is hidden, and a hidden object does not invalidate itself. */
    lv_obj_invalidate(g_page[idx]);
    dots_update();
}

/* ---------------- rendering ---------------- */
static void team_abbr(const char *n, char *out, size_t len)
{
    if (strstr(n, "McLaren")) snprintf(out, len, "MCL");
    else if (strstr(n, "Red Bull")) snprintf(out, len, "RBR");
    else if (strstr(n, "Ferrari")) snprintf(out, len, "FER");
    else if (strstr(n, "Mercedes")) snprintf(out, len, "MER");
    else if (strstr(n, "Aston Martin")) snprintf(out, len, "AMR");
    else if (strstr(n, "Alpine")) snprintf(out, len, "ALP");
    else if (strstr(n, "Williams")) snprintf(out, len, "WIL");
    else if (strstr(n, "Racing Bulls")) snprintf(out, len, "RBU");
    else if (strstr(n, "Haas")) snprintf(out, len, "HAS");
    else if (strstr(n, "Sauber")) snprintf(out, len, "SAU");
    else snprintf(out, len, "%.3s", n);
}

static const char *MON[] = {"JAN","FEB","MAR","APR","MAY","JUN",
                            "JUL","AUG","SEP","OCT","NOV","DEC"};
static bool p1_dirty = false;

/* Format an epoch in a given POSIX timezone, e.g. "LOC 21 NOV 19:00". */
static void fmt_at(time_t ts, const char *tz, const char *tag, char *out, size_t n)
{
    setenv("TZ", tz, 1);
    tzset();
    struct tm t;
    localtime_r(&ts, &t);
    snprintf(out, n, "%s %02d %s %02d:%02d", tag, t.tm_mday, MON[t.tm_mon],
             t.tm_hour, t.tm_min);
}

/* Show the nearest upcoming session of the weekend (FP1 -> ... -> RACE). */
static void p1_session_update(void)
{
    if (!g_has_data) return;
    f1_nextrace_t *r = &g_data.next_race;
    if (r->session_count <= 0) return;

    time_t now = time(NULL);
    int idx = -1;
    for (int i = 0; i < r->session_count; i++) {
        if (r->sessions[i].start_ts > now) { idx = i; break; }
    }
    if (idx < 0) idx = r->session_count - 1;   /* weekend already finished */

    static int last_idx = -1;
    static char last_cd[32] = {0};

    f1_session_t *s = &r->sessions[idx];
    if (idx != last_idx || p1_dirty) {
        lv_label_set_text(p1_code, s->name);
        char a[32], b[32];
        fmt_at(s->start_ts, r->tz[0] ? r->tz : "UTC0", "LOC", a, sizeof(a));
        fmt_at(s->start_ts, "CST-8", "HOM", b, sizeof(b));
        setenv("TZ", "CST-8", 1);
        tzset();                      /* the clock in the status bar uses it */
        lv_label_set_text(p1_tloc, a);
        lv_label_set_text(p1_thome, b);
        last_idx = idx;
        last_cd[0] = 0;
        p1_dirty = false;
    }

    long diff = (long)(s->start_ts - now);
    if (diff < 0) diff = 0;
    char cd[32];
    int d = (int)(diff / 86400);
    int h = (int)((diff % 86400) / 3600);
    int m = (int)((diff % 3600) / 60);
    snprintf(cd, sizeof(cd), "%02dd %02dh %02dm", d, h, m);
    if (strcmp(cd, last_cd) != 0) {
        lv_label_set_text(p1_count, cd);
        strcpy(last_cd, cd);
    }
}

static void draw_page1(void)
{
    if (!g_has_data) return;
    f1_nextrace_t *r = &g_data.next_race;

    char h[40];
    snprintf(h, sizeof(h), "%s   R%02d/%02d", r->code[0] ? r->code : "---",
             r->round, r->total_rounds);
    lv_label_set_text(p1_hdr, h);

    if (r->total_rounds > 0) {
        int ang = (int)(360 * r->round / r->total_rounds);
        lv_arc_set_angles(p1_arc, 90 - ang, 90);
    }

    for (int i = 0; i < 3 && i < g_data.team_count; i++) {
        f1_team_t *t = &g_data.teams[i];
        uint32_t c = f1_team_color(t->name);
        lv_obj_set_style_bg_color(p1_bar[i], col(c), 0);
        int pts = atoi(t->points);
        int maxpts = g_data.team_count ? atoi(g_data.teams[0].points) : 1;
        int w = maxpts > 0 ? (int)(80 * pts / maxpts) : 12;
        if (w < 12) w = 12;
        lv_obj_set_width(p1_bar[i], w);
        char abbr[8];
        team_abbr(t->name, abbr, sizeof(abbr));
        lv_label_set_text(p1_barlbl[i], abbr);
    }

    p1_dirty = true;
    p1_session_update();
}

static void draw_page2(void)
{
    if (!g_has_data || g_data.driver_count == 0) return;

    f1_driver_t *lead = &g_data.drivers[0];
    uint32_t c = f1_team_color(lead->constructor);

    lv_label_set_text(p2_code, lead->code[0] ? lead->code : "--");
    lv_obj_set_style_text_color(p2_code, col(c), 0);
    lv_obj_set_style_border_color(p2_ring, col(c), 0);

    char s[32];
    snprintf(s, sizeof(s), "%s PTS", lead->points);
    lv_label_set_text(p2_pts, s);
    lv_obj_set_style_text_color(p2_pts, col(c), 0);
    lv_label_set_text(p2_top, lead->constructor);

    /* P2..P5 for the tap-to-expand list, each row in its team colour */
    int maxpts = atoi(lead->points);
    for (int i = 0; i < 4; i++) {
        int idx = i + 1;
        if (idx < g_data.driver_count) {
            f1_driver_t *d = &g_data.drivers[idx];
            snprintf(s, sizeof(s), "P%-2s %-3s %4s", d->position,
                     d->code[0] ? d->code : d->family_name, d->points);
            lv_label_set_text(p2_row[i], s);
            lv_obj_set_style_text_color(p2_row[i], col(f1_team_color(d->constructor)), 0);
        } else {
            lv_label_set_text(p2_row[i], "");
        }
    }

    for (int i = 0; i < 5; i++) {
        if (i < g_data.driver_count) {
            uint32_t cc = f1_team_color(g_data.drivers[i].constructor);
            lv_obj_set_style_arc_color(p2_arc[i], col(cc), LV_PART_INDICATOR);
            int pts = atoi(g_data.drivers[i].points);
            int span = maxpts > 0 ? (int)(300 * pts / maxpts) : 4;
            if (span < 4) span = 4;
            lv_arc_set_angles(p2_arc[i], 90 - span / 2, 90 + span / 2);
        }
    }
}

static void draw_page3(void)
{
    if (!g_has_data || g_data.team_count == 0) return;

    f1_team_t *lead = &g_data.teams[0];
    uint32_t c = f1_team_color(lead->name);

    char abbr[8];
    team_abbr(lead->name, abbr, sizeof(abbr));
    lv_label_set_text(p3_code, abbr);
    lv_obj_set_style_text_color(p3_code, col(c), 0);
    lv_obj_set_style_border_color(p3_ring, col(c), 0);

    char s[40];
    snprintf(s, sizeof(s), "%s PTS", lead->points);
    lv_label_set_text(p3_pts, s);
    lv_obj_set_style_text_color(p3_pts, col(c), 0);

    snprintf(s, sizeof(s), "%.22s", g_data.last_result.race_name[0] ?
             g_data.last_result.race_name : "LAST RACE ---");
    lv_label_set_text(p3_last1, s);
    snprintf(s, sizeof(s), "WIN %s   FAST %s",
             g_data.last_result.winner[0] ? g_data.last_result.winner : "---",
             g_data.last_result.fastest_lap[0] ? g_data.last_result.fastest_lap : "---");
    lv_label_set_text(p3_last2, s);

    int maxpts = atoi(lead->points);
    for (int i = 0; i < 4; i++) {
        int idx = i + 1;
        if (idx < g_data.team_count) {
            f1_team_t *t = &g_data.teams[idx];
            char a2[8];
            team_abbr(t->name, a2, sizeof(a2));
            snprintf(s, sizeof(s), "P%-2s %-3s %4s", t->position, a2, t->points);
            lv_label_set_text(p3_row[i], s);
            lv_obj_set_style_text_color(p3_row[i], col(f1_team_color(t->name)), 0);
        } else {
            lv_label_set_text(p3_row[i], "");
        }
    }

    for (int i = 0; i < 5; i++) {
        if (i < g_data.team_count) {
            uint32_t cc = f1_team_color(g_data.teams[i].name);
            lv_obj_set_style_arc_color(p3_arc[i], col(cc), LV_PART_INDICATOR);
            int pts = atoi(g_data.teams[i].points);
            int span = maxpts > 0 ? (int)(300 * pts / maxpts) : 4;
            if (span < 4) span = 4;
            lv_arc_set_angles(p3_arc[i], 90 - span / 2, 90 + span / 2);
        }
    }
}

/* ---------------- timers & input ---------------- */
static void tick_cb(lv_timer_t *t)
{
    (void)t;
    g_burn_phase = (g_burn_phase + 1) % 4;
    uint32_t now_ms = lv_tick_get();

    /* Anti-burn-in: nudge the season arc by +-1 degree every second */
    if (g_has_data && g_data.next_race.total_rounds > 0) {
        int ang = (int)(360 * g_data.next_race.round / g_data.next_race.total_rounds);
        int drift = (g_burn_phase == 0 || g_burn_phase == 2) ? 1 : 0;
        lv_arc_set_angles(p1_arc, 90 - ang + drift, 90 + drift);
    }

    draw_status_clock();
    draw_status_wifi();
    if (now_ms >= st_next_batt_ms) {
        draw_status_batt();
        st_next_batt_ms = now_ms + 30000;
    }

    if (status_time_valid()) p1_session_update();

    if (!g_screen_off && lv_display_get_inactive_time(NULL) > IDLE_OFF_MS) {
        bsp_display_brightness_set(BRIGHT_OFF);
        g_screen_off = true;
    }
}

static void wake_cb(lv_event_t *e)
{
    (void)e;
    if (g_screen_off) {
        bsp_display_brightness_set(BRIGHT_ON);
        g_screen_off = false;
    }
}

static void longpress_cb(lv_event_t *e)
{
    (void)e;
    if (g_refresh_cb) g_refresh_cb();
}

static void page_tap_cb(lv_event_t *e)
{
    lv_point_t p;
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_indev_get_point(indev, &p);

    int target = g_page_idx;
    if (p.x < 150) target = g_page_idx - 1;
    else if (p.x > 316) target = g_page_idx + 1;
    if (target < 0) target = 0;
    if (target > 2) target = 2;
    if (target != g_page_idx) page_show(target);
}

lv_obj_t *f1_ui_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, 466, 466);
    lv_obj_set_style_bg_color(scr, col(C_BG), 0);
    /* remove_style_all() also drops bg_opa, which left the display showing its
     * default white background. Set it back explicitly. */
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(scr, wake_cb, LV_EVENT_PRESSED, NULL);

    for (int i = 0; i < 3; i++) {
        g_page[i] = lv_obj_create(scr);
        lv_obj_remove_style_all(g_page[i]);
        lv_obj_set_size(g_page[i], 466, 466);
        lv_obj_center(g_page[i]);
        /* Opaque page background so partial redraws never blend with stale
         * pixels from the other draw buffer. */
        lv_obj_set_style_bg_color(g_page[i], col(C_BG), 0);
        lv_obj_set_style_bg_opa(g_page[i], LV_OPA_COVER, 0);
        lv_obj_remove_flag(g_page[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(g_page[i], wake_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(g_page[i], page_tap_cb, LV_EVENT_SHORT_CLICKED, NULL);
        lv_obj_add_event_cb(g_page[i], longpress_cb, LV_EVENT_LONG_PRESSED, NULL);
    }

    build_page1(g_page[0]);
    build_page2(g_page[1]);
    build_page3(g_page[2]);

    lv_obj_add_event_cb(p2_box, p2_center_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(p3_box, p3_center_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(p2_box, longpress_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(p3_box, longpress_cb, LV_EVENT_LONG_PRESSED, NULL);

    build_status(scr);
    build_dots(scr);
    draw_status_clock();
    draw_status_wifi();
    st_next_batt_ms = lv_tick_get() + 30000;
    page_show(0);

    lv_timer_create(tick_cb, 1000, NULL);

    /* a screen only becomes visible after it is loaded */
    lv_screen_load(scr);
    return scr;
}

void f1_ui_update(const f1_standings_t *data)
{
    /* Called from the fetch task while the LVGL task is drawing, so every LVGL
     * touch has to happen under the display lock. */
    bsp_display_lock(-1);
    memcpy(&g_data, data, sizeof(g_data));
    g_has_data = true;
    draw_page1();
    draw_page2();
    draw_page3();
    bsp_display_unlock();
}

void f1_ui_set_refresh_cb(void (*cb)(void))
{
    g_refresh_cb = cb;
}
