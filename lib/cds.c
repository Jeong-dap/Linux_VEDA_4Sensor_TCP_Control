#include <wiringPiI2C.h>
#include <pthread.h>
#include "cds.h"

#define PCF8591_CH0  0x00  /* AIN0 — 조도센서 채널 */

static int g_fd = -1;
static pthread_mutex_t g_sensor_lock = PTHREAD_MUTEX_INITIALIZER;

int sensor_init(int i2c_addr) {
    g_fd = wiringPiI2CSetup(i2c_addr);
    return (g_fd < 0) ? -1 : 0;
}

/* 0~255 반환 (높을수록 밝음) */
int sensor_read(void) {
    if (g_fd < 0) return -1;

    pthread_mutex_lock(&g_sensor_lock);
    int result = wiringPiI2CWrite(g_fd, PCF8591_CH0);
    if (result >= 0) {
        result = wiringPiI2CRead(g_fd);  /* 이전 변환값 버림 */
    }
    if (result >= 0) {
        result = wiringPiI2CRead(g_fd);  /* 현재 조도값 */
    }
    pthread_mutex_unlock(&g_sensor_lock);
    return result;
}

void sensor_cleanup(void) {}
