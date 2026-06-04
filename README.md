# 스마트 경보 시스템

TCP 소켓으로 라즈베리파이4에 연결된 하드웨어를 우분투 클라이언트(CLI) 및 웹 브라우저에서 원격 제어·모니터링하는 C 프로그램.

---

## 시스템 구성

```
브라우저 ←── WebSocket:60000 ──→ Node.js (web/server.js)
                                          │
                              TCP:8080    │
                                          ▼
CLI 클라이언트 (alarm_client) ←── TCP:8080 ──→ 라즈베리파이4 C 서버 (alarm_server)
                                                        │
                                                   GPIO / I2C
                                          ┌────┬────┬────┬────┐
                                         LED  부저 CDS  7세그
```

---

## 하드웨어

| 장치 | 연결 방식 | 핀 |
|------|-----------|-----|
| LED | softPwm (BCM) | GPIO 18 |
| 부저 | softTone (BCM) | GPIO 23 |
| 조도센서 (PCF8591T) | I2C | SDA(핀3) / SCL(핀5) |
| 7세그먼트 | 디지털 출력 8핀 | GPIO 4,17,27,22,5,6,13,19 |

- 조도센서 I2C 주소: `0x48` / 조도값 0~255 (170 이상 = 빛 꺼짐 감지)
- 7세그먼트: Common Anode (HIGH=OFF)

---

## 디렉토리 구조

```
Project/
├── server/
│   ├── main.c       # 서버 진입점, dlopen 로드, 데몬화, TCP accept
│   ├── daemon.c     # fork+setsid 데몬화
│   ├── handler.c    # 장치 전담 스레드 + 클라이언트 핸들러
│   └── server.h     # 공유 구조체 + extern 선언
├── client/
│   └── main.c       # CLI 메뉴 루프, 이벤트 수신 스레드
├── web/
│   ├── server.js    # Node.js WebSocket↔TCP 브리지 (포트 60000)
│   ├── index.html   # 웹 대시보드
│   └── package.json
├── lib/
│   ├── led.c        → led.so
│   ├── buzzer.c     → buzzer.so
│   ├── cds.c        → cds.so
│   └── segment.c    → segment.so
├── include/
│   ├── protocol.h
│   ├── led.h / buzzer.h / cds.h / segment.h
├── docs/
│   ├── 기획서.md
│   └── 회로도.md
└── Makefile
```

---

## 빌드 및 배포

### 요구사항

- 우분투: `aarch64-linux-gnu-gcc` (크로스컴파일러)
- 라즈베리파이: WiringPi, Node.js 20+

### 빌드

```bash
# 우분투에서
make          # lib + server(ARM64) + client(x86_64) + deploy
```

- `make lib` / `make server` / `make client` 독립 빌드 가능
- `make CROSS= all` — 라즈베리파이에서 직접 빌드 시

### deploy 포함 항목

`make deploy` 실행 시 라즈베리파이로 자동 전송:
- `alarm_server`
- `lib/*.so`
- `web/server.js`, `web/index.html`, `web/package.json`

---

## 실행 방법

### 1. 라즈베리파이 — C 서버

```bash
# make deploy 시 자동 실행됨. 수동 실행:
./alarm_server
```

### 2. 라즈베리파이 — 웹 브리지

```bash
cd ~/Project/web
npm install    # 최초 1회
node server.js
```

### 3. 우분투 — CLI 클라이언트

```bash
./alarm_client 172.20.33.119
```

### 4. 브라우저 — 웹 UI

```
http://172.20.33.119:60000
```

---

## CLI 메뉴

```
[1] LED 제어         — ON / OFF / 밝기 3단계 (LOW·MID·HIGH)
[2] 부저 제어        — ON / OFF / 멜로디 재생 (Für Elise)
[3] 세그먼트 제어    — 1~9 카운트다운 시작 / 10 중지
[4] 상태 조회        — LED / 부저 / 센서 상태
[5] 조도 수치 확인   — 현재 조도값 (0~255)
[6] 도움말
[0] 종료 (Ctrl+C)
```

---

## 주요 기능

### 자동 경보 (서버 주도)

1. 센서 스레드가 100ms마다 조도값 폴링
2. 조도값 ≥ 170이 연속 3회 → 빛 꺼짐 감지
3. LED 최대 밝기 ON + 부저 ON
4. 모든 클라이언트에 `EVT_INTRUSION`, `EVT_ALARM_ON` 이벤트 전송
5. **5초 후 자동 해제**

### 세그먼트 카운트다운

- 클라이언트에서 1~9 전송 → 세그먼트에 숫자 표시 후 매초 감소
- 0 도달 시 부저 ON → 3초 후 자동 OFF
- 진행 중 중지 명령으로 즉시 취소 가능

### 멜로디 재생

- 부저로 Für Elise 재생 (`ACT_PLAY_MELODY`)
- `pthread_cancel` + cleanup_push로 언제든 중단 보장

### 다중 클라이언트

- C 서버가 최대 4개 클라이언트 동시 연결 지원
- 모든 이벤트·응답은 `broadcast_msg()`로 전체 공유
- CLI와 웹에서 동시 제어·모니터링 가능

---

## TCP 프로토콜

4바이트 고정 크기 메시지:

```c
typedef struct {
    uint8_t type;    // MSG_CMD / MSG_EVENT / MSG_RESP / MSG_QUERY
    uint8_t device;  // DEV_LED / DEV_BUZZER / DEV_SENSOR / DEV_SEGMENT
    uint8_t action;  // ACT_ON / ACT_OFF / ACT_SET_NUMBER / ACT_GET_LUX ...
    uint8_t value;   // 파라미터
} Message;
```

---

## 이벤트 로그

서버 동작 기록: `/tmp/alarm.log`

```
[2026-06-04 14:32:10] SERVER STARTED
[2026-06-04 14:32:15] INTRUSION DETECTED
[2026-06-04 14:32:15] ALARM ON (LED HIGH + BUZZER)
[2026-06-04 14:32:20] ALARM AUTO OFF (5s)
[2026-06-04 14:33:01] COUNTDOWN STARTED: 5
[2026-06-04 14:33:06] COUNTDOWN REACHED 0 - BUZZER TRIGGERED
[2026-06-04 14:33:09] BUZZER AUTO OFF (3s)
```

---

## 제출물

압축 파일명: `심화실습평가(리눅스 프로그래밍)_이름`
