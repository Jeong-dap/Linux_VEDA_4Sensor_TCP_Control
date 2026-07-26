# TCP 통신 프로토콜 명세서

## 개요

- **프로토콜**: TCP 소켓
- **포트**: 8080
- **메시지 형식**: 4바이트 고정 크기 바이너리 구조체
- **전송**: `send(fd, &msg, sizeof(Message), 0)` / `recv(fd, &msg, sizeof(Message), 0)`
- **인코딩**: 바이너리 (1바이트 필드만 사용 — 엔디언 무관)

---

## 메시지 공통 구조 (Client ↔ Server)

```c
/* include/protocol.h */
typedef struct {
    uint8_t type;    // 메시지 종류 (MSG_CMD / MSG_QUERY / MSG_RESP / MSG_EVENT)
    uint8_t device;  // 대상 장치 (DEV_LED / DEV_BUZZER / DEV_SENSOR / DEV_SEGMENT / DEV_SYSTEM)
    uint8_t action;  // 수행 동작 (ACT_* / EVT_*)
    uint8_t value;   // 파라미터 — 없으면 0x00
} Message;
```

### type 코드

| 상수 | 값 | 방향 | 설명 |
|------|----|------|------|
| `MSG_CMD` | `0x01` | Client → Server | 장치 제어 명령 |
| `MSG_EVENT` | `0x02` | Server → Client | 서버 자동 이벤트 알림 |
| `MSG_RESP` | `0x03` | Server → Client | 명령 응답 |
| `MSG_QUERY` | `0x04` | Client → Server | 상태 조회 요청 |

### device 코드

| 상수 | 값 | 설명 |
|------|----|------|
| `DEV_SYSTEM` | `0x00` | 시스템 전체 |
| `DEV_LED` | `0x01` | LED |
| `DEV_BUZZER` | `0x02` | 부저 |
| `DEV_SENSOR` | `0x03` | 조도센서 |
| `DEV_SEGMENT` | `0x04` | 7세그먼트 |

### 응답 코드 (MSG_RESP의 value 필드)

| 상수 | 값 | 설명 |
|------|----|------|
| `RESP_OK` | `0x00` | 명령 처리 성공 |
| `RESP_ERR` | `0x01` | 명령 처리 실패 |
| `RESP_BUSY` | `0x02` | 장치가 사용 중 |

---

## 명령 (Client → Server)

### 1. LED 켜기 `ACT_ON`

> `MSG_CMD` / 응답 있음

**요청**

```
{ type=MSG_CMD, device=DEV_LED, action=ACT_ON, value=0x00 }
```

**응답 (성공)**

```
{ type=MSG_RESP, device=DEV_LED, action=ACT_ON, value=RESP_OK }
```

**응답 (실패)**

```
{ type=MSG_RESP, device=DEV_LED, action=ACT_ON, value=RESP_ERR }
```

---

### 2. LED 끄기 `ACT_OFF`

> `MSG_CMD` / 응답 있음

**요청**

```
{ type=MSG_CMD, device=DEV_LED, action=ACT_OFF, value=0x00 }
```

**응답 (성공)**

```
{ type=MSG_RESP, device=DEV_LED, action=ACT_OFF, value=RESP_OK }
```

---

### 3. LED 밝기 설정 `ACT_SET_BRIGHTNESS`

> `MSG_CMD` / 응답 있음

**요청**

```
{ type=MSG_CMD, device=DEV_LED, action=ACT_SET_BRIGHTNESS, value=BRIGHTNESS_MID }
```

| value 필드 | 상수 | 값 | 설명 |
|-----------|------|----|------|
| 최대 밝기 | `BRIGHTNESS_HIGH` | `0x03` | duty cycle 100% |
| 중간 밝기 | `BRIGHTNESS_MID` | `0x02` | duty cycle 50% |
| 최저 밝기 | `BRIGHTNESS_LOW` | `0x01` | duty cycle 25% |

**응답 (성공)**

```
{ type=MSG_RESP, device=DEV_LED, action=ACT_SET_BRIGHTNESS, value=RESP_OK }
```

---

### 4. 부저 켜기 `ACT_ON`

> `MSG_CMD` / 응답 있음

**요청**

```
{ type=MSG_CMD, device=DEV_BUZZER, action=ACT_ON, value=0x00 }
```

**응답 (성공)**

```
{ type=MSG_RESP, device=DEV_BUZZER, action=ACT_ON, value=RESP_OK }
```

---

### 5. 부저 끄기 `ACT_OFF`

> `MSG_CMD` / 응답 있음

**요청**

```
{ type=MSG_CMD, device=DEV_BUZZER, action=ACT_OFF, value=0x00 }
```

**응답 (성공)**

```
{ type=MSG_RESP, device=DEV_BUZZER, action=ACT_OFF, value=RESP_OK }
```

---

### 6. 멜로디 재생 `ACT_PLAY_MELODY`

> `MSG_CMD` / 응답 있음

**요청**

```
{ type=MSG_CMD, device=DEV_BUZZER, action=ACT_PLAY_MELODY, value=0x00 }
```

> 부저로 Für Elise 멜로디를 재생한다. 재생 중 `ACT_OFF` 또는 새 `ACT_PLAY_MELODY` 수신 시 즉시 중단된다.

**응답 (성공)**

```
{ type=MSG_RESP, device=DEV_BUZZER, action=ACT_PLAY_MELODY, value=RESP_OK }
```

---

### 7. 카운트다운 시작 `ACT_SET_NUMBER`

> `MSG_CMD` / 응답 있음 + 이후 EVT_COUNTDOWN 이벤트 연속 수신

**요청**

```
{ type=MSG_CMD, device=DEV_SEGMENT, action=ACT_SET_NUMBER, value=7 }
```

| value 필드 | 조건 |
|-----------|------|
| 표시할 숫자 | `1` ~ `9` (0x01 ~ 0x09) |

> 카운트다운 진행 중 새 명령 수신 시 기존 스레드를 `pthread_cancel` + `pthread_join` 후 새 값으로 재시작한다.

**응답 (성공)**

```
{ type=MSG_RESP, device=DEV_SEGMENT, action=ACT_SET_NUMBER, value=RESP_OK }
```

> 응답 이후 서버는 1초마다 `EVT_COUNTDOWN` 이벤트를 자동 전송한다. → [12. 카운트다운 진행](#12-카운트다운-진행-evt_countdown) 참고

---

### 8. 카운트다운 중지 `ACT_OFF`

> `MSG_CMD` / 응답 있음

**요청**

```
{ type=MSG_CMD, device=DEV_SEGMENT, action=ACT_OFF, value=0x00 }
```

> 진행 중인 카운트다운 스레드를 즉시 취소한다. 카운트다운이 없으면 RESP_OK 반환.

**응답 (성공)**

```
{ type=MSG_RESP, device=DEV_SEGMENT, action=ACT_OFF, value=RESP_OK }
```

---

### 9. 조도 수치 조회 `ACT_GET_LUX`

> `MSG_QUERY` / 응답 있음

**요청**

```
{ type=MSG_QUERY, device=DEV_SENSOR, action=ACT_GET_LUX, value=0x00 }
```

> 서버가 PCF8591T ADC에서 현재 조도값을 즉시 읽어 반환한다.

**응답**

```
{ type=MSG_RESP, device=DEV_SENSOR, action=ACT_GET_LUX, value=187 }
```

| value 범위 | 의미 |
|-----------|------|
| `0` ~ `169` | 밝음 (정상) |
| `175` ~ `255` | 어두움 (빛 꺼짐 감지) |

---

### 10. 전체 경보 해제 `ACT_ALARM_OFF`

> `MSG_CMD` / 응답 있음

**요청**

```
{ type=MSG_CMD, device=DEV_SYSTEM, action=ACT_ALARM_OFF, value=0x00 }
```

> 서버는 부저 OFF, LED OFF, `alarm_active = 0` 처리 후 응답한다.

**응답 (성공)**

```
{ type=MSG_RESP, device=DEV_SYSTEM, action=ACT_ALARM_OFF, value=RESP_OK }
```

---

### 11. 상태 조회 `ACT_GET_STATUS`

> `MSG_QUERY` / 응답 있음

**요청**

```
{ type=MSG_QUERY, device=DEV_SENSOR, action=ACT_GET_STATUS, value=0x00 }
```

| device 필드 | 조회 가능 장치 |
|------------|--------------|
| `DEV_SENSOR` | 조도센서 (차단 여부) |
| `DEV_LED` | LED 현재 상태 |
| `DEV_BUZZER` | 부저 현재 상태 |

**응답 (성공 — 조도센서 예시)**

```
{ type=MSG_RESP, device=DEV_SENSOR, action=ACT_GET_STATUS, value=0x01 }
```

| value 값 | 의미 |
|---------|------|
| `0x00` | 정상 (빛 감지 중) |
| `0x01` | 차단 감지 (침입) |

---

## 이벤트 (Server → Client)

> 이벤트는 클라이언트의 요청 없이 서버가 자동으로 전송한다. 클라이언트는 별도 수신 스레드(pthread)로 비동기 처리한다.

### 12. 카운트다운 진행 `EVT_COUNTDOWN`

> `ACT_SET_NUMBER` 명령 수신 후 서버 세그먼트 스레드가 1초마다 전송

**이벤트**

```
{ type=MSG_EVENT, device=DEV_SEGMENT, action=EVT_COUNTDOWN, value=7 }
{ type=MSG_EVENT, device=DEV_SEGMENT, action=EVT_COUNTDOWN, value=6 }
...
{ type=MSG_EVENT, device=DEV_SEGMENT, action=EVT_COUNTDOWN, value=0 }
```

| value 필드 | 설명 |
|-----------|------|
| `1` ~ `N` | 카운트다운 현재 값 |
| `0x00` | 카운트다운 완료 → 부저 울림 트리거 |

---

### 13. 침입 감지 `EVT_INTRUSION`

> 조도센서 조도값 ≥ 175가 연속 3회 감지될 때 자동 전송. 경보는 **5초 후 자동 해제**된다.

**이벤트**

```
{ type=MSG_EVENT, device=DEV_SENSOR, action=EVT_INTRUSION, value=0x00 }
```

---

### 14. 경보 활성화 `EVT_ALARM_ON`

> 침입 감지 직후 LED가 최대 밝기로 점등될 때 자동 전송

**이벤트**

```
{ type=MSG_EVENT, device=DEV_LED, action=EVT_ALARM_ON, value=0x00 }
```

---

### 15. 부저 울림 `EVT_ALARM_TRIGGERED`

> 카운트다운 0 도달 시 자동 전송

**이벤트**

```
{ type=MSG_EVENT, device=DEV_BUZZER, action=EVT_ALARM_TRIGGERED, value=0x00 }
```

---

## 공통 오류 응답

**명령 처리 실패**

```
{ type=MSG_RESP, device=<장치>, action=<동작>, value=RESP_ERR }
```

**장치 사용 중 (busy)**

```
{ type=MSG_RESP, device=<장치>, action=<동작>, value=RESP_BUSY }
```

---

## 전체 흐름 요약

```
클라이언트 접속 (TCP connect → 3-Way Handshake)
    ↓
LED 켜기        (ACT_ON  / DEV_LED)
LED 끄기        (ACT_OFF / DEV_LED)
LED 밝기 설정   (ACT_SET_BRIGHTNESS / DEV_LED — value: LOW·MID·HIGH)
부저 켜기       (ACT_ON  / DEV_BUZZER)
부저 끄기       (ACT_OFF / DEV_BUZZER)
멜로디 재생     (ACT_PLAY_MELODY / DEV_BUZZER)
상태 조회       (ACT_GET_STATUS / MSG_QUERY — DEV_SENSOR·DEV_LED·DEV_BUZZER)
조도 수치 조회  (ACT_GET_LUX / MSG_QUERY / DEV_SENSOR — 0~255)
    ↓
[자동] 침입 감지 이벤트     EVT_INTRUSION  ← 조도값 ≥ 175, 연속 3회
[자동] 경보 활성화 이벤트   EVT_ALARM_ON   ← LED HIGH + 부저 ON
[자동] 5초 후 경보 자동 해제
    ↓
카운트다운 시작  (ACT_SET_NUMBER / DEV_SEGMENT — value: 1~9)
카운트다운 중지  (ACT_OFF / DEV_SEGMENT)
    ↓
[자동] 카운트다운 이벤트    EVT_COUNTDOWN       ← 서버 1초마다 자동 전송
[자동] 부저 울림 이벤트     EVT_ALARM_TRIGGERED ← 카운트다운 0 도달 시
    ↓
클라이언트 종료 (SIGINT → close(fd) → exit)
```

> **다중 클라이언트**: 이벤트는 연결된 최대 4개 클라이언트에 broadcast되고,
> 명령·조회 응답은 요청한 클라이언트에만 전송된다.

---

## 헤더 파일 전체 (include/protocol.h)

```c
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

/* 메시지 구조체 */
typedef struct {
    uint8_t type;
    uint8_t device;
    uint8_t action;
    uint8_t value;
} Message;

/* 메시지 타입 */
#define MSG_CMD     0x01
#define MSG_EVENT   0x02
#define MSG_RESP    0x03
#define MSG_QUERY   0x04

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
#define ACT_PLAY_MELODY     0x07   /* 부저 멜로디 재생 */
#define ACT_GET_LUX         0x08   /* 조도 수치 조회 (0~255) */

/* LED 밝기 */
#define BRIGHTNESS_HIGH 0x03
#define BRIGHTNESS_MID  0x02
#define BRIGHTNESS_LOW  0x01

/* 응답 코드 */
#define RESP_OK     0x00
#define RESP_ERR    0x01
#define RESP_BUSY   0x02

/* 이벤트 코드 */
#define EVT_INTRUSION       0x01
#define EVT_ALARM_ON        0x02
#define EVT_COUNTDOWN       0x03
#define EVT_ALARM_TRIGGERED 0x04

#endif /* PROTOCOL_H */
```
