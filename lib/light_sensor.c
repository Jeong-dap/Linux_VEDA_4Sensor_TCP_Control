#include <wiringPi.h>
#include "light_sensor.h"

static int g_pin = -1;

int sensor_init(int gpio_pin) {
    if (wiringPiSetupGpio() < 0) return -1;
    g_pin = gpio_pin;
    pinMode(g_pin, INPUT);
    return 0;
}

/* 0=정상, 1=차단 */
int sensor_read(void) {
    return digitalRead(g_pin);
}

void sensor_cleanup(void) {
    /* 입력 핀은 별도 정리 불필요 */
}
