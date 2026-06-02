# 스마트 경보 시스템 — CLAUDE.md

## 프로젝트 개요

TCP 소켓으로 라즈베리파이4에 연결된 하드웨어를 우분투 클라이언트에서 원격 제어·모니터링하는 C 프로그램.  
리눅스 프로그래밍 심화실습 평가 과제. 제출 압축 파일명: `심화실습평가(리눅스 프로그래밍)_이름`

- **언어**: C (C99)
- **서버**: 라즈베리파이4 (ARM Cortex-A72, Raspberry Pi OS)
- **클라이언트**: 우분투 리눅스
- **빌드**: Makefile
- **포트**: 8080

---

## 디렉토리 구조

```
Project/
├── server/
│   ├── main.c          # 서버 진입점, 데몬화, TCP accept 루프
│   ├── daemon.c        # 데몬 프로세스 생성 (fork+setsid)
│   └── handler.c       # 클라이언트 메시지 수신 및 장치 디스패치
├── client/
│   └── main.c          # CLI 명령 입력 루프, 이벤트 수신 스레드
├── lib/
│   ├── led.c           # LED 제어 → led.so
│   ├── buzzer.c        # 부저 제어 → buzzer.so
│   ├── light_sensor.c  # 조도센서 제어 → light_sensor.so
│   └── segment.c       # 7세그먼트 제어 → segment.so
├── include/
│   ├── protocol.h      # Message 구조체, 모든 상수 정의
│   ├── led.h
│   ├── buzzer.h
│   ├── light_sensor.h
│   └── segment.h
├── docs/
│   ├── 기획서.md
│   └── API명세서.md
├── Makefile
└── README.md
```

---

## 빌드

```makefile
CC      = gcc
CFLAGS  = -Wall -I include
LDFLAGS = -lpthread -ldl

.PHONY: all server client lib clean

all: lib server client

lib:
	$(CC) -fPIC -shared $(CFLAGS) -o lib/led.so          lib/led.c
	$(CC) -fPIC -shared $(CFLAGS) -o lib/buzzer.so        lib/buzzer.c
	$(CC) -fPIC -shared $(CFLAGS) -o lib/light_sensor.so  lib/light_sensor.c
	$(CC) -fPIC -shared $(CFLAGS) -o lib/segment.so       lib/segment.c

server: lib
	$(CC) $(CFLAGS) -o $@ server/main.c server/daemon.c server/handler.c \
	    -Wl,-rpath,./lib $(LDFLAGS)

client:
	$(CC) $(CFLAGS) -o $@ client/main.c $(LDFLAGS)

clean:
	rm -f server client lib/*.so
```

빌드 명령: `make all` / `make lib` / `make server` / `make client` / `make clean`

---

## 공유 라이브러리 (.so) 구조

각 장치 모듈을 `-fPIC -shared`로 `.so`로 빌드하고 **dlopen/dlsym**으로 런타임 로드한다.

```c
// 서버에서 런타임 로드 방식
void *handle = dlopen("./lib/led.so", RTLD_LAZY);
led_init           = dlsym(handle, "led_init");
led_on             = dlsym(handle, "led_on");
led_off            = dlsym(handle, "led_off");
led_set_brightness = dlsym(handle, "led_set_brightness");
```

> 데몬화 시 `chdir("/")` 이후 dlopen 경로가 상대경로라면 실패함.  
> dlopen 경로는 절대경로 또는 실행파일 기준 절대경로로 처리할 것.

### 각 라이브러리 인터페이스

**include/led.h**
```c
int  led_init(int gpio_pin);
void led_on(void);
void led_off(void);
void led_set_brightness(int level);  // BRIGHTNESS_LOW=1 / MID=2 / HIGH=3
void led_cleanup(void);
```

**include/buzzer.h**
```c
int  buzzer_init(int gpio_pin);
void buzzer_on(void);
void buzzer_off(void);
void buzzer_cleanup(void);
```

**include/light_sensor.h**
```c
int  sensor_init(int gpio_pin);
int  sensor_read(void);  // 0=정상, 1=차단(침입)
void sensor_cleanup(void);
```

**include/segment.h**
```c
int  segment_init(int gpio_pins[8]);
void segment_display(int digit);  // 0~9, -1=꺼짐
void segment_cleanup(void);
```

---

## TCP 프로토콜 (include/protocol.h)

4바이트 고정 크기 바이너리 구조체 하나로 모든 통신 처리.

```c
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

typedef struct {
    uint8_t type;    // MSG_CMD / MSG_EVENT / MSG_RESP / MSG_QUERY
    uint8_t device;  // 대상 장치
    uint8_t action;  // 수행할 동작
    uint8_t value;   // 파라미터 (없으면 0x00)
} Message;

/* 메시지 타입 */
#define MSG_CMD     0x01   // Client → Server: 장치 제어
#define MSG_EVENT   0x02   // Server → Client: 이벤트 알림
#define MSG_RESP    0x03   // Server → Client: 명령 응답
#define MSG_QUERY   0x04   // Client → Server: 상태 조회

/* 장치 코드 */
#define DEV_SYSTEM  0x00
#define DEV_LED     0x01
#define DEV_BUZZER  0x02
#define DEV_SENSOR  0x03
#define DEV_SEGMENT 0x04

/* 동작 코드 */
#define ACT_ON              0x01
#define ACT_OFF             0x02
#define ACT_SET_BRIGHTNESS  0x03
#define ACT_SET_NUMBER      0x04
#define ACT_GET_STATUS      0x05
#define ACT_ALARM_OFF       0x06

/* LED 밝기 (ACT_SET_BRIGHTNESS의 value) */
#define BRIGHTNESS_HIGH 0x03   // duty cycle 100%
#define BRIGHTNESS_MID  0x02   // duty cycle 50%
#define BRIGHTNESS_LOW  0x01   // duty cycle 25%

/* 응답 코드 (MSG_RESP의 value) */
#define RESP_OK     0x00
#define RESP_ERR    0x01
#define RESP_BUSY   0x02

/* 이벤트 코드 (MSG_EVENT의 action) */
#define EVT_INTRUSION       0x01   // 침입 감지
#define EVT_ALARM_ON        0x02   // 경보 활성화
#define EVT_COUNTDOWN       0x03   // 카운트다운 진행 (value=현재숫자)
#define EVT_ALARM_TRIGGERED 0x04   // 카운트다운 0→부저 울림

#endif /* PROTOCOL_H */
```

전송/수신:
```c
send(fd, &msg, sizeof(Message), 0);
recv(fd, &msg, sizeof(Message), 0);
```

---

## 하드웨어 GPIO 핀 배치 (BCM 번호)

| 장치 | GPIO 핀 | 제어 방식 |
|------|---------|-----------|
| LED | GPIO 18 | PWM 소프트웨어 (duty 25/50/100%) |
| 부저 | GPIO 23 | 디지털 출력 (digitalWrite) |
| 조도센서 | GPIO 24 | 디지털 입력 (digitalRead) |
| 7세그먼트 | GPIO 4,17,27,22,5,6,13,19 | 디지털 출력 8핀 (a~g+dp) |

WiringPi: `wiringPiSetupGpio()`로 초기화 (BCM 번호 체계 사용)

---

## 스레드 구성

| 스레드 | 역할 | 생성 시점 |
|--------|------|-----------|
| 메인(데몬) | accept 루프, 클라이언트 연결 수락 | 서버 시작 시 |
| 클라이언트 핸들러 | 메시지 수신 → 장치 명령 디스패치 | 클라이언트 접속마다 |
| LED 제어 | ACT_ON/OFF/SET_BRIGHTNESS 처리 | 서버 시작 시 |
| 부저 제어 | ACT_ON/OFF 처리, 침입 감지 시 자동 ON | 서버 시작 시 |
| 센서 감시 | 조도센서 폴링 100ms, 침입 감지 시 경보 발동 | 서버 시작 시 |
| 세그먼트 카운트다운 | 1초마다 감소, 0 도달 시 부저 ON | ACT_SET_NUMBER 수신 시 |
| 이벤트 수신 (클라이언트) | 서버 이벤트 비동기 수신 | 클라이언트 시작 시 |

### 공유 상태 구조체

```c
typedef struct {
    int  alarm_active;      // 경보 활성 여부
    int  led_state;         // 0=off, 1=on
    int  led_brightness;    // BRIGHTNESS_LOW/MID/HIGH
    int  buzzer_state;      // 0=off, 1=on
    int  sensor_blocked;    // 0=정상, 1=차단(침입)
    int  segment_value;     // 현재 표시 숫자 (-1=꺼짐)
    int  client_fd;         // 연결된 클라이언트 소켓
    pthread_mutex_t lock;   // 공유 자원 보호
} SystemState;
```

---

## 필수 구현 요구사항

1. **멀티스레드 (pthread)** — 장치별 독립 스레드, mutex로 공유 상태 보호
2. **공유 라이브러리 (.so)** — dlopen/dlsym 런타임 로드
3. **서버 데몬화** — `fork()` + `setsid()` + 표준 입출력 `/dev/null` 리다이렉션
4. **클라이언트 시그널** — SIGINT(Ctrl+C)만 정상 종료, 나머지(SIGTERM·SIGHUP·SIGPIPE) SIG_IGN
5. **Makefile** — lib / server / client 독립 빌드 타겟

### 데몬화 구현

```c
void daemonize(void) {
    pid_t pid = fork();
    if (pid > 0) exit(EXIT_SUCCESS);
    if (pid < 0) exit(EXIT_FAILURE);

    setsid();
    signal(SIGHUP, SIG_IGN);
    chdir("/");
    umask(0);

    int fd = open("/dev/null", O_RDWR);
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    close(fd);
}
```

### TCP 서버 소켓 (SO_REUSEADDR 필수)

```c
int setup_server(int port) {
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(port),
        .sin_addr.s_addr = INADDR_ANY
    };
    bind(sfd, (struct sockaddr *)&addr, sizeof(addr));
    listen(sfd, 5);
    return sfd;
}
```

### 클라이언트 시그널 처리

```c
signal(SIGINT,  sigint_handler);  // Ctrl+C → close(sock_fd) + exit
signal(SIGTERM, SIG_IGN);
signal(SIGHUP,  SIG_IGN);
signal(SIGPIPE, SIG_IGN);         // 서버 연결 끊김 시 크래시 방지
```

---

## 핵심 동작 흐름

### 자동 경보 (서버 주도)

```
센서 스레드: digitalRead 100ms 폴링
→ 빛 차단 감지 (연속 3회 확인 — 디바운싱)
→ mutex_lock → alarm_active=1, sensor_blocked=1 → mutex_unlock
→ LED ON (BRIGHTNESS_HIGH) + 부저 ON
→ 클라이언트에 이벤트 전송:
    { MSG_EVENT, DEV_SENSOR, EVT_INTRUSION, 0 }
    { MSG_EVENT, DEV_LED,    EVT_ALARM_ON,  0 }
→ /tmp/alarm.log 기록
```

### 경보 해제 (클라이언트 주도)

```
클라이언트: { MSG_CMD, DEV_SEGMENT, ACT_SET_NUMBER, N }
서버:
→ 기존 세그먼트 스레드 pthread_cancel() + pthread_join()
→ 새 스레드 생성, segment_display(N)
→ 매초 { MSG_EVENT, DEV_SEGMENT, EVT_COUNTDOWN, 현재값 } 전송
→ 0 도달: 부저 ON + { MSG_EVENT, DEV_BUZZER, EVT_ALARM_TRIGGERED, 0 }

클라이언트: { MSG_CMD, DEV_SYSTEM, ACT_ALARM_OFF, 0 }
서버: 부저 OFF, LED OFF, alarm_active=0
```

---

## 이벤트 로그 (추가 기능 — /tmp/alarm.log)

```c
void log_event(const char *msg) {
    int fd = open("/tmp/alarm.log", O_CREAT | O_APPEND | O_WRONLY, 0644);
    if (fd < 0) return;
    time_t now = time(NULL);
    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    char line[256];
    int len = snprintf(line, sizeof(line), "[%s] %s\n", timebuf, msg);
    write(fd, line, len);
    close(fd);
}
```

---

## 예외 처리 주의사항

| 상황 | 처리 방법 |
|------|-----------|
| recv() == 0 (클라이언트 연결 끊김) | close(fd), client_fd=-1, 핸들러 스레드 종료 |
| 카운트다운 중 새 ACT_SET_NUMBER | pthread_cancel + pthread_join 후 재시작 |
| dlopen 경로 문제 (데몬 CWD = `/`) | 절대경로 사용 |
| 조도센서 노이즈 오감지 | 연속 3회 동일 상태 확인 후 처리 |
| SIGPIPE | SIG_IGN — 클라이언트 갑작스런 종료 시 서버 크래시 방지 |
| 포트 재사용 오류 | SO_REUSEADDR 적용 |

---

## 평가 기준

| 항목 | 배점 |
|------|------|
| 장치 구현 (LED·부저·조도센서·7세그먼트 각 10점) | 40점 |
| 구현 내용 (데몬, .so, 멀티스레드, 시그널) | 30점 |
| 사용자 편의성 | 10점 |
| 문서 | 10점 |
| 추가 기능 (로그, PIN 인증 등) | 10점 |

---

## 개발 일정 (2026-06-02 시작 기준)

| 단계 | 내용 | 일정 |
|------|------|------|
| 1 | protocol.h 확정, GPIO 핀 배치 | 1일차 |
| 2~3 | .so 4종 구현 및 단독 테스트 | 2~3일차 |
| 3 | Makefile 작성 | 3일차 |
| 4~5 | 서버 데몬화, dlopen, 장치별 스레드 | 4~5일차 |
| 5~6 | TCP accept 루프, 클라이언트 핸들러 | 5~6일차 |
| 6 | 클라이언트 CLI, 시그널 처리 | 6일차 |
| 7 | 이벤트 로그, PIN 인증 | 7일차 |
| 8 | 통합 테스트, gdb 디버깅 | 8일차 |
| 9 | 문서 정리, README, 제출 | 9일차 |
