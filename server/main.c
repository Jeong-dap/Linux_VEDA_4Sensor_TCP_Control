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
#include <netinet/in.h>
#include "protocol.h"
#include "server.h"

#define PORT        8080
#define GPIO_LED     18
#define GPIO_BUZZER  23
#define I2C_SENSOR_ADDR  0x48

static int g_seg_pins[8] = {4, 17, 27, 22, 5, 6, 13, 19};

char g_tty_path[64] = "";  /* 데몬화 전 터미널 경로 (log_event에서 사용) */

/* ── 공유 상태 ── */
SystemState g_state = {
    .segment_value = -1,
    .client_fd     = -1,
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

/* ── 세그먼트 스레드 ── */
pthread_t g_seg_tid     = 0;
int       g_seg_running = 0;

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

pthread_t g_melody_tid     = 0;
int       g_melody_running = 0;

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
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    char line[256];
    int len = snprintf(line, sizeof(line), "[%s] %s\n", timebuf, msg);

    int fd = open("/tmp/alarm.log", O_CREAT | O_APPEND | O_WRONLY, 0644);
    if (fd >= 0) { write(fd, line, len); close(fd); }

    /* 데몬화 전 터미널에도 출력 */
    if (g_tty_path[0]) {
        int tfd = open(g_tty_path, O_WRONLY | O_NOCTTY);
        if (tfd >= 0) { write(tfd, line, len); close(tfd); }
    }
}

/* ── .so 동적 로드 ── */
static void load_libraries(const char *base_dir) {
    char path[512];
    void *h;

#define LOAD(name, handle_var) \
    snprintf(path, sizeof(path), "%s/lib/" name ".so", base_dir); \
    handle_var = dlopen(path, RTLD_LAZY); \
    if (!(handle_var)) { fprintf(stderr, "dlopen %s: %s\n", path, dlerror()); exit(EXIT_FAILURE); }

    LOAD("led", h)
    fp_led_init           = dlsym(h, "led_init");
    fp_led_on             = dlsym(h, "led_on");
    fp_led_off            = dlsym(h, "led_off");
    fp_led_set_brightness = dlsym(h, "led_set_brightness");
    fp_led_cleanup        = dlsym(h, "led_cleanup");

    LOAD("buzzer", h)
    fp_buzzer_init         = dlsym(h, "buzzer_init");
    fp_buzzer_on           = dlsym(h, "buzzer_on");
    fp_buzzer_off          = dlsym(h, "buzzer_off");
    fp_buzzer_play_melody  = dlsym(h, "buzzer_play_melody");
    fp_buzzer_stop_melody  = dlsym(h, "buzzer_stop_melody");
    fp_buzzer_cleanup      = dlsym(h, "buzzer_cleanup");

    LOAD("cds", h)
    fp_sensor_init    = dlsym(h, "sensor_init");
    fp_sensor_read    = dlsym(h, "sensor_read");
    fp_sensor_cleanup = dlsym(h, "sensor_cleanup");

    LOAD("segment", h)
    fp_segment_init    = dlsym(h, "segment_init");
    fp_segment_display = dlsym(h, "segment_display");
    fp_segment_cleanup = dlsym(h, "segment_cleanup");
#undef LOAD
}

/* ── TCP 서버 소켓 설정 ── */
static int setup_server(int port) {
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) { perror("socket"); exit(EXIT_FAILURE); }
    int opt = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(port),
        .sin_addr.s_addr = INADDR_ANY,
    };
    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(EXIT_FAILURE);
    }
    listen(sfd, 5);
    return sfd;
}

int main(void) {
    /* 데몬화 후 CWD가 '/'가 되므로, 미리 실행 파일 기준 절대경로 계산 */
    char exe[512], base_dir[512];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n < 0) { perror("readlink"); exit(EXIT_FAILURE); }
    exe[n] = '\0';
    char *slash = strrchr(exe, '/');
    if (slash) { *slash = '\0'; strncpy(base_dir, exe, sizeof(base_dir)); }
    else        strncpy(base_dir, ".", sizeof(base_dir));

    /* 데몬화 전 터미널 경로 저장 (데몬화 후 로그 출력용) */
    char *tty = ttyname(STDIN_FILENO);
    if (tty) strncpy(g_tty_path, tty, sizeof(g_tty_path) - 1);

    /* stderr 출력이 가능한 데몬화 이전에 .so 로드 */
    load_libraries(base_dir);

    daemonize();
    signal(SIGPIPE, SIG_IGN);

    /* GPIO 초기화 (.so 내부에서 wiringPiSetupGpio 포함) */
    fp_led_init(GPIO_LED);
    fp_buzzer_init(GPIO_BUZZER);
    fp_sensor_init(I2C_SENSOR_ADDR);
    fp_segment_init(g_seg_pins);

    /* 장치 전담 스레드 생성 */
    pthread_t led_tid, buzzer_tid, sensor_tid;
    pthread_create(&led_tid,    NULL, led_thread_fn,    NULL);
    pthread_create(&buzzer_tid, NULL, buzzer_thread_fn, NULL);
    pthread_create(&sensor_tid, NULL, sensor_thread_fn, NULL);

    int sfd = setup_server(PORT);
    log_event("SERVER STARTED");

    /* TCP accept 루프 */
    while (1) {
        struct sockaddr_in cli;
        socklen_t cli_len = sizeof(cli);
        int cfd = accept(sfd, (struct sockaddr *)&cli, &cli_len);
        if (cfd < 0) continue;

        /* 기존 클라이언트 연결 정리 후 새 클라이언트 등록 */
        pthread_mutex_lock(&g_state.lock);
        if (g_state.client_fd >= 0)
            close(g_state.client_fd);
        g_state.client_fd = cfd;
        pthread_mutex_unlock(&g_state.lock);

        pthread_t htid;
        pthread_create(&htid, NULL, handle_client, (void *)(intptr_t)cfd);
        pthread_detach(htid);
    }
    return 0;
}
