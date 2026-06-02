#include <wiringPi.h>
#include "light_sensor.h"

static int g_pin1 = -1;
static int g_pin2 = -1;

int sensor_init(int gpio_pin1, int gpio_pin2) {
    if (wiringPiSetupGpio() < 0) return -1;
    g_pin1 = gpio_pin1;
    g_pin2 = gpio_pin2;
    pinMode(g_pin1, INPUT);
    pinMode(g_pin2, INPUT);
    return 0;
}

/* 0=정상, 1=차단(어느 하나라도 차단 시) */
int sensor_read(void) {
    return digitalRead(g_pin1) || digitalRead(g_pin2);
}

void sensor_cleanup(void) {
    /* 입력 핀은 별도 정리 불필요 */
}
