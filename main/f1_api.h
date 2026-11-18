#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define F1_MAX_DRIVERS   24
#define F1_MAX_TEAMS     12
#define F1_NAME_LEN      40
#define F1_MAX_SESSIONS  8

typedef struct {
    char position[8];
    char points[12];
    char given_name[F1_NAME_LEN];
    char family_name[F1_NAME_LEN];
    char code[4];
    char constructor[F1_NAME_LEN];
    char nationality[F1_NAME_LEN];
} f1_driver_t;

typedef struct {
    char position[8];
    char points[12];
    char name[F1_NAME_LEN];
    char nationality[F1_NAME_LEN];
} f1_team_t;

/* One practice / qualifying / sprint / race session of the next GP weekend */
typedef struct {
    char name[8];      /* FP1 FP2 FP3 SQ SPR Q RACE */
    time_t start_ts;   /* UTC */
} f1_session_t;

/* Next race + season progress (page 1 main dial) */
typedef struct {
    char code[8];        /* circuit code e.g. SUZ / MON, derived from circuitId */
    char name[F1_NAME_LEN];
    int round;           /* e.g. 21 */
    int total_rounds;    /* e.g. 23 */
    time_t start_ts;     /* race start epoch (UTC), 0 if unknown */
    char tz[48];         /* POSIX TZ string of the circuit, e.g. PST8PDT,... */
    f1_session_t sessions[F1_MAX_SESSIONS];  /* sorted by start_ts */
    int session_count;
} f1_nextrace_t;

/* Last race result (page 3) */
typedef struct {
    char race_name[F1_NAME_LEN];
    char winner[8];      /* driver code e.g. VER */
    char fastest_lap[8]; /* driver code e.g. GAS */
} f1_lastresult_t;

typedef struct {
    f1_driver_t drivers[F1_MAX_DRIVERS];
    int driver_count;
    f1_team_t teams[F1_MAX_TEAMS];
    int team_count;
    char season[8];
    f1_nextrace_t next_race;
    f1_lastresult_t last_result;
    time_t synced_at;    /* last successful fetch time */
    bool valid;
} f1_standings_t;

typedef enum {
    F1_FETCH_OK = 0,
    F1_FETCH_WIFI_ERROR,
    F1_FETCH_HTTP_ERROR,
    F1_FETCH_PARSE_ERROR,
} f1_fetch_result_t;

void f1_wifi_init(void);
bool f1_wifi_is_connected(void);
void f1_wifi_kick(void);

/**
 * Fetch current-season data from Jolpica API: driver standings,
 * constructor standings, race calendar (next race + progress), last result.
 */
f1_fetch_result_t f1_fetch_standings(f1_standings_t *out);

/* Constructor name -> brand color (0xRRGGBB) for UI accents. */
uint32_t f1_team_color(const char *constructor_name);
