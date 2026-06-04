#include <wiringPiI2C.h>
#include "cds.h"

#define PCF8591_CH0  0x00  /* AIN0 — 조도센서 채널 */

static int g_fd = -1;

int sensor_init(int i2c_addr) {
    g_fd = wiringPiI2CSetup(i2c_addr);
    return (g_fd < 0) ? -1 : 0;
}

/* 0~255 반환 (높을수록 밝음) */
int sensor_read(void) {
    if (g_fd < 0) return 0;
    wiringPiI2CWrite(g_fd, PCF8591_CH0);
    wiringPiI2CRead(g_fd);         /* 이전 변환값 버림 */
    return wiringPiI2CRead(g_fd);  /* 현재 조도값 */
}

void sensor_cleanup(void) {}
