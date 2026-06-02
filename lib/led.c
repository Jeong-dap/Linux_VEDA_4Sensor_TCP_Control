#include <wiringPi.h>
#include <softPwm.h>
#include "led.h"
#include "protocol.h"

static int g_pin = -1;

int led_init(int gpio_pin) {
    if (wiringPiSetupGpio() < 0) return -1;
    g_pin = gpio_pin;
    pinMode(g_pin, OUTPUT);
    softPwmCreate(g_pin, 0, 100);
    return 0;
}

void led_on(void) {
    softPwmWrite(g_pin, 100);
}

void led_off(void) {
    softPwmWrite(g_pin, 0);
}

/* BRIGHTNESS_LOW=25%, MID=50%, HIGH=100% */
void led_set_brightness(int level) {
    int duty;
    switch (level) {
        case BRIGHTNESS_LOW:  duty = 25;  break;
        case BRIGHTNESS_MID:  duty = 50;  break;
        case BRIGHTNESS_HIGH: duty = 100; break;
        default:              duty = 0;   break;
    }
    softPwmWrite(g_pin, duty);
}

void led_cleanup(void) {
    softPwmWrite(g_pin, 0);
    softPwmStop(g_pin);
}
