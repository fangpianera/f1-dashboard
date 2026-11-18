#include "f1_api.h"

#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

static const char *TAG = "f1_api";

/* cJSON builds a node per JSON token: parsing a 20 KB standings payload costs
 * tens of KB of heap. If that comes from internal RAM the LCD's SPI DMA pool
 * is starved and LVGL starts failing every flush, so put JSON nodes in PSRAM. */
static void *json_malloc(size_t size)
{
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(size);
}

static void json_free(void *p)
{
    heap_caps_free(p);
}

static void json_hooks_init(void)
{
    static bool done = false;
    if (done) return;
    done = true;
    cJSON_Hooks hooks = {.malloc_fn = json_malloc, .free_fn = json_free};
    cJSON_InitHooks(&hooks);
}

#define WIFI_SSID_MAX   32
#define WIFI_PASS_MAX   64
#define NVS_NAMESPACE   "f1dash"
#define NVS_KEY_SSID    "wifi_ssid"
#define NVS_KEY_PASS    "wifi_pass"

#define WIFI_CONNECTED_BIT  BIT0

static EventGroupHandle_t s_wifi_events;
static bool s_connected = false;

/* ---- NVS credentials ---- */
static bool load_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    size_t s = ssid_len, p = pass_len;
    esp_err_t e1 = nvs_get_str(h, NVS_KEY_SSID, ssid, &s);
    esp_err_t e2 = nvs_get_str(h, NVS_KEY_PASS, pass, &p);
    nvs_close(h);
    return (e1 == ESP_OK && e2 == ESP_OK && s > 1);
}

static void save_credentials(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, NVS_KEY_SSID, ssid);
    nvs_set_str(h, NVS_KEY_PASS, pass);
    nvs_commit(h);
    nvs_close(h);
}

/* ---- WiFi event handler ---- */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_connected = true;
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "WiFi connected, got IP");
    }
}

/* ---- Captive portal (soft AP + HTTP form) ---- */
static const char *PORTAL_HTML =
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>F1 Dash WiFi</title><style>"
    "body{font-family:sans-serif;background:#111;color:#eee;padding:24px}"
    "input,button{font-size:16px;padding:10px;margin:6px 0;width:100%;box-sizing:border-box}"
    "button{background:#e10600;color:#fff;border:0;border-radius:6px}"
    "</style></head><body><h2>F1 Dashboard WiFi Setup</h2>"
    "<form method='post' action='/save'>"
    "WiFi SSID:<input name='ssid' required>"
    "Password:<input name='pass' type='password'>"
    "<button type='submit'>Connect & Save</button></form></body></html>";

static esp_err_t portal_root_handler(httpd_req_t *req)
{
    httpd_resp_send(req, PORTAL_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static void reboot_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

static esp_err_t portal_save_handler(httpd_req_t *req)
{
    char buf[512];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) { httpd_resp_send_500(req); return ESP_FAIL; }
    buf[len] = 0;

    char ssid[WIFI_SSID_MAX] = {0}, pass[WIFI_PASS_MAX] = {0};
    char *k = strstr(buf, "ssid=");
    if (k) {
        k += 5;
        char *e = strchr(k, '&');
        size_t n = e ? (size_t)(e - k) : strlen(k);
        if (n >= sizeof(ssid)) n = sizeof(ssid) - 1;
        memcpy(ssid, k, n);
    }
    k = strstr(buf, "pass=");
    if (k) {
        k += 5;
        char *e = strchr(k, '&');
        size_t n = e ? (size_t)(e - k) : strlen(k);
        if (n >= sizeof(pass)) n = sizeof(pass) - 1;
        memcpy(pass, k, n);
    }

    if (ssid[0]) {
        save_credentials(ssid, pass);
        httpd_resp_sendstr(req, "<h2>Saved! Rebooting...</h2>");
        ESP_LOGI(TAG, "Portal got creds: %s", ssid);
        xTaskCreate(reboot_task, "reboot", 2048, NULL, 5, NULL);
    } else {
        httpd_resp_sendstr(req, "<h2>SSID empty</h2><a href='/'>back</a>");
    }
    return ESP_OK;
}

static void start_portal(void)
{
    ESP_LOGI(TAG, "Starting WiFi setup AP: F1-DASH");
    esp_netif_create_default_wifi_ap();

    wifi_config_t ap = {0};
    strcpy((char *)ap.ap.ssid, "F1-DASH");
    ap.ap.ssid_len = strlen("F1-DASH");
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN;
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    /* CRITICAL: start the WiFi driver. Without this the AP never broadcasts.
     * The saved-credentials branch calls esp_wifi_start(), but this fresh-boot
     * path did not — that is why "F1-DASH" never appeared. */
    ESP_ERROR_CHECK(esp_wifi_start());

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    httpd_start(&server, &cfg);
    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = portal_root_handler };
    httpd_uri_t save = { .uri = "/save", .method = HTTP_POST, .handler = portal_save_handler };
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &save);

    /* Broadcast a DNS server that resolves all queries to the AP (captive portal) */
    /* Simplified: user can open 192.168.4.1 manually; full DNS redirect omitted. */
    ESP_LOGI(TAG, "Open http://192.168.4.1 to configure WiFi");
}

void f1_wifi_init(void)
{
    s_wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                    &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                    &wifi_event_handler, NULL, NULL));

    char ssid[WIFI_SSID_MAX] = {0}, pass[WIFI_PASS_MAX] = {0};
    if (load_credentials(ssid, sizeof(ssid), pass, sizeof(pass))) {
        wifi_config_t sta = {0};
        snprintf((char *)sta.sta.ssid, sizeof(sta.sta.ssid), "%s", ssid);
        snprintf((char *)sta.sta.password, sizeof(sta.sta.password), "%s", pass);
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_set_config(WIFI_IF_STA, &sta);
        esp_wifi_start();
        esp_wifi_connect();
        EventBits_t bits = xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT,
                                               pdFALSE, pdTRUE, pdMS_TO_TICKS(15000));
        if (bits & WIFI_CONNECTED_BIT) return;
        ESP_LOGW(TAG, "Saved WiFi failed to connect, launching portal");
    }
    start_portal();
}

bool f1_wifi_is_connected(void)
{
    return s_connected;
}

/* Ask the STA to (re)associate. Used by the fetch task so a boot that came up
 * before the AP was ready - or a dropped link - recovers without a reboot. */
void f1_wifi_kick(void)
{
    if (!s_connected) {
        esp_wifi_connect();
    }
}

/* ---- Jolpica F1 API ---- */
static size_t fetch_url(const char *url, char *buf, size_t bufsize)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) { esp_http_client_cleanup(client); return 0; }
    int len = esp_http_client_fetch_headers(client);
    if (len < 0) { esp_http_client_cleanup(client); return 0; }
    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGW(TAG, "HTTP status %d for %s", status, url);
        esp_http_client_cleanup(client); return 0;
    }
    size_t total = esp_http_client_read(client, buf, bufsize - 1);
    buf[total] = 0;
    esp_http_client_cleanup(client);
    return total;
}

static void parse_standings(const char *json, f1_standings_t *out)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return;
    cJSON *mr = cJSON_GetObjectItem(root, "MRData");
    if (!mr) { cJSON_Delete(root); return; }
    cJSON *table = cJSON_GetObjectItem(mr, "StandingsTable");
    if (table) {
        cJSON *season = cJSON_GetObjectItem(table, "season");
        if (season && season->valuestring)
            snprintf(out->season, sizeof(out->season), "%s", season->valuestring);
    }

    /* Driver standings: MRData.StandingsTable.StandingsLists[0].DriverStandings[] */
    cJSON *lists = table ? cJSON_GetObjectItem(table, "StandingsLists") : NULL;
    cJSON *list0 = (lists && cJSON_IsArray(lists)) ? cJSON_GetArrayItem(lists, 0) : NULL;
    cJSON *ds = list0 ? cJSON_GetObjectItem(list0, "DriverStandings") : NULL;
    if (ds && cJSON_IsArray(ds)) {
        int n = cJSON_GetArraySize(ds);
        if (n > F1_MAX_DRIVERS) n = F1_MAX_DRIVERS;
        for (int i = 0; i < n; i++) {
            cJSON *it = cJSON_GetArrayItem(ds, i);
            f1_driver_t *d = &out->drivers[i];
            cJSON *v;
            if ((v = cJSON_GetObjectItem(it, "position")) && v->valuestring)
                snprintf(d->position, sizeof(d->position), "%s", v->valuestring);
            if ((v = cJSON_GetObjectItem(it, "points")) && v->valuestring)
                snprintf(d->points, sizeof(d->points), "%s", v->valuestring);
            cJSON *drv = cJSON_GetObjectItem(it, "Driver");
            if (drv) {
                if ((v = cJSON_GetObjectItem(drv, "givenName")) && v->valuestring)
                    snprintf(d->given_name, sizeof(d->given_name), "%s", v->valuestring);
                if ((v = cJSON_GetObjectItem(drv, "familyName")) && v->valuestring)
                    snprintf(d->family_name, sizeof(d->family_name), "%s", v->valuestring);
                if ((v = cJSON_GetObjectItem(drv, "code")) && v->valuestring)
                    snprintf(d->code, sizeof(d->code), "%s", v->valuestring);
                if ((v = cJSON_GetObjectItem(drv, "nationality")) && v->valuestring)
                    snprintf(d->nationality, sizeof(d->nationality), "%s", v->valuestring);
            }
            cJSON *cts = cJSON_GetObjectItem(it, "Constructors");
            if (cts && cJSON_IsArray(cts)) {
                cJSON *c0 = cJSON_GetArrayItem(cts, 0);
                cJSON *nm = c0 ? cJSON_GetObjectItem(c0, "name") : NULL;
                if (nm && nm->valuestring)
                    snprintf(d->constructor, sizeof(d->constructor), "%s", nm->valuestring);
            }
        }
        out->driver_count = n;
    }

    /* Constructor standings fetched separately into a second call; see f1_fetch_standings */
    cJSON_Delete(root);
}

static void parse_constructors(const char *json, f1_standings_t *out)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return;
    cJSON *table = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "MRData"), "StandingsTable");
    cJSON *lists = table ? cJSON_GetObjectItem(table, "StandingsLists") : NULL;
    cJSON *list0 = (lists && cJSON_IsArray(lists)) ? cJSON_GetArrayItem(lists, 0) : NULL;
    cJSON *cs = list0 ? cJSON_GetObjectItem(list0, "ConstructorStandings") : NULL;
    if (cs && cJSON_IsArray(cs)) {
        int n = cJSON_GetArraySize(cs);
        if (n > F1_MAX_TEAMS) n = F1_MAX_TEAMS;
        for (int i = 0; i < n; i++) {
            cJSON *it = cJSON_GetArrayItem(cs, i);
            f1_team_t *t = &out->teams[i];
            cJSON *v;
            if ((v = cJSON_GetObjectItem(it, "position")) && v->valuestring)
                snprintf(t->position, sizeof(t->position), "%s", v->valuestring);
            if ((v = cJSON_GetObjectItem(it, "points")) && v->valuestring)
                snprintf(t->points, sizeof(t->points), "%s", v->valuestring);
            cJSON *c = cJSON_GetObjectItem(it, "Constructor");
            if (c) {
                if ((v = cJSON_GetObjectItem(c, "name")) && v->valuestring)
                    snprintf(t->name, sizeof(t->name), "%s", v->valuestring);
                if ((v = cJSON_GetObjectItem(c, "nationality")) && v->valuestring)
                    snprintf(t->nationality, sizeof(t->nationality), "%s", v->valuestring);
            }
        }
        out->team_count = n;
    }
    cJSON_Delete(root);
}

/* circuitId -> 3-letter display code (F1 official style). */
static void circuit_code(const char *id, char *out, size_t n)
{
    static const struct { const char *id; const char *code; } MAP[] = {
        {"albert_park", "ALB"}, {"shanghai", "SHA"}, {"suzuka", "SUZ"},
        {"bahrain", "BAH"}, {"jeddah", "JED"}, {"miami", "MIA"},
        {"monaco", "MON"}, {"catalunya", "CAT"}, {"spain", "MAD"},
        {"silverstone", "SIL"}, {"spa", "SPA"}, {"hungaroring", "HUN"},
        {"zandvoort", "ZAN"}, {"monza", "MON"}, {"baku", "BAK"},
        {"marina_bay", "SGP"}, {"americas", "COT"}, {"rodriguez", "MEX"},
        {"interlagos", "SAO"}, {"vegas", "VGS"}, {"losail", "LOS"},
        {"yas_marina", "YAS"}, {"red_bull_ring", "RBR"}, {"imola", "IMO"},
    };
    for (size_t i = 0; i < sizeof(MAP)/sizeof(MAP[0]); i++) {
        if (strcasecmp(id, MAP[i].id) == 0) {
            snprintf(out, n, "%s", MAP[i].code);
            return;
        }
    }
    /* Fallback: first 3 chars uppercase */
    snprintf(out, n, "%.3s", id ? id : "GP");
    for (char *p = out; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
}

/* Parse ISO-8601 "2026-11-22T04:00:00Z" to epoch (UTC, TZ independent).
 * mktime() would apply the local timezone (we run at CST-8), so convert
 * civil date -> days since epoch by hand instead. */
static time_t parse_iso8601(const char *s)
{
    int Y, M, D, h, m, sec;
    if (!s || sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &Y, &M, &D, &h, &m, &sec) != 6)
        return 0;
    int y = Y - (M <= 2 ? 1 : 0);
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (M + (M > 2 ? -3 : 9)) + 2) / 5 + D - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = (long)era * 146097 + (long)doe - 719468;
    return (time_t)days * 86400 + h * 3600 + m * 60 + sec;
}

/* circuitId -> POSIX TZ string (newlib parses these directly, DST included). */
static const char *circuit_tz(const char *id)
{
    static const struct { const char *id; const char *tz; } MAP[] = {
        {"albert_park", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
        {"shanghai",    "CST-8"},
        {"suzuka",      "JST-9"},
        {"bahrain",     "<-03>3"},
        {"jeddah",      "<-03>3"},
        {"miami",       "EST5EDT,M3.2.0,M11.1.0"},
        {"monaco",      "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"catalunya",   "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"madrid",      "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"silverstone", "GMT0BST,M3.5.0/1,M10.5.0"},
        {"spa",         "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"hungaroring", "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"zandvoort",   "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"monza",       "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"imola",       "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"red_bull_ring","CET-1CEST,M3.5.0,M10.5.0/3"},
        {"baku",        "<-04>4"},
        {"marina_bay",  "<-08>8"},
        {"americas",    "CST6CDT,M3.2.0,M11.1.0"},
        {"rodriguez",   "CST6"},
        {"interlagos",  "<-03>3"},
        {"vegas",       "PST8PDT,M3.2.0,M11.1.0"},
        {"losail",      "<-03>3"},
        {"yas_marina",  "<-04>4"},
    };
    for (size_t i = 0; i < sizeof(MAP) / sizeof(MAP[0]); i++)
        if (strcasecmp(id, MAP[i].id) == 0) return MAP[i].tz;
    return "UTC0";
}

static void session_push(f1_nextrace_t *nr, const char *label, time_t ts)
{
    if (ts <= 0) return;
    for (int i = 0; i < nr->session_count; i++)
        if (nr->sessions[i].start_ts == ts) return;   /* dedupe SQ aliases */
    if (nr->session_count >= F1_MAX_SESSIONS) return;
    f1_session_t *s = &nr->sessions[nr->session_count++];
    snprintf(s->name, sizeof(s->name), "%s", label);
    s->start_ts = ts;
}

static void session_push_obj(f1_nextrace_t *nr, cJSON *race, const char *key, const char *label)
{
    cJSON *o = cJSON_GetObjectItem(race, key);
    if (!o) return;
    cJSON *d = cJSON_GetObjectItem(o, "date");
    cJSON *t = cJSON_GetObjectItem(o, "time");
    if (!d || !d->valuestring) return;
    char iso[40];
    snprintf(iso, sizeof(iso), "%sT%s", d->valuestring,
             (t && t->valuestring) ? t->valuestring : "00:00:00Z");
    session_push(nr, label, parse_iso8601(iso));
}

/* Parse race calendar: pick next future race + total count. */
static void parse_calendar(const char *json, f1_standings_t *out)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return;
    cJSON *races = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "MRData"), "RaceTable");
    races = races ? cJSON_GetObjectItem(races, "Races") : NULL;
    if (!races || !cJSON_IsArray(races)) { cJSON_Delete(root); return; }
    int total = cJSON_GetArraySize(races);
    out->next_race.total_rounds = total;

    time_t now = time(NULL);
    for (int i = 0; i < total; i++) {
        cJSON *r = cJSON_GetArrayItem(races, i);
        cJSON *date = cJSON_GetObjectItem(r, "date");
        cJSON *timev = cJSON_GetObjectItem(r, "time");
        if (!date || !date->valuestring) continue;
        char isotime[40];
        snprintf(isotime, sizeof(isotime), "%sT%s", date->valuestring,
                 (timev && timev->valuestring) ? timev->valuestring : "00:00:00Z");
        time_t start = parse_iso8601(isotime);
        if (start > now) {
            cJSON *rnd = cJSON_GetObjectItem(r, "round");
            cJSON *name = cJSON_GetObjectItem(r, "raceName");
            cJSON *circ = cJSON_GetObjectItem(r, "Circuit");
            out->next_race.round = rnd ? atoi(rnd->valuestring) : (i + 1);
            if (name && name->valuestring)
                snprintf(out->next_race.name, sizeof(out->next_race.name), "%s", name->valuestring);
            if (circ) {
                cJSON *cid = cJSON_GetObjectItem(circ, "circuitId");
                const char *cid_s = cid ? cid->valuestring : "";
                circuit_code(cid_s, out->next_race.code, sizeof(out->next_race.code));
                snprintf(out->next_race.tz, sizeof(out->next_race.tz), "%s", circuit_tz(cid_s));
            }
            out->next_race.start_ts = start;

            /* Whole weekend, so the dial can count down to the next session. */
            session_push_obj(&out->next_race, r, "FirstPractice", "FP1");
            session_push_obj(&out->next_race, r, "SecondPractice", "FP2");
            session_push_obj(&out->next_race, r, "ThirdPractice", "FP3");
            session_push_obj(&out->next_race, r, "SprintQualifying", "SQ");
            session_push_obj(&out->next_race, r, "SprintShootout", "SQ");
            session_push_obj(&out->next_race, r, "Sprint", "SPR");
            session_push_obj(&out->next_race, r, "Qualifying", "Q");
            session_push(&out->next_race, "RACE", start);

            /* insertion sort by start time */
            f1_nextrace_t *nr = &out->next_race;
            for (int k = 1; k < nr->session_count; k++) {
                f1_session_t key = nr->sessions[k];
                int j = k - 1;
                while (j >= 0 && nr->sessions[j].start_ts > key.start_ts) {
                    nr->sessions[j + 1] = nr->sessions[j];
                    j--;
                }
                nr->sessions[j + 1] = key;
            }
            break;
        }
    }
    cJSON_Delete(root);
}

/* Parse last race results: winner + fastest lap. */
static void parse_last_result(const char *json, f1_standings_t *out)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return;
    cJSON *rt = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "MRData"), "RaceTable");
    cJSON *races = rt ? cJSON_GetObjectItem(rt, "Races") : NULL;
    cJSON *race = (races && cJSON_IsArray(races)) ? cJSON_GetArrayItem(races, 0) : NULL;
    if (!race) { cJSON_Delete(root); return; }
    cJSON *nm = cJSON_GetObjectItem(race, "raceName");
    if (nm && nm->valuestring)
        snprintf(out->last_result.race_name, sizeof(out->last_result.race_name), "%s", nm->valuestring);
    cJSON *results = cJSON_GetObjectItem(race, "Results");
    if (results && cJSON_IsArray(results)) {
        for (int i = 0; i < cJSON_GetArraySize(results); i++) {
            cJSON *it = cJSON_GetArrayItem(results, i);
            cJSON *pos = cJSON_GetObjectItem(it, "position");
            cJSON *drv = cJSON_GetObjectItem(it, "Driver");
            cJSON *code = drv ? cJSON_GetObjectItem(drv, "code") : NULL;
            if (pos && pos->valuestring && strcmp(pos->valuestring, "1") == 0 && code && code->valuestring)
                snprintf(out->last_result.winner, sizeof(out->last_result.winner), "%s", code->valuestring);
            cJSON *fl = cJSON_GetObjectItem(it, "FastestLap");
            cJSON *rank = fl ? cJSON_GetObjectItem(fl, "rank") : NULL;
            if (rank && rank->valuestring && strcmp(rank->valuestring, "1") == 0 && code && code->valuestring)
                snprintf(out->last_result.fastest_lap, sizeof(out->last_result.fastest_lap), "%s", code->valuestring);
        }
    }
    cJSON_Delete(root);
}

uint32_t f1_team_color(const char *name)
{
    if (!name) return 0x9AA0A6;
    struct { const char *key; uint32_t color; } C[] = {
        {"McLaren", 0xFF8700}, {"Red Bull", 0x3671C6}, {"Ferrari", 0xE8002D},
        {"Mercedes", 0x27F4D2}, {"Aston Martin", 0x229971}, {"Alpine", 0xFF87BC},
        {"Williams", 0x64C4FF}, {"Racing Bulls", 0x6692FF}, {"Haas", 0xB6BABD},
        {"Kick Sauber", 0x52E252}, {"Sauber", 0x52E252},
    };
    for (size_t i = 0; i < sizeof(C)/sizeof(C[0]); i++)
        if (strstr(name, C[i].key)) return C[i].color;
    return 0x9AA0A6;
}

/* Each block is fetched separately and one failed request must not wipe the
 * data we already have on screen. Keep a scratch copy of the standings. */
static f1_driver_t s_drv_bak[F1_MAX_DRIVERS];
static f1_team_t   s_team_bak[F1_MAX_TEAMS];

f1_fetch_result_t f1_fetch_standings(f1_standings_t *out)
{
    json_hooks_init();
    if (!s_connected) return F1_FETCH_WIFI_ERROR;

    /* Allocate from PSRAM: a 24KB static buffer in internal DRAM starves the
     * LCD's transient DMA allocations and causes a white screen. */
    const size_t buf_size = 24576;
    char *buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = malloc(buf_size);
    if (!buf) return F1_FETCH_HTTP_ERROR;

    /* ---- drivers (required) ---- */
    size_t n = fetch_url("https://api.jolpi.ca/ergast/f1/current/driverStandings.json?limit=30",
                         buf, buf_size);
    if (n == 0) {
        free(buf);
        return out->driver_count ? F1_FETCH_OK : F1_FETCH_HTTP_ERROR;
    }
    memcpy(s_drv_bak, out->drivers, sizeof(s_drv_bak));
    int dc_bak = out->driver_count;
    char season_bak[8];
    memcpy(season_bak, out->season, sizeof(season_bak));

    out->driver_count = 0;
    parse_standings(buf, out);
    if (out->driver_count == 0) {
        memcpy(out->drivers, s_drv_bak, sizeof(s_drv_bak));
        memcpy(out->season, season_bak, sizeof(season_bak));
        out->driver_count = dc_bak;
        if (!out->driver_count) { free(buf); return F1_FETCH_PARSE_ERROR; }
    }

    /* ---- constructors ---- */
    n = fetch_url("https://api.jolpi.ca/ergast/f1/current/constructorStandings.json?limit=20",
                  buf, buf_size);
    if (n > 0) {
        memcpy(s_team_bak, out->teams, sizeof(s_team_bak));
        int tc_bak = out->team_count;
        out->team_count = 0;
        parse_constructors(buf, out);
        if (out->team_count == 0) {
            memcpy(out->teams, s_team_bak, sizeof(s_team_bak));
            out->team_count = tc_bak;
        }
    }

    /* ---- calendar: next race + weekend sessions ---- */
    n = fetch_url("https://api.jolpi.ca/ergast/f1/current.json?limit=100", buf, buf_size);
    if (n > 0) {
        f1_nextrace_t save = out->next_race;
        memset(&out->next_race, 0, sizeof(out->next_race));
        parse_calendar(buf, out);
        if (out->next_race.round == 0 && out->next_race.session_count == 0)
            out->next_race = save;
    }

    /* ---- last race result ---- */
    /* limit=20 so the fastest-lap car is actually in the payload (the FL
     * holder is rarely P1 or P2). */
    n = fetch_url("https://api.jolpi.ca/ergast/f1/current/last/results.json?limit=20", buf, buf_size);
    if (n > 0) {
        f1_lastresult_t save = out->last_result;
        memset(&out->last_result, 0, sizeof(out->last_result));
        parse_last_result(buf, out);
        if (!out->last_result.race_name[0])
            out->last_result = save;
    }

    free(buf);
    out->synced_at = time(NULL);
    out->valid = true;
    return F1_FETCH_OK;
}
