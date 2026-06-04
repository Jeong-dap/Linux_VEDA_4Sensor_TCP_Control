#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

typedef struct {
    uint8_t type;
    uint8_t device;
    uint8_t action;
    uint8_t value;
} Message;

/* 메시지 타입 */
#define MSG_CMD     0x01   /* Client → Server: 장치 제어 */
#define MSG_EVENT   0x02   /* Server → Client: 이벤트 알림 */
#define MSG_RESP    0x03   /* Server → Client: 명령 응답 */
#define MSG_QUERY   0x04   /* Client → Server: 상태 조회 */

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

/* LED 밝기 (ACT_SET_BRIGHTNESS의 value) */
#define BRIGHTNESS_LOW  0x01   /* duty cycle 25% */
#define BRIGHTNESS_MID  0x02   /* duty cycle 50% */
#define BRIGHTNESS_HIGH 0x03   /* duty cycle 100% */

/* 응답 코드 (MSG_RESP의 value) */
#define RESP_OK     0x00
#define RESP_ERR    0x01
#define RESP_BUSY   0x02

/* 이벤트 코드 (MSG_EVENT의 action) */
#define EVT_INTRUSION       0x01   /* 침입 감지 */
#define EVT_ALARM_ON        0x02   /* 경보 활성화 */
#define EVT_COUNTDOWN       0x03   /* 카운트다운 진행 (value=현재숫자) */
#define EVT_ALARM_TRIGGERED 0x04   /* 카운트다운 0→부저 울림 */

#endif /* PROTOCOL_H */
