#ifndef SERVER_H
#define SERVER_H

#include <pthread.h>
#include <stdint.h>
#include "protocol.h"

typedef struct {
    int alarm_active;
    int led_state;
    int led_brightness;
    int buzzer_state;
    int sensor_blocked;
    int segment_value;
    int client_fd;
    pthread_mutex_t lock;
} SystemState;

typedef struct { int action; int value; int pending; } LedCmd;
typedef struct { int action;            int pending; } BuzzerCmd;

/* ── main.c 에서 정의된 전역 변수 ── */
extern SystemState      g_state;
extern char             g_tty_path[64];  /* 데몬화 전 터미널 경로 */

extern LedCmd           g_led_cmd;
extern pthread_mutex_t  g_led_mtx;
extern pthread_cond_t   g_led_cond;

extern BuzzerCmd        g_buzzer_cmd;
extern pthread_mutex_t  g_buzzer_mtx;
extern pthread_cond_t   g_buzzer_cond;

extern pthread_t        g_seg_tid;
extern int              g_seg_running;

/* ── .so 함수 포인터 (main.c 정의) ── */
extern int  (*fp_led_init)(int);
extern void (*fp_led_on)(void);
extern void (*fp_led_off)(void);
extern void (*fp_led_set_brightness)(int);
extern void (*fp_led_cleanup)(void);

extern int  (*fp_buzzer_init)(int);
extern void (*fp_buzzer_on)(void);
extern void (*fp_buzzer_off)(void);
extern void (*fp_buzzer_play_melody)(void);
extern void (*fp_buzzer_stop_melody)(void);
extern void (*fp_buzzer_cleanup)(void);

extern pthread_t g_melody_tid;
extern int       g_melody_running;

extern int  (*fp_sensor_init)(int);
extern int  (*fp_sensor_read)(void);
extern void (*fp_sensor_cleanup)(void);

extern int  (*fp_segment_init)(int *);
extern void (*fp_segment_display)(int);
extern void (*fp_segment_cleanup)(void);

/* ── main.c 에서 정의된 함수 ── */
void dispatch_led(int action, int value);
void dispatch_buzzer(int action);
void log_event(const char *msg);

/* ── handler.c 에서 정의된 스레드 함수 ── */
void *led_thread_fn(void *arg);
void *buzzer_thread_fn(void *arg);
void *sensor_thread_fn(void *arg);
void *segment_thread_fn(void *arg);
void *handle_client(void *arg);

/* ── daemon.c ── */
void daemonize(void);

#endif /* SERVER_H */
