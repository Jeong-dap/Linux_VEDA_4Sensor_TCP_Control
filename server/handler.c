#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include "protocol.h"
#include "server.h"

/* ════════════════════════════════════════
   LED 전담 스레드
   ════════════════════════════════════════ */
void *led_thread_fn(void *arg) {
    (void)arg;
    while (1) {
        pthread_mutex_lock(&g_led_mtx);
        while (!g_led_cmd.pending)
            pthread_cond_wait(&g_led_cond, &g_led_mtx);
        int action = g_led_cmd.action;
        int value  = g_led_cmd.value;
        g_led_cmd.pending = 0;
        pthread_mutex_unlock(&g_led_mtx);

        switch (action) {
            case ACT_ON:             fp_led_on();                    break;
            case ACT_OFF:            fp_led_off();                   break;
            case ACT_SET_BRIGHTNESS: fp_led_set_brightness(value);   break;
        }
    }
    return NULL;
}

/* ════════════════════════════════════════
   부저 전담 스레드
   ════════════════════════════════════════ */
void *buzzer_thread_fn(void *arg) {
    (void)arg;
    while (1) {
        pthread_mutex_lock(&g_buzzer_mtx);
        while (!g_buzzer_cmd.pending)
            pthread_cond_wait(&g_buzzer_cond, &g_buzzer_mtx);
        int action = g_buzzer_cmd.action;
        g_buzzer_cmd.pending = 0;
        pthread_mutex_unlock(&g_buzzer_mtx);

        if (action == ACT_ON)  fp_buzzer_on();
        else                   fp_buzzer_off();
    }
    return NULL;
}

/* ════════════════════════════════════════
   멜로디 스레드 (블로킹 재생, pthread_cancel 가능)
   ════════════════════════════════════════ */
static void melody_cleanup(void *arg) {
    (void)arg;
    fp_buzzer_stop_melody();
    pthread_mutex_lock(&g_state.lock);
    g_melody_running = 0;
    pthread_mutex_unlock(&g_state.lock);
}

void *melody_thread_fn(void *arg) {
    (void)arg;
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    pthread_cleanup_push(melody_cleanup, NULL);

    fp_buzzer_play_melody();  /* usleep 루프 → 취소 포인트 있음 */

    pthread_cleanup_pop(0);
    pthread_mutex_lock(&g_state.lock);
    g_melody_running = 0;
    pthread_mutex_unlock(&g_state.lock);
    return NULL;
}

/* 진행 중인 멜로디 스레드 취소 후 join */
static void stop_melody_thread(void) {
    pthread_mutex_lock(&g_state.lock);
    pthread_t tid  = g_melody_tid;
    int       alive = g_melody_running;
    pthread_mutex_unlock(&g_state.lock);

    if (alive) {
        fp_buzzer_stop_melody();
        pthread_cancel(tid);
        pthread_join(tid, NULL);
    }
}

/* ════════════════════════════════════════
   센서 감시 스레드 — 100ms 폴링, 디바운싱 3회
   ════════════════════════════════════════ */
void *sensor_thread_fn(void *arg) {
    (void)arg;
    struct timespec ts = {0, 100 * 1000 * 1000};  /* 100ms */
    int consec = 0;

    while (1) {
        int blocked = fp_sensor_read();

        if (blocked) {
            consec++;
        } else {
            consec = 0;
            /* 센서 정상 복귀 시 sensor_blocked 초기화 */
            pthread_mutex_lock(&g_state.lock);
            g_state.sensor_blocked = 0;
            pthread_mutex_unlock(&g_state.lock);
        }

        if (consec >= 3) {
            consec = 0;
            pthread_mutex_lock(&g_state.lock);
            int already = g_state.alarm_active;
            if (!already) {
                g_state.alarm_active   = 1;
                g_state.sensor_blocked = 1;
                g_state.led_state      = 1;
                g_state.led_brightness = BRIGHTNESS_HIGH;
                g_state.buzzer_state   = 1;
            }
            int cfd = g_state.client_fd;
            pthread_mutex_unlock(&g_state.lock);

            if (!already) {
                dispatch_led(ACT_SET_BRIGHTNESS, BRIGHTNESS_HIGH);
                dispatch_buzzer(ACT_ON);

                if (cfd >= 0) {
                    Message e1 = {MSG_EVENT, DEV_SENSOR, EVT_INTRUSION, 0};
                    Message e2 = {MSG_EVENT, DEV_LED,    EVT_ALARM_ON,  0};
                    send(cfd, &e1, sizeof(e1), 0);
                    send(cfd, &e2, sizeof(e2), 0);
                }
                log_event("INTRUSION DETECTED");
                log_event("ALARM ON (LED HIGH + BUZZER)");
            }
        }

        nanosleep(&ts, NULL);
    }
    return NULL;
}

/* ════════════════════════════════════════
   세그먼트 카운트다운 스레드 (pthread_cancel 가능)
   ════════════════════════════════════════ */
static void seg_cleanup(void *arg) {
    (void)arg;
    fp_segment_display(-1);
    pthread_mutex_lock(&g_state.lock);
    g_seg_running      = 0;
    g_state.segment_value = -1;
    pthread_mutex_unlock(&g_state.lock);
}

void *segment_thread_fn(void *arg) {
    int count = (int)(intptr_t)arg;

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    pthread_cleanup_push(seg_cleanup, NULL);

    for (; count >= 0; count--) {
        fp_segment_display(count);

        pthread_mutex_lock(&g_state.lock);
        g_state.segment_value = count;
        int cfd = g_state.client_fd;
        pthread_mutex_unlock(&g_state.lock);

        /* 매초 EVT_COUNTDOWN 전송 */
        if (cfd >= 0) {
            Message evt = {MSG_EVENT, DEV_SEGMENT, EVT_COUNTDOWN, (uint8_t)count};
            send(cfd, &evt, sizeof(evt), 0);
        }

        if (count == 0) {
            /* 카운트다운 완료 → 부저 ON, 이벤트 전송 */
            pthread_mutex_lock(&g_state.lock);
            g_state.buzzer_state = 1;
            cfd = g_state.client_fd;
            pthread_mutex_unlock(&g_state.lock);

            dispatch_buzzer(ACT_ON);

            if (cfd >= 0) {
                Message evt = {MSG_EVENT, DEV_BUZZER, EVT_ALARM_TRIGGERED, 0};
                send(cfd, &evt, sizeof(evt), 0);
            }
            log_event("COUNTDOWN REACHED 0 - BUZZER TRIGGERED");

            sleep(1);  /* 1초 후 세그먼트 OFF */
            fp_segment_display(-1);
            pthread_mutex_lock(&g_state.lock);
            g_state.segment_value = -1;
            pthread_mutex_unlock(&g_state.lock);

            sleep(2);  /* 2초 더 후 부저 OFF (총 3초) */

            dispatch_buzzer(ACT_OFF);
            pthread_mutex_lock(&g_state.lock);
            g_state.buzzer_state = 0;
            pthread_mutex_unlock(&g_state.lock);
            log_event("BUZZER AUTO OFF (3s)");
            break;
        }
        sleep(1);  /* 취소 포인트 */
    }

    /* 정상 완료: cleanup 핸들러 제거만 (실행 안 함) */
    pthread_cleanup_pop(0);

    pthread_mutex_lock(&g_state.lock);
    g_seg_running = 0;
    pthread_mutex_unlock(&g_state.lock);

    return NULL;
}

/* ════════════════════════════════════════
   클라이언트 핸들러 스레드
   ════════════════════════════════════════ */
void *handle_client(void *arg) {
    int cfd = (int)(intptr_t)arg;
    Message msg, resp;

    while (recv(cfd, &msg, sizeof(msg), 0) > 0) {

        switch (msg.type) {

        /* ── MSG_CMD ── */
        case MSG_CMD:
            switch (msg.device) {

            case DEV_LED: {
                switch (msg.action) {
                case ACT_ON:
                    pthread_mutex_lock(&g_state.lock);
                    g_state.led_state = 1;
                    pthread_mutex_unlock(&g_state.lock);
                    dispatch_led(ACT_ON, 0);
                    resp = (Message){MSG_RESP, DEV_LED, ACT_ON, RESP_OK};
                    break;
                case ACT_OFF:
                    pthread_mutex_lock(&g_state.lock);
                    g_state.led_state = 0;
                    pthread_mutex_unlock(&g_state.lock);
                    dispatch_led(ACT_OFF, 0);
                    resp = (Message){MSG_RESP, DEV_LED, ACT_OFF, RESP_OK};
                    break;
                case ACT_SET_BRIGHTNESS:
                    pthread_mutex_lock(&g_state.lock);
                    g_state.led_state      = 1;
                    g_state.led_brightness = msg.value;
                    pthread_mutex_unlock(&g_state.lock);
                    dispatch_led(ACT_SET_BRIGHTNESS, msg.value);
                    resp = (Message){MSG_RESP, DEV_LED, ACT_SET_BRIGHTNESS, RESP_OK};
                    break;
                default:
                    resp = (Message){MSG_RESP, DEV_LED, msg.action, RESP_ERR};
                }
                send(cfd, &resp, sizeof(resp), 0);
                break;
            }

            case DEV_BUZZER: {
                switch (msg.action) {
                case ACT_ON:
                    stop_melody_thread();
                    pthread_mutex_lock(&g_state.lock);
                    g_state.buzzer_state = 1;
                    pthread_mutex_unlock(&g_state.lock);
                    dispatch_buzzer(ACT_ON);
                    resp = (Message){MSG_RESP, DEV_BUZZER, ACT_ON, RESP_OK};
                    break;
                case ACT_OFF:
                    stop_melody_thread();
                    pthread_mutex_lock(&g_state.lock);
                    g_state.buzzer_state = 0;
                    pthread_mutex_unlock(&g_state.lock);
                    dispatch_buzzer(ACT_OFF);
                    resp = (Message){MSG_RESP, DEV_BUZZER, ACT_OFF, RESP_OK};
                    break;
                case ACT_PLAY_MELODY:
                    stop_melody_thread();
                    pthread_mutex_lock(&g_state.lock);
                    g_state.buzzer_state = 1;
                    g_melody_running     = 1;
                    pthread_mutex_unlock(&g_state.lock);
                    pthread_create(&g_melody_tid, NULL, melody_thread_fn, NULL);
                    log_event("MELODY START (Canon in D)");
                    resp = (Message){MSG_RESP, DEV_BUZZER, ACT_PLAY_MELODY, RESP_OK};
                    break;
                default:
                    resp = (Message){MSG_RESP, DEV_BUZZER, msg.action, RESP_ERR};
                }
                send(cfd, &resp, sizeof(resp), 0);
                break;
            }

            case DEV_SEGMENT: {
                if (msg.action != ACT_SET_NUMBER || msg.value < 1 || msg.value > 9) {
                    resp = (Message){MSG_RESP, DEV_SEGMENT, msg.action, RESP_ERR};
                    send(cfd, &resp, sizeof(resp), 0);
                    break;
                }

                /* 진행 중인 카운트다운 취소 */
                pthread_mutex_lock(&g_state.lock);
                pthread_t old_tid   = g_seg_tid;
                int       was_alive = g_seg_running;
                pthread_mutex_unlock(&g_state.lock);

                if (was_alive) {
                    pthread_cancel(old_tid);
                    pthread_join(old_tid, NULL);
                }

                pthread_mutex_lock(&g_state.lock);
                g_seg_running         = 1;
                g_state.segment_value = msg.value;
                pthread_mutex_unlock(&g_state.lock);

                char logbuf[64];
                snprintf(logbuf, sizeof(logbuf), "COUNTDOWN STARTED: %d", msg.value);
                log_event(logbuf);

                pthread_create(&g_seg_tid, NULL, segment_thread_fn,
                               (void *)(intptr_t)(int)msg.value);

                resp = (Message){MSG_RESP, DEV_SEGMENT, ACT_SET_NUMBER, RESP_OK};
                send(cfd, &resp, sizeof(resp), 0);
                break;
            }

            case DEV_SYSTEM: {
                if (msg.action != ACT_ALARM_OFF) {
                    resp = (Message){MSG_RESP, DEV_SYSTEM, msg.action, RESP_ERR};
                    send(cfd, &resp, sizeof(resp), 0);
                    break;
                }

                /* 멜로디·카운트다운 스레드 취소 */
                stop_melody_thread();

                pthread_mutex_lock(&g_state.lock);
                pthread_t old_tid   = g_seg_tid;
                int       was_alive = g_seg_running;
                pthread_mutex_unlock(&g_state.lock);

                if (was_alive) {
                    pthread_cancel(old_tid);
                    pthread_join(old_tid, NULL);
                }

                /* 상태 초기화 */
                pthread_mutex_lock(&g_state.lock);
                g_state.alarm_active   = 0;
                g_state.led_state      = 0;
                g_state.led_brightness = 0;
                g_state.buzzer_state   = 0;
                pthread_mutex_unlock(&g_state.lock);

                dispatch_led(ACT_OFF, 0);
                dispatch_buzzer(ACT_OFF);
                log_event("ALARM OFF (manual)");

                resp = (Message){MSG_RESP, DEV_SYSTEM, ACT_ALARM_OFF, RESP_OK};
                send(cfd, &resp, sizeof(resp), 0);
                break;
            }

            default:
                resp = (Message){MSG_RESP, msg.device, msg.action, RESP_ERR};
                send(cfd, &resp, sizeof(resp), 0);
            }
            break;  /* MSG_CMD end */

        /* ── MSG_QUERY ── */
        case MSG_QUERY: {
            uint8_t val;
            pthread_mutex_lock(&g_state.lock);
            switch (msg.device) {
            case DEV_SENSOR:  val = (uint8_t)g_state.sensor_blocked; break;
            case DEV_LED:     val = (uint8_t)g_state.led_state;      break;
            case DEV_BUZZER:  val = (uint8_t)g_state.buzzer_state;   break;
            default:          val = RESP_ERR;                         break;
            }
            pthread_mutex_unlock(&g_state.lock);

            if (msg.device == DEV_SENSOR || msg.device == DEV_LED || msg.device == DEV_BUZZER)
                resp = (Message){MSG_RESP, msg.device, ACT_GET_STATUS, val};
            else
                resp = (Message){MSG_RESP, msg.device, msg.action, RESP_ERR};
            send(cfd, &resp, sizeof(resp), 0);
            break;
        }

        default:
            resp = (Message){MSG_RESP, msg.device, msg.action, RESP_ERR};
            send(cfd, &resp, sizeof(resp), 0);
        }
    }

    /* recv() == 0 : 클라이언트 연결 끊김 */
    pthread_mutex_lock(&g_state.lock);
    g_state.client_fd = -1;
    pthread_mutex_unlock(&g_state.lock);
    close(cfd);
    return NULL;
}
