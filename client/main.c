#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "protocol.h"

#define DEFAULT_IP "127.0.0.1"
#define PORT       8080

static int g_sock = -1;

static void sigint_handler(int sig) {
    (void)sig;
    if (g_sock >= 0) close(g_sock);
    printf("\n[클라이언트 종료]\n");
    exit(0);
}

/* ── 이벤트 수신 스레드 ── */
static void *recv_thread_fn(void *arg) {
    (void)arg;
    Message msg;
    while (recv(g_sock, &msg, sizeof(msg), 0) > 0) {
        if (msg.type == MSG_EVENT) {
            printf("\n");
            switch (msg.action) {
            case EVT_INTRUSION:
                printf("  !! [이벤트] 침입 감지!\n");
                break;
            case EVT_ALARM_ON:
                printf("  !! [이벤트] 경보 활성화 (LED HIGH + 부저)\n");
                break;
            case EVT_COUNTDOWN:
                printf("  >> [이벤트] 카운트다운: %d\n", msg.value);
                break;
            case EVT_ALARM_TRIGGERED:
                printf("  !! [이벤트] 카운트다운 완료 — 부저 울림!\n");
                break;
            default:
                printf("  >> [이벤트] device=0x%02x action=0x%02x value=%d\n",
                       msg.device, msg.action, msg.value);
            }
            fflush(stdout);
        } else if (msg.type == MSG_RESP) {
            const char *dev_str;
            switch (msg.device) {
            case DEV_LED:     dev_str = "LED";    break;
            case DEV_BUZZER:  dev_str = "부저";   break;
            case DEV_SENSOR:  dev_str = "센서";   break;
            case DEV_SEGMENT: dev_str = "세그먼트"; break;
            case DEV_SYSTEM:  dev_str = "시스템"; break;
            default:          dev_str = "UNKNOWN"; break;
            }

            printf("\n");
            if (msg.action == ACT_GET_LUX) {
                printf("  [조도] 현재 밝기 수치: %d / 255\n", msg.value);
            } else if (msg.action == ACT_GET_STATUS) {
                const char *state_str;
                if (msg.device == DEV_SENSOR)
                    state_str = msg.value ? "차단(침입)" : "정상";
                else
                    state_str = msg.value ? "ON" : "OFF";
                printf("  [응답] %s 상태: %s\n", dev_str, state_str);
            } else {
                const char *res = (msg.value == RESP_OK)   ? "OK"   :
                                  (msg.value == RESP_BUSY) ? "BUSY" : "ERR";
                printf("  [응답] %s → %s\n", dev_str, res);
            }
            fflush(stdout);
        }
    }
    printf("\n[서버 연결 끊김]\n");
    return NULL;
}

static void send_msg(Message msg) {
    if (send(g_sock, &msg, sizeof(msg), 0) < 0)
        perror("send");
}

/* 한 줄 읽어 정수 반환, EOF/-1이면 -1 */
static int read_choice(void) {
    char buf[16];
    printf("선택 > ");
    fflush(stdout);
    if (!fgets(buf, sizeof(buf), stdin)) return -1;
    return atoi(buf);
}

/* ── 서브메뉴 ── */
static void menu_led(void) {
    for (;;) {
        printf("\n");
        printf("  ┌─────────────────────────┐\n");
        printf("  │      LED 제어           │\n");
        printf("  ├─────────────────────────┤\n");
        printf("  │  1. LED ON              │\n");
        printf("  │  2. LED OFF             │\n");
        printf("  │  3. 밝기 설정           │\n");
        printf("  │  0. 뒤로                │\n");
        printf("  └─────────────────────────┘\n");

        int c = read_choice();
        if (c == 0) return;

        if (c == 1) {
            send_msg((Message){MSG_CMD, DEV_LED, ACT_ON, 0});
        } else if (c == 2) {
            send_msg((Message){MSG_CMD, DEV_LED, ACT_OFF, 0});
        } else if (c == 3) {
            printf("\n");
            printf("  ┌─────────────────────────┐\n");
            printf("  │      LED 밝기 설정      │\n");
            printf("  ├─────────────────────────┤\n");
            printf("  │  1. 낮음 (25%%)          │\n");
            printf("  │  2. 보통 (50%%)          │\n");
            printf("  │  3. 높음 (100%%)         │\n");
            printf("  │  0. 뒤로                │\n");
            printf("  └─────────────────────────┘\n");

            int b = read_choice();
            if (b == 1)
                send_msg((Message){MSG_CMD, DEV_LED, ACT_SET_BRIGHTNESS, BRIGHTNESS_LOW});
            else if (b == 2)
                send_msg((Message){MSG_CMD, DEV_LED, ACT_SET_BRIGHTNESS, BRIGHTNESS_MID});
            else if (b == 3)
                send_msg((Message){MSG_CMD, DEV_LED, ACT_SET_BRIGHTNESS, BRIGHTNESS_HIGH});
            else if (b != 0)
                printf("  잘못된 입력입니다.\n");
        } else {
            printf("  잘못된 입력입니다.\n");
        }
    }
}

static void menu_buzzer(void) {
    for (;;) {
        printf("\n");
        printf("  ┌───────────────────────────┐\n");
        printf("  │      부저 제어            │\n");
        printf("  ├───────────────────────────┤\n");
        printf("  │  1. 부저 ON               │\n");
        printf("  │  2. 부저 OFF              │\n");
        printf("  │  3. 엘리제를 위해여 재생  │\n");
        printf("  │  0. 뒤로                  │\n");
        printf("  └───────────────────────────┘\n");

        int c = read_choice();
        if (c == 0) return;

        if (c == 1)
            send_msg((Message){MSG_CMD, DEV_BUZZER, ACT_ON, 0});
        else if (c == 2)
            send_msg((Message){MSG_CMD, DEV_BUZZER, ACT_OFF, 0});
        else if (c == 3)
            send_msg((Message){MSG_CMD, DEV_BUZZER, ACT_PLAY_MELODY, 0});
        else
            printf("  잘못된 입력입니다.\n");
    }
}

static void menu_segment(void) {
    for (;;) {
        printf("\n");
        printf("  ┌─────────────────────────┐\n");
        printf("  │   세그먼트 카운트다운   │\n");
        printf("  ├─────────────────────────┤\n");
        printf("  │  1~9: 카운트다운 시작   │\n");
        printf("  │  10 : 카운트다운 중지   │\n");
        printf("  │  0  : 뒤로              │\n");
        printf("  └─────────────────────────┘\n");

        int c = read_choice();
        if (c == 0) return;

        if (c >= 1 && c <= 9)
            send_msg((Message){MSG_CMD, DEV_SEGMENT, ACT_SET_NUMBER, (uint8_t)c});
        else if (c == 10)
            send_msg((Message){MSG_CMD, DEV_SEGMENT, ACT_OFF, 0});
        else
            printf("  1~9 시작, 10 중지를 입력하세요.\n");
    }
}

static void menu_status(void) {
    for (;;) {
        printf("\n");
        printf("  ┌─────────────────────────┐\n");
        printf("  │      상태 조회          │\n");
        printf("  ├─────────────────────────┤\n");
        printf("  │  1. LED 상태            │\n");
        printf("  │  2. 부저 상태           │\n");
        printf("  │  3. 센서 상태           │\n");
        printf("  │  0. 뒤로                │\n");
        printf("  └─────────────────────────┘\n");

        int c = read_choice();
        if (c == 0) return;

        if (c == 1)
            send_msg((Message){MSG_QUERY, DEV_LED,    ACT_GET_STATUS, 0});
        else if (c == 2)
            send_msg((Message){MSG_QUERY, DEV_BUZZER, ACT_GET_STATUS, 0});
        else if (c == 3)
            send_msg((Message){MSG_QUERY, DEV_SENSOR, ACT_GET_STATUS, 0});
        else
            printf("  잘못된 입력입니다.\n");
    }
}

static void print_help(void) {
    printf("\n");
    printf("  ┌───────────────────────────────────────┐\n");
    printf("  │         스마트 경보 시스템 도움말     │\n");
    printf("  ├───────────────────────────────────────┤\n");
    printf("  │  1. LED 제어   — ON / OFF / 밝기 설정 │\n");
    printf("  │  2. 부저 제어  — ON / OFF / 멜로디    │\n");
    printf("  │  3. 세그먼트   — 1~9 카운트다운 시작 │\n");
    printf("  │  4. 상태 조회  — LED/부저/센서 확인   │\n");
    printf("  │  5. 조도 수치  — 현재 밝기 수치 조회  │\n");
    printf("  │  6. 도움말                            │\n");
    printf("  │  0. 종료                              │\n");
    printf("  ├───────────────────────────────────────┤\n");
    printf("  │  침입 감지 시 서버가 자동으로 이벤트  │\n");
    printf("  │  메시지를 전송합니다.                 │\n");
    printf("  └───────────────────────────────────────┘\n");
}

static void show_main_menu(void) {
    printf("\n");
    printf("  ╔═══════════════════════════════════╗\n");
    printf("  ║   스마트 경보 시스템 클라이언트   ║\n");
    printf("  ╠═══════════════════════════════════╣\n");
    printf("  ║  1. LED 제어                      ║\n");
    printf("  ║  2. 부저 제어                     ║\n");
    printf("  ║  3. 세그먼트 제어 (카운트다운)    ║\n");
    printf("  ║  4. 상태 조회                     ║\n");
    printf("  ║  5. 조도 수치 확인                ║\n");
    printf("  ║  6. 도움말                        ║\n");
    printf("  ║  0. 종료                          ║\n");
    printf("  ╚═══════════════════════════════════╝\n");
}

int main(int argc, char *argv[]) {
    const char *server_ip = (argc >= 2) ? argv[1] : DEFAULT_IP;

    signal(SIGINT,  sigint_handler);
    signal(SIGTERM, SIG_IGN);
    signal(SIGHUP,  SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);  /* Ctrl+Z (일시정지) 무시 */
    signal(SIGQUIT, SIG_IGN);  /* Ctrl+\ (종료+코어덤프) 무시 */

    g_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sock < 0) { perror("socket"); exit(EXIT_FAILURE); }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(PORT),
    };
    if (inet_pton(AF_INET, server_ip, &addr.sin_addr) <= 0) {
        fprintf(stderr, "잘못된 IP 주소: %s\n", server_ip);
        exit(EXIT_FAILURE);
    }
    if (connect(g_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect"); exit(EXIT_FAILURE);
    }
    printf("서버 %s:%d 에 연결됨\n", server_ip, PORT);

    pthread_t rtid;
    pthread_create(&rtid, NULL, recv_thread_fn, NULL);
    pthread_detach(rtid);

    for (;;) {
        show_main_menu();
        int c = read_choice();

        switch (c) {
        case 1: menu_led();     break;
        case 2: menu_buzzer();  break;
        case 3: menu_segment(); break;
        case 4: menu_status();  break;
        case 5:
            send_msg((Message){MSG_QUERY, DEV_SENSOR, ACT_GET_LUX, 0});
            break;
        case 6: print_help();   break;
        case 0:
        case -1:
            goto done;
        default:
            printf("  잘못된 입력입니다.\n");
        }
    }

done:
    close(g_sock);
    printf("[종료]\n");
    return 0;
}
