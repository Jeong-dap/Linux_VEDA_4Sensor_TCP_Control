#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include "protocol.h"
#include "protocol_io.h"
#include "server.h"

#define PORT        8080
#define GPIO_LED     18
#define GPIO_BUZZER  23
#define I2C_SENSOR_ADDR  0x48
#define CLIENT_SEND_TIMEOUT_SECONDS 1

static int g_seg_pins[8] = {4, 17, 27, 22, 5, 6, 13, 19};
static void *g_library_handles[4];

char g_tty_path[64] = "";  /* 데몬화 전 터미널 경로 (log_event에서 사용) */

/* ── 공유 상태 ── */
SystemState g_state = {
    .segment_value = -1,
    .client_fds    = {-1, -1, -1, -1},
    .n_clients     = 0,
    .lock          = PTHREAD_MUTEX_INITIALIZER,
};

/* ── LED 전담 스레드 제어 ── */
LedCmd         g_led_cmd    = {0};
pthread_mutex_t g_led_mtx   = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  g_led_cond  = PTHREAD_COND_INITIALIZER;

/* ── 부저 전담 스레드 제어 ── */
BuzzerCmd      g_buzzer_cmd  = {0};
pthread_mutex_t g_buzzer_mtx = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  g_buzzer_cond = PTHREAD_COND_INITIALIZER;

/* ── .so 함수 포인터 ── */
int  (*fp_led_init)(int);
void (*fp_led_on)(void);
void (*fp_led_off)(void);
void (*fp_led_set_brightness)(int);
void (*fp_led_cleanup)(void);

int  (*fp_buzzer_init)(int);
void (*fp_buzzer_on)(void);
void (*fp_buzzer_off)(void);
void (*fp_buzzer_play_melody)(void);
void (*fp_buzzer_stop_melody)(void);
void (*fp_buzzer_cleanup)(void);

int  (*fp_sensor_init)(int);
int  (*fp_sensor_read)(void);
void (*fp_sensor_cleanup)(void);

int  (*fp_segment_init)(int *);
void (*fp_segment_display)(int);
void (*fp_segment_cleanup)(void);

/* ── dispatch helpers ── */
void dispatch_led(int action, int value) {
    pthread_mutex_lock(&g_led_mtx);
    g_led_cmd.action  = action;
    g_led_cmd.value   = value;
    g_led_cmd.pending = 1;
    pthread_cond_signal(&g_led_cond);
    pthread_mutex_unlock(&g_led_mtx);
}

void broadcast_msg(Message *msg) {
    int client_copies[MAX_CLIENTS];
    int copy_count = 0;

    pthread_mutex_lock(&g_state.lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_state.client_fds[i] >= 0) {
            int copy = dup(g_state.client_fds[i]);
            if (copy >= 0) {
                client_copies[copy_count++] = copy;
            }
        }
    }
    pthread_mutex_unlock(&g_state.lock);

    for (int i = 0; i < copy_count; i++) {
        protocol_send_message(client_copies[i], msg);
        close(client_copies[i]);
    }
}

void dispatch_buzzer(int action) {
    pthread_mutex_lock(&g_buzzer_mtx);
    g_buzzer_cmd.action  = action;
    g_buzzer_cmd.pending = 1;
    pthread_cond_signal(&g_buzzer_cond);
    pthread_mutex_unlock(&g_buzzer_mtx);
}

/* ── 이벤트 로그 (open/write 시스템 콜 직접 사용) ── */
void log_event(const char *msg) {
    time_t now = time(NULL);
    char timebuf[32];
    struct tm local_time;
    localtime_r(&now, &local_time);
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &local_time);
    char line[256];
    int len = snprintf(line, sizeof(line), "[%s] %s\n", timebuf, msg);
    if (len < 0) {
        return;
    }
    if ((size_t)len >= sizeof(line)) {
        len = (int)sizeof(line) - 1;
    }

    int fd = open("/tmp/alarm.log", O_CREAT | O_APPEND | O_WRONLY, 0644);
    if (fd >= 0) { write(fd, line, len); close(fd); }

    /* 데몬화 전 터미널에도 출력 */
    if (g_tty_path[0]) {
        int tfd = open(g_tty_path, O_WRONLY | O_NOCTTY);
        if (tfd >= 0) { write(tfd, line, len); close(tfd); }
    }
}

/* ── .so 동적 로드 ── */
static void *load_library(const char *base_dir, const char *name) {
    char path[512];
    int written = snprintf(path, sizeof(path), "%s/lib/%s.so", base_dir, name);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        fprintf(stderr, "library path is too long: %s\n", name);
        exit(EXIT_FAILURE);
    }

    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        fprintf(stderr, "dlopen %s: %s\n", path, dlerror());
        exit(EXIT_FAILURE);
    }
    return handle;
}

static void *load_symbol(void *handle, const char *name) {
    dlerror();
    void *symbol = dlsym(handle, name);
    const char *error = dlerror();
    if (error) {
        fprintf(stderr, "dlsym %s: %s\n", name, error);
        exit(EXIT_FAILURE);
    }
    return symbol;
}

static void load_libraries(const char *base_dir) {
#define ASSIGN_SYMBOL(target, handle, name) do { \
    _Static_assert(sizeof(target) == sizeof(void *), "incompatible dlsym pointer size"); \
    void *symbol = load_symbol((handle), (name)); \
    memcpy(&(target), &symbol, sizeof(target)); \
} while (0)

    g_library_handles[0] = load_library(base_dir, "led");
    ASSIGN_SYMBOL(fp_led_init, g_library_handles[0], "led_init");
    ASSIGN_SYMBOL(fp_led_on, g_library_handles[0], "led_on");
    ASSIGN_SYMBOL(fp_led_off, g_library_handles[0], "led_off");
    ASSIGN_SYMBOL(fp_led_set_brightness, g_library_handles[0], "led_set_brightness");
    ASSIGN_SYMBOL(fp_led_cleanup, g_library_handles[0], "led_cleanup");

    g_library_handles[1] = load_library(base_dir, "buzzer");
    ASSIGN_SYMBOL(fp_buzzer_init, g_library_handles[1], "buzzer_init");
    ASSIGN_SYMBOL(fp_buzzer_on, g_library_handles[1], "buzzer_on");
    ASSIGN_SYMBOL(fp_buzzer_off, g_library_handles[1], "buzzer_off");
    ASSIGN_SYMBOL(fp_buzzer_play_melody, g_library_handles[1], "buzzer_play_melody");
    ASSIGN_SYMBOL(fp_buzzer_stop_melody, g_library_handles[1], "buzzer_stop_melody");
    ASSIGN_SYMBOL(fp_buzzer_cleanup, g_library_handles[1], "buzzer_cleanup");

    g_library_handles[2] = load_library(base_dir, "cds");
    ASSIGN_SYMBOL(fp_sensor_init, g_library_handles[2], "sensor_init");
    ASSIGN_SYMBOL(fp_sensor_read, g_library_handles[2], "sensor_read");
    ASSIGN_SYMBOL(fp_sensor_cleanup, g_library_handles[2], "sensor_cleanup");

    g_library_handles[3] = load_library(base_dir, "segment");
    ASSIGN_SYMBOL(fp_segment_init, g_library_handles[3], "segment_init");
    ASSIGN_SYMBOL(fp_segment_display, g_library_handles[3], "segment_display");
    ASSIGN_SYMBOL(fp_segment_cleanup, g_library_handles[3], "segment_cleanup");
#undef ASSIGN_SYMBOL
}

/* ── TCP 서버 소켓 설정 ── */
static int setup_server(int port) {
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) { perror("socket"); exit(EXIT_FAILURE); }
    int opt = 1;
    if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(sfd);
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(port),
        .sin_addr.s_addr = INADDR_ANY,
    };
    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sfd);
        exit(EXIT_FAILURE);
    }
    if (listen(sfd, MAX_CLIENTS) < 0) {
        perror("listen");
        close(sfd);
        exit(EXIT_FAILURE);
    }
    return sfd;
}

static int configure_client_socket(int client_fd) {
    const struct timeval timeout = {
        .tv_sec = CLIENT_SEND_TIMEOUT_SECONDS,
        .tv_usec = 0,
    };
    return setsockopt(
        client_fd,
        SOL_SOCKET,
        SO_SNDTIMEO,
        &timeout,
        sizeof(timeout)
    );
}

static void initialize_hardware(void) {
    if (fp_led_init(GPIO_LED) < 0 ||
        fp_buzzer_init(GPIO_BUZZER) < 0 ||
        fp_sensor_init(I2C_SENSOR_ADDR) < 0 ||
        fp_segment_init(g_seg_pins) < 0) {
        log_event("HARDWARE INITIALIZATION FAILED");
        exit(EXIT_FAILURE);
    }
}

static void start_worker(pthread_t *thread, void *(*function)(void *), const char *name) {
    int result = pthread_create(thread, NULL, function, NULL);
    if (result != 0) {
        char message[128];
        snprintf(message, sizeof(message), "THREAD START FAILED: %s (%s)", name, strerror(result));
        log_event(message);
        exit(EXIT_FAILURE);
    }
}

int main(void) {
    /* 데몬화 후 CWD가 '/'가 되므로, 미리 실행 파일 기준 절대경로 계산 */
    char exe[512], base_dir[512];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n < 0) { perror("readlink"); exit(EXIT_FAILURE); }
    exe[n] = '\0';
    char *slash = strrchr(exe, '/');
    if (slash) {
        *slash = '\0';
        snprintf(base_dir, sizeof(base_dir), "%s", exe);
    } else {
        snprintf(base_dir, sizeof(base_dir), ".");
    }

    /* 데몬화 전 터미널 경로 저장 (데몬화 후 로그 출력용) */
    char *tty = ttyname(STDIN_FILENO);
    if (tty) {
        snprintf(g_tty_path, sizeof(g_tty_path), "%s", tty);
    }

    /* stderr 출력이 가능한 데몬화 이전에 .so 로드 */
    load_libraries(base_dir);
    int sfd = setup_server(PORT);

    daemonize();
    signal(SIGPIPE, SIG_IGN);

    /* GPIO 초기화 (.so 내부에서 wiringPiSetupGpio 포함) */
    initialize_hardware();

    /* 장치 전담 스레드 생성 */
    pthread_t led_tid, buzzer_tid, sensor_tid;
    start_worker(&led_tid, led_thread_fn, "led");
    start_worker(&buzzer_tid, buzzer_thread_fn, "buzzer");
    start_worker(&sensor_tid, sensor_thread_fn, "sensor");
    log_event("SERVER STARTED");

    /* TCP accept 루프 */
    while (1) {
        struct sockaddr_in cli;
        socklen_t cli_len = sizeof(cli);
        int cfd = accept(sfd, (struct sockaddr *)&cli, &cli_len);
        if (cfd < 0) continue;
        if (configure_client_socket(cfd) < 0) {
            close(cfd);
            continue;
        }

        /* 빈 슬롯에 새 클라이언트 등록 */
        pthread_mutex_lock(&g_state.lock);
        int added = 0;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (g_state.client_fds[i] < 0) {
                g_state.client_fds[i] = cfd;
                g_state.n_clients++;
                added = 1;
                break;
            }
        }
        pthread_mutex_unlock(&g_state.lock);
        if (!added) { close(cfd); continue; }

        pthread_t htid;
        int thread_result = pthread_create(&htid, NULL, handle_client, (void *)(intptr_t)cfd);
        if (thread_result != 0) {
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
            continue;
        }
        pthread_detach(htid);
    }
    return 0;
}
