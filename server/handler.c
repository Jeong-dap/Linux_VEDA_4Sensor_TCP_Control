#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include "protocol.h"
#include "protocol_io.h"
#include "server.h"

static pthread_mutex_t g_melody_control_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_melody_tid;
static atomic_int g_melody_running = 0;
static int g_melody_joinable = 0;

static pthread_mutex_t g_segment_control_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_segment_tid;
static atomic_int g_segment_running = 0;
static int g_segment_joinable = 0;

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
    g_state.buzzer_state = 0;
    pthread_mutex_unlock(&g_state.lock);
    atomic_store(&g_melody_running, 0);
}

void *melody_thread_fn(void *arg) {
    (void)arg;
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    pthread_cleanup_push(melody_cleanup, NULL);

    fp_buzzer_play_melody();  /* usleep 루프 → 취소 포인트 있음 */

    pthread_cleanup_pop(0);
    pthread_mutex_lock(&g_state.lock);
    g_state.buzzer_state = 0;
    pthread_mutex_unlock(&g_state.lock);
    atomic_store(&g_melody_running, 0);
    return NULL;
}

static void stop_melody_locked(void) {
    if (g_melody_joinable) {
        fp_buzzer_stop_melody();
        if (atomic_load(&g_melody_running)) {
            pthread_cancel(g_melody_tid);
        }
        pthread_join(g_melody_tid, NULL);
        g_melody_joinable = 0;
        atomic_store(&g_melody_running, 0);
    }
}

/* 진행 중인 멜로디를 중단하고 종료된 스레드 자원도 회수한다. */
static void stop_melody_thread(void) {
    pthread_mutex_lock(&g_melody_control_lock);
    stop_melody_locked();
    pthread_mutex_unlock(&g_melody_control_lock);
}

static int start_melody_thread(void) {
    int result;

    pthread_mutex_lock(&g_melody_control_lock);
    stop_melody_locked();
    atomic_store(&g_melody_running, 1);
    result = pthread_create(&g_melody_tid, NULL, melody_thread_fn, NULL);
    if (result == 0) {
        g_melody_joinable = 1;
    } else {
        atomic_store(&g_melody_running, 0);
    }
    pthread_mutex_unlock(&g_melody_control_lock);
    return result;
}

static void send_response(int client_fd, const Message *response) {
    if (protocol_send_message(client_fd, response) < 0) {
        perror("send response");
    }
}

/* ════════════════════════════════════════
   센서 감시 스레드 — 100ms 폴링, 디바운싱 3회
   ════════════════════════════════════════ */
#define LIGHT_THRESHOLD 175  /* 이 값 이상이면 빛 꺼짐(차단) 감지 */

void *sensor_thread_fn(void *arg) {
    (void)arg;
    struct timespec ts = {0, 100 * 1000 * 1000};  /* 100ms */
    int consec = 0;

    while (1) {
        int lux = fp_sensor_read();
        if (lux < 0) {
            nanosleep(&ts, NULL);
            continue;
        }
        int blocked = (lux >= LIGHT_THRESHOLD);

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
            pthread_mutex_unlock(&g_state.lock);

            if (!already) {
                stop_melody_thread();
                dispatch_led(ACT_SET_BRIGHTNESS, BRIGHTNESS_HIGH);
                dispatch_buzzer(ACT_ON);

                Message e1 = {MSG_EVENT, DEV_SENSOR, EVT_INTRUSION, 0};
                Message e2 = {MSG_EVENT, DEV_LED,    EVT_ALARM_ON,  0};
                broadcast_msg(&e1);
                broadcast_msg(&e2);
                log_event("INTRUSION DETECTED");
                log_event("ALARM ON (LED HIGH + BUZZER)");

                sleep(5);

                dispatch_led(ACT_OFF, 0);
                dispatch_buzzer(ACT_OFF);
                pthread_mutex_lock(&g_state.lock);
                g_state.alarm_active   = 0;
                g_state.led_state      = 0;
                g_state.led_brightness = 0;
                g_state.buzzer_state   = 0;
                g_state.sensor_blocked = 0;
                pthread_mutex_unlock(&g_state.lock);
                log_event("ALARM AUTO OFF (5s)");
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
    int buzzer_owned = *(int *)arg;

    fp_segment_display(-1);
    if (buzzer_owned) {
        dispatch_buzzer(ACT_OFF);
    }
    pthread_mutex_lock(&g_state.lock);
    g_state.segment_value = -1;
    if (buzzer_owned) {
        g_state.buzzer_state = 0;
    }
    pthread_mutex_unlock(&g_state.lock);
    atomic_store(&g_segment_running, 0);
}

void *segment_thread_fn(void *arg) {
    int count = (int)(intptr_t)arg;
    int buzzer_owned = 0;

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    pthread_cleanup_push(seg_cleanup, &buzzer_owned);

    for (; count >= 0; count--) {
        fp_segment_display(count);

        pthread_mutex_lock(&g_state.lock);
        g_state.segment_value = count;
        pthread_mutex_unlock(&g_state.lock);

        /* 매초 EVT_COUNTDOWN 전송 */
        Message evt = {MSG_EVENT, DEV_SEGMENT, EVT_COUNTDOWN, (uint8_t)count};
        broadcast_msg(&evt);

        if (count == 0) {
            /* 카운트다운 완료 → 부저 ON, 이벤트 전송 */
            stop_melody_thread();
            pthread_mutex_lock(&g_state.lock);
            g_state.buzzer_state = 1;
            pthread_mutex_unlock(&g_state.lock);

            dispatch_buzzer(ACT_ON);
            buzzer_owned = 1;

            Message alarm_evt = {MSG_EVENT, DEV_BUZZER, EVT_ALARM_TRIGGERED, 0};
            broadcast_msg(&alarm_evt);
            log_event("COUNTDOWN REACHED 0 - BUZZER TRIGGERED");

            sleep(1);  /* 1초 후 세그먼트 OFF */
            fp_segment_display(-1);
            pthread_mutex_lock(&g_state.lock);
            g_state.segment_value = -1;
            pthread_mutex_unlock(&g_state.lock);

            sleep(2);  /* 2초 더 후 부저 OFF (총 3초) */

            dispatch_buzzer(ACT_OFF);
            buzzer_owned = 0;
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

    atomic_store(&g_segment_running, 0);

    return NULL;
}

static void stop_segment_locked(void) {
    if (!g_segment_joinable) {
        return;
    }
    if (atomic_load(&g_segment_running)) {
        pthread_cancel(g_segment_tid);
    }
    pthread_join(g_segment_tid, NULL);
    g_segment_joinable = 0;
    atomic_store(&g_segment_running, 0);
}

static void stop_segment_thread(void) {
    pthread_mutex_lock(&g_segment_control_lock);
    stop_segment_locked();
    pthread_mutex_unlock(&g_segment_control_lock);
}

static int start_segment_thread(uint8_t start_value) {
    int result;

    pthread_mutex_lock(&g_segment_control_lock);
    stop_segment_locked();
    atomic_store(&g_segment_running, 1);
    result = pthread_create(
        &g_segment_tid,
        NULL,
        segment_thread_fn,
        (void *)(intptr_t)(int)start_value
    );
    if (result == 0) {
        g_segment_joinable = 1;
    } else {
        atomic_store(&g_segment_running, 0);
    }
    pthread_mutex_unlock(&g_segment_control_lock);
    return result;
}

/* ════════════════════════════════════════
   클라이언트 핸들러 스레드
   ════════════════════════════════════════ */
static Message error_response(const Message *request) {
    return (Message){MSG_RESP, request->device, request->action, RESP_ERR};
}

static Message handle_led_command(const Message *command) {
    switch (command->action) {
    case ACT_ON:
        pthread_mutex_lock(&g_state.lock);
        g_state.led_state = 1;
        pthread_mutex_unlock(&g_state.lock);
        dispatch_led(ACT_ON, 0);
        return (Message){MSG_RESP, DEV_LED, ACT_ON, RESP_OK};

    case ACT_OFF:
        pthread_mutex_lock(&g_state.lock);
        g_state.led_state = 0;
        g_state.led_brightness = 0;
        pthread_mutex_unlock(&g_state.lock);
        dispatch_led(ACT_OFF, 0);
        return (Message){MSG_RESP, DEV_LED, ACT_OFF, RESP_OK};

    case ACT_SET_BRIGHTNESS:
        if (command->value < BRIGHTNESS_LOW || command->value > BRIGHTNESS_HIGH) {
            return error_response(command);
        }
        pthread_mutex_lock(&g_state.lock);
        g_state.led_state = 1;
        g_state.led_brightness = command->value;
        pthread_mutex_unlock(&g_state.lock);
        dispatch_led(ACT_SET_BRIGHTNESS, command->value);
        return (Message){MSG_RESP, DEV_LED, ACT_SET_BRIGHTNESS, RESP_OK};

    default:
        return error_response(command);
    }
}

static Message handle_buzzer_command(const Message *command) {
    switch (command->action) {
    case ACT_ON:
        stop_melody_thread();
        pthread_mutex_lock(&g_state.lock);
        g_state.buzzer_state = 1;
        pthread_mutex_unlock(&g_state.lock);
        dispatch_buzzer(ACT_ON);
        return (Message){MSG_RESP, DEV_BUZZER, ACT_ON, RESP_OK};

    case ACT_OFF:
        stop_melody_thread();
        pthread_mutex_lock(&g_state.lock);
        g_state.buzzer_state = 0;
        pthread_mutex_unlock(&g_state.lock);
        dispatch_buzzer(ACT_OFF);
        return (Message){MSG_RESP, DEV_BUZZER, ACT_OFF, RESP_OK};

    case ACT_PLAY_MELODY:
        if (start_melody_thread() != 0) {
            return error_response(command);
        }
        pthread_mutex_lock(&g_state.lock);
        g_state.buzzer_state = 1;
        pthread_mutex_unlock(&g_state.lock);
        log_event("MELODY START (Für Elise)");
        return (Message){MSG_RESP, DEV_BUZZER, ACT_PLAY_MELODY, RESP_OK};

    default:
        return error_response(command);
    }
}

static Message handle_segment_command(const Message *command) {
    if (command->action == ACT_OFF) {
        stop_segment_thread();
        log_event("COUNTDOWN STOPPED (manual)");
        return (Message){MSG_RESP, DEV_SEGMENT, ACT_OFF, RESP_OK};
    }
    if (command->action != ACT_SET_NUMBER ||
        command->value < 1 ||
        command->value > 9) {
        return error_response(command);
    }
    if (start_segment_thread(command->value) != 0) {
        return error_response(command);
    }

    pthread_mutex_lock(&g_state.lock);
    g_state.segment_value = command->value;
    pthread_mutex_unlock(&g_state.lock);

    char log_message[64];
    snprintf(log_message, sizeof(log_message), "COUNTDOWN STARTED: %d", command->value);
    log_event(log_message);
    return (Message){MSG_RESP, DEV_SEGMENT, ACT_SET_NUMBER, RESP_OK};
}

static Message handle_system_command(const Message *command) {
    if (command->action != ACT_ALARM_OFF) {
        return error_response(command);
    }

    stop_melody_thread();
    stop_segment_thread();

    pthread_mutex_lock(&g_state.lock);
    g_state.alarm_active = 0;
    g_state.led_state = 0;
    g_state.led_brightness = 0;
    g_state.buzzer_state = 0;
    pthread_mutex_unlock(&g_state.lock);

    dispatch_led(ACT_OFF, 0);
    dispatch_buzzer(ACT_OFF);
    log_event("ALARM OFF (manual)");
    return (Message){MSG_RESP, DEV_SYSTEM, ACT_ALARM_OFF, RESP_OK};
}

static Message handle_command(const Message *command) {
    switch (command->device) {
    case DEV_LED:
        return handle_led_command(command);
    case DEV_BUZZER:
        return handle_buzzer_command(command);
    case DEV_SEGMENT:
        return handle_segment_command(command);
    case DEV_SYSTEM:
        return handle_system_command(command);
    default:
        return error_response(command);
    }
}

static Message handle_query(const Message *query) {
    if (query->device == DEV_SENSOR && query->action == ACT_GET_LUX) {
        int lux = fp_sensor_read();
        if (lux < 0 || lux > UINT8_MAX) {
            return (Message){MSG_RESP, DEV_SENSOR, 0, RESP_ERR};
        }
        return (Message){MSG_RESP, DEV_SENSOR, ACT_GET_LUX, (uint8_t)lux};
    }
    if (query->action != ACT_GET_STATUS) {
        return error_response(query);
    }

    uint8_t value;
    pthread_mutex_lock(&g_state.lock);
    switch (query->device) {
    case DEV_SENSOR:
        value = (uint8_t)g_state.sensor_blocked;
        break;
    case DEV_LED:
        value = (uint8_t)g_state.led_state;
        break;
    case DEV_BUZZER:
        value = (uint8_t)g_state.buzzer_state;
        break;
    default:
        pthread_mutex_unlock(&g_state.lock);
        return error_response(query);
    }
    pthread_mutex_unlock(&g_state.lock);
    return (Message){MSG_RESP, query->device, ACT_GET_STATUS, value};
}

void *handle_client(void *arg) {
    int cfd = (int)(intptr_t)arg;
    Message request;

    while (protocol_recv_message(cfd, &request) == 1) {
        Message response;
        if (request.type == MSG_CMD) {
            response = handle_command(&request);
        } else if (request.type == MSG_QUERY) {
            response = handle_query(&request);
        } else {
            response = error_response(&request);
        }
        send_response(cfd, &response);
    }

    /* recv() == 0 : 클라이언트 연결 끊김 */
    pthread_mutex_lock(&g_state.lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_state.client_fds[i] == cfd) {
            g_state.client_fds[i] = -1;
            g_state.n_clients--;
            break;
        }
    }
    pthread_mutex_unlock(&g_state.lock);
    close(cfd);
    return NULL;
}
