# 스마트 경보 시스템 — CLAUDE.md

## 프로젝트 개요

TCP 소켓으로 라즈베리파이4에 연결된 하드웨어를 우분투 클라이언트에서 원격 제어·모니터링하는 C 프로그램.  
리눅스 프로그래밍 심화실습 평가 과제. 제출 압축 파일명: `심화실습평가(리눅스 프로그래밍)_이름`

- **언어**: C (C99, `_GNU_SOURCE`)
- **서버**: 라즈베리파이4 (ARM Cortex-A72, Raspberry Pi OS) — 크로스컴파일 대상
- **클라이언트**: 우분투 리눅스 — 네이티브 빌드
- **빌드**: Makefile (크로스컴파일 + deploy 자동화)
- **포트**: 8080
- **라즈베리파이 접속**: `njd990603@172.20.33.119`

---

## 디렉토리 구조

```
Project/
├── server/
│   ├── main.c          # 서버 진입점, .so 로드, 데몬화, TCP accept 루프
│   ├── daemon.c        # 데몬 프로세스 생성 (fork+setsid)
│   ├── handler.c       # 장치 전담 스레드 + 클라이언트 핸들러
│   └── server.h        # SystemState, LedCmd, BuzzerCmd 구조체 + extern 선언
├── client/
│   └── main.c          # CLI 메뉴 루프, 이벤트 수신 스레드
├── lib/
│   ├── led.c           # LED 제어 (softPwm) → led.so
│   ├── buzzer.c        # 부저 제어 (softTone) + Für Elise 멜로디 → buzzer.so
│   ├── light_sensor.c  # 조도센서 2핀 제어 → light_sensor.so
│   └── segment.c       # 7세그먼트 제어 (common anode) → segment.so
├── include/
│   ├── protocol.h      # Message 구조체, 모든 상수 정의
│   ├── led.h
│   ├── buzzer.h
│   ├── light_sensor.h
│   └── segment.h
├── docs/
│   ├── 기획서.md
│   ├── 회로도.md
│   └── 회로구상.png
├── Makefile
├── scp.sh              # 수동 배포 스크립트 (참고용)
├── .gitignore          # alarm_server, alarm_client, lib/*.so 제외
└── README.md
```

빌드 산출물 (gitignore):
- `alarm_server` — ARM64 서버 바이너리 (라즈베리파이용)
- `alarm_client` — x86_64 클라이언트 바이너리
- `lib/*.so` — ARM64 공유 라이브러리

---

## 빌드

```makefile
PIHOST   = njd990603@172.20.33.119
PIDIR    = /home/njd990603/Project

CROSS    = aarch64-linux-gnu-
CC_SRV   = $(CROSS)gcc    # 서버·라이브러리용 (ARM64 크로스컴파일)
CC_CLI   = gcc            # 클라이언트용 (x86_64 네이티브)

CFLAGS   = -Wall -I include
LDFLAGS  = -lpthread -ldl

WIRING_INC ?=
WIRING_LIB ?=

SRV_CFLAGS  = $(CFLAGS) $(if $(WIRING_INC),-I$(WIRING_INC))
SRV_LDFLAGS = -lwiringPi  $(if $(WIRING_LIB),-L$(WIRING_LIB))

.PHONY: all server client lib clean deploy

all: lib server client deploy

lib:
    $(CC_SRV) -fPIC -shared $(SRV_CFLAGS) -o lib/led.so          lib/led.c          $(SRV_LDFLAGS)
    $(CC_SRV) -fPIC -shared $(SRV_CFLAGS) -o lib/buzzer.so        lib/buzzer.c       $(SRV_LDFLAGS)
    $(CC_SRV) -fPIC -shared $(SRV_CFLAGS) -o lib/light_sensor.so  lib/light_sensor.c $(SRV_LDFLAGS)
    $(CC_SRV) -fPIC -shared $(SRV_CFLAGS) -o lib/segment.so       lib/segment.c      $(SRV_LDFLAGS)

server: lib
    $(CC_SRV) $(SRV_CFLAGS) -o alarm_server server/main.c server/daemon.c server/handler.c \
        -Wl,-rpath,$(abspath lib) $(LDFLAGS) $(SRV_LDFLAGS)

client:
    $(CC_CLI) $(CFLAGS) -o alarm_client client/main.c $(LDFLAGS)

deploy:
    -ssh $(PIHOST) pkill -f alarm_server
    ssh $(PIHOST) mkdir -p $(PIDIR)/lib
    scp alarm_server $(PIHOST):$(PIDIR)/
    scp lib/*.so     $(PIHOST):$(PIDIR)/lib/

clean:
    rm -f alarm_server alarm_client lib/*.so
```

빌드 명령:
- `make all` — lib + server(크로스) + client(네이티브) + deploy
- `make lib` / `make server` / `make client` 독립 빌드
- `make deploy` — alarm_server + lib/*.so를 라즈베리파이로 scp 전송 후 기존 프로세스 종료
- `make CROSS= all` — 라즈베리파이에서 직접 빌드 시 크로스컴파일러 비활성화

---

## 공유 라이브러리 (.so) 구조

각 장치 모듈을 `-fPIC -shared`로 `.so`로 빌드하고 **dlopen/dlsym**으로 런타임 로드한다.

### dlopen 절대경로 처리

데몬화 시 `chdir("/")` 이후 상대경로 dlopen은 실패한다.  
`readlink("/proc/self/exe")`로 실행 파일의 절대경로를 구한 뒤 base_dir을 계산해 **데몬화 이전**에 로드한다.

```c
// server/main.c
ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
exe[n] = '\0';
char *slash = strrchr(exe, '/');
*slash = '\0';
strncpy(base_dir, exe, sizeof(base_dir));  // e.g. "/home/njd990603/Project"

load_libraries(base_dir);  // 데몬화 전에 로드
daemonize();
```

```c
// load_libraries() 내부 — 매크로로 반복 처리
snprintf(path, sizeof(path), "%s/lib/led.so", base_dir);
void *h = dlopen(path, RTLD_LAZY);
fp_led_init           = dlsym(h, "led_init");
fp_led_on             = dlsym(h, "led_on");
fp_led_off            = dlsym(h, "led_off");
fp_led_set_brightness = dlsym(h, "led_set_brightness");
fp_led_cleanup        = dlsym(h, "led_cleanup");
```

### 각 라이브러리 인터페이스

**include/led.h** — softPwm 기반 PWM
```c
int  led_init(int gpio_pin);
void led_on(void);                    // duty 100%
void led_off(void);                   // duty 0%
void led_set_brightness(int level);   // LOW=25% / MID=50% / HIGH=100%
void led_cleanup(void);
```

**include/buzzer.h** — softTone 기반, Für Elise 멜로디 포함
```c
int  buzzer_init(int gpio_pin);
void buzzer_on(void);                 // 1000Hz 경보음
void buzzer_off(void);
void buzzer_play_melody(void);        // Für Elise (usleep 루프, 취소 포인트 있음)
void buzzer_stop_melody(void);        // g_stop=1 → usleep 루프 종료
void buzzer_cleanup(void);
```

**include/light_sensor.h** — 디지털 입력
```c
int  sensor_init(int gpio_pin);  // GPIO 24
int  sensor_read(void);   // 0=정상, 1=차단
void sensor_cleanup(void);
```

**include/segment.h** — common anode (HIGH=OFF), SEG_MAP 룩업테이블
```c
int  segment_init(int gpio_pins[8]);  // GPIO 4,17,27,22,5,6,13,19 (a~g+dp)
void segment_display(int digit);      // 0~9, -1=전체 끄기
void segment_cleanup(void);
```

---

## TCP 프로토콜 (include/protocol.h)

4바이트 고정 크기 바이너리 구조체 하나로 모든 통신 처리.

```c
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
#define ACT_PLAY_MELODY     0x07   // 부저 멜로디 재생 (Für Elise)

/* LED 밝기 (ACT_SET_BRIGHTNESS의 value) */
#define BRIGHTNESS_LOW  0x01   // duty cycle 25%
#define BRIGHTNESS_MID  0x02   // duty cycle 50%
#define BRIGHTNESS_HIGH 0x03   // duty cycle 100%

/* 응답 코드 (MSG_RESP의 value) */
#define RESP_OK     0x00
#define RESP_ERR    0x01
#define RESP_BUSY   0x02

/* 이벤트 코드 (MSG_EVENT의 action) */
#define EVT_INTRUSION       0x01   // 침입 감지
#define EVT_ALARM_ON        0x02   // 경보 활성화
#define EVT_COUNTDOWN       0x03   // 카운트다운 진행 (value=현재숫자)
#define EVT_ALARM_TRIGGERED 0x04   // 카운트다운 0→부저 울림
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
| LED | GPIO 18 | softPwm (duty 25/50/100%) |
| 부저 | GPIO 23 | softTone (1000Hz 경보음, Für Elise 멜로디) |
| 조도센서 | GPIO 24 | 디지털 입력 (digitalRead) |
| 7세그먼트 | GPIO 4,17,27,22,5,6,13,19 | 디지털 출력 8핀 (a~g+dp), common anode |

WiringPi: 각 `.so`의 `init()` 함수에서 `wiringPiSetupGpio()` 호출 (BCM 번호 체계)

---

## 스레드 구성

| 스레드 | 역할 | 생성 시점 | 취소 가능 |
|--------|------|-----------|-----------|
| 메인(데몬) | accept 루프, 클라이언트 연결 수락 | 서버 시작 시 | — |
| 클라이언트 핸들러 | 메시지 수신 → 장치 명령 디스패치 | 클라이언트 접속마다 (detach) | — |
| LED 전담 | cond_wait → ACT_ON/OFF/SET_BRIGHTNESS 처리 | 서버 시작 시 | — |
| 부저 전담 | cond_wait → ACT_ON/ACT_OFF 처리 | 서버 시작 시 | — |
| 멜로디 전담 | `buzzer_play_melody()` 블로킹 재생 | ACT_PLAY_MELODY 수신 시 | pthread_cancel (cleanup_push로 stop_melody 보장) |
| 센서 감시 | 100ms 폴링, 연속 3회 차단 시 경보 발동 | 서버 시작 시 | — |
| 세그먼트 카운트다운 | 1초마다 감소, 0 도달 시 부저 3초 자동 OFF | ACT_SET_NUMBER 수신 시 | pthread_cancel (cleanup_push로 세그먼트 OFF 보장) |
| 이벤트 수신 (클라이언트) | 서버 이벤트/응답 비동기 수신 및 출력 | 클라이언트 시작 시 (detach) | — |

### 장치 전담 스레드 통신 방식

LED·부저 전담 스레드는 `pthread_cond_wait`로 대기하다 핸들러에서 `dispatch_led()` / `dispatch_buzzer()` 호출 시 깨어나 동작한다.

```c
// server/server.h
typedef struct { int action; int value; int pending; } LedCmd;
typedef struct { int action;            int pending; } BuzzerCmd;

// server/main.c
void dispatch_led(int action, int value) {
    pthread_mutex_lock(&g_led_mtx);
    g_led_cmd = (LedCmd){action, value, 1};
    pthread_cond_signal(&g_led_cond);
    pthread_mutex_unlock(&g_led_mtx);
}
```

### 공유 상태 구조체 (server/server.h)

```c
typedef struct {
    int  alarm_active;      // 경보 활성 여부
    int  led_state;         // 0=off, 1=on
    int  led_brightness;    // BRIGHTNESS_LOW/MID/HIGH
    int  buzzer_state;      // 0=off, 1=on
    int  sensor_blocked;    // 0=정상, 1=차단(침입)
    int  segment_value;     // 현재 표시 숫자 (-1=꺼짐)
    int  client_fd;         // 연결된 클라이언트 소켓 (-1=미연결)
    pthread_mutex_t lock;   // 공유 자원 보호
} SystemState;
```

---

## 필수 구현 요구사항

1. **멀티스레드 (pthread)** — 장치별 독립 스레드, mutex+cond로 공유 상태 보호
2. **공유 라이브러리 (.so)** — dlopen/dlsym 런타임 로드
3. **서버 데몬화** — `fork()` + `setsid()` + 표준 입출력 `/dev/null` 리다이렉션
4. **클라이언트 시그널** — SIGINT(Ctrl+C)만 정상 종료, 나머지(SIGTERM·SIGHUP·SIGPIPE·SIGTSTP·SIGQUIT) SIG_IGN
5. **Makefile** — lib / server / client 독립 빌드 타겟

### 데몬화 구현 (server/daemon.c)

```c
void daemonize(void) {
    pid_t pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);   // 부모 종료

    setsid();
    signal(SIGHUP, SIG_IGN);
    if (chdir("/") < 0) exit(EXIT_FAILURE);
    umask(0);

    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
    }
}
```

### TCP 서버 소켓 (SO_REUSEADDR 필수)

```c
int sfd = socket(AF_INET, SOCK_STREAM, 0);
int opt = 1;
setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
struct sockaddr_in addr = {
    .sin_family      = AF_INET,
    .sin_port        = htons(8080),
    .sin_addr.s_addr = INADDR_ANY,
};
bind(sfd, (struct sockaddr *)&addr, sizeof(addr));
listen(sfd, 5);
```

### 클라이언트 시그널 처리

```c
signal(SIGINT,  sigint_handler);  // Ctrl+C → close(g_sock) + exit(0)
signal(SIGTERM, SIG_IGN);
signal(SIGHUP,  SIG_IGN);
signal(SIGPIPE, SIG_IGN);         // 서버 연결 끊김 시 크래시 방지
signal(SIGTSTP, SIG_IGN);         // Ctrl+Z (일시정지) 무시
signal(SIGQUIT, SIG_IGN);         // Ctrl+\ (종료+코어덤프) 무시
```

### 클라이언트 실행

```bash
./alarm_client [서버_IP]   # IP 생략 시 127.0.0.1 사용
./alarm_client 172.20.33.119
```

---

## 핵심 동작 흐름

### 자동 경보 (서버 주도)

```
센서 스레드: sensor_read() 100ms 폴링 (GPIO 24)
→ 차단 감지 (연속 3회 확인 — 디바운싱)
→ mutex_lock → alarm_active=1, sensor_blocked=1, led/buzzer_state=1 → mutex_unlock
→ dispatch_led(ACT_SET_BRIGHTNESS, BRIGHTNESS_HIGH)
→ dispatch_buzzer(ACT_ON)
→ 클라이언트에 이벤트 전송:
    { MSG_EVENT, DEV_SENSOR, EVT_INTRUSION, 0 }
    { MSG_EVENT, DEV_LED,    EVT_ALARM_ON,  0 }
→ log_event("INTRUSION DETECTED") → /tmp/alarm.log + 데몬화 전 tty
```

### 카운트다운 경보 해제 (클라이언트 주도)

```
클라이언트: { MSG_CMD, DEV_SEGMENT, ACT_SET_NUMBER, N }  (N: 1~9)
서버:
→ 기존 세그먼트 스레드 pthread_cancel() + pthread_join()
→ 새 스레드 생성, segment_display(N) 부터 시작
→ 매초 감소, { MSG_EVENT, DEV_SEGMENT, EVT_COUNTDOWN, 현재값 } 전송
→ 0 도달: dispatch_buzzer(ACT_ON) + { MSG_EVENT, DEV_BUZZER, EVT_ALARM_TRIGGERED, 0 }
→ 1초 후 segment_display(-1), 2초 후 dispatch_buzzer(ACT_OFF)  ← 총 3초 후 자동 OFF

클라이언트: { MSG_CMD, DEV_SYSTEM, ACT_ALARM_OFF, 0 }
서버: stop_melody_thread() + 세그먼트 스레드 취소 → 부저OFF, LED OFF, alarm_active=0
```

### 멜로디 재생 (추가 기능)

```
클라이언트: { MSG_CMD, DEV_BUZZER, ACT_PLAY_MELODY, 0 }
서버:
→ stop_melody_thread() (기존 멜로디 취소)
→ melody_thread_fn 새 스레드 생성
→ buzzer_play_melody() 블로킹 실행 (Für Elise, usleep 루프)
→ ACT_ALARM_OFF 또는 ACT_OFF 수신 시 pthread_cancel() → cleanup_push에서 buzzer_stop_melody()
```

### MSG_QUERY 상태 조회

```
클라이언트: { MSG_QUERY, DEV_LED/DEV_BUZZER/DEV_SENSOR, ACT_GET_STATUS, 0 }
서버:
→ g_state.lock 잠금 후 상태값 읽기
→ { MSG_RESP, device, ACT_GET_STATUS, 현재값 } 응답
   DEV_SENSOR: 0=정상, 1=차단
   DEV_LED/DEV_BUZZER: 0=OFF, 1=ON
```

---

## 이벤트 로그 (/tmp/alarm.log)

```c
void log_event(const char *msg) {
    time_t now = time(NULL);
    char timebuf[32], line[256];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    int len = snprintf(line, sizeof(line), "[%s] %s\n", timebuf, msg);

    int fd = open("/tmp/alarm.log", O_CREAT | O_APPEND | O_WRONLY, 0644);
    if (fd >= 0) { write(fd, line, len); close(fd); }

    // 데몬화 전 터미널 경로(g_tty_path)에도 출력 — ttyname(STDIN) 저장
    if (g_tty_path[0]) {
        int tfd = open(g_tty_path, O_WRONLY | O_NOCTTY);
        if (tfd >= 0) { write(tfd, line, len); close(tfd); }
    }
}
```

로그 기록 시점: 서버 시작 / 침입 감지 / 경보 ON / 카운트다운 시작 / 카운트다운 완료·부저 자동 OFF / 멜로디 시작 / 수동 경보 해제

---

## 예외 처리 주의사항

| 상황 | 처리 방법 |
|------|-----------|
| recv() == 0 (클라이언트 연결 끊김) | close(fd), g_state.client_fd=-1, 핸들러 스레드 종료 |
| 카운트다운 중 새 ACT_SET_NUMBER | pthread_cancel + pthread_join 후 재시작 |
| ACT_ALARM_OFF | stop_melody_thread() + 세그먼트 스레드 취소 모두 처리 |
| dlopen 경로 문제 (데몬 CWD = `/`) | readlink("/proc/self/exe") 로 절대경로 계산, 데몬화 전에 로드 |
| 조도센서 노이즈 오감지 | 연속 3회 차단 확인 후 경보 발동 (dibaouncing) |
| SIGPIPE | SIG_IGN — 클라이언트 갑작스런 종료 시 서버 크래시 방지 |
| 포트 재사용 오류 | SO_REUSEADDR 적용 |
| 기존 클라이언트 재접속 | 새 클라이언트 accept 시 이전 fd close 후 교체 |
| ACT_SET_NUMBER 범위 | 1~9 외 값은 RESP_ERR 즉시 반환 |
| 멜로디 중단 | buzzer_stop_melody() 후 pthread_cancel → cleanup_push 보장 |

---

## 평가 기준

| 항목 | 배점 |
|------|------|
| 장치 구현 (LED·부저·조도센서·7세그먼트 각 10점) | 40점 |
| 구현 내용 (데몬, .so, 멀티스레드, 시그널) | 30점 |
| 사용자 편의성 | 10점 |
| 문서 | 10점 |
| 추가 기능 (로그, 멜로디 재생 등) | 10점 |

---

## 개발 일정 (2026-06-02 시작 기준, 제출 기한 2026-06-05)

| 날짜 | 단계 | 내용 | 관련 강의 |
|------|------|------|-----------|
| 6/2 (1일차) | 기반 구축 ✅ | 헤더 5종, 공유 라이브러리 4종, Makefile, server.h | 2장, 3장, WiringPi |
| 6/3 (2일차) | 서버 구현 ✅ | daemon.c, dlopen 로드, 장치 전담 스레드, TCP accept, handler.c | 5-6장, 7-8장, 9장 |
| 6/4 (3일차) | 클라이언트 + 추가기능 ✅ | client/main.c (CLI 메뉴, 이벤트 수신), 로그, 멜로디 | 3-4장, 5-6장 |
| 6/5 (4일차) | 마무리 | 통합 테스트, gdb 디버깅, README, 제출 파일 구성 | 1장 |
