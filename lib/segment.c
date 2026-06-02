#include <stdio.h>
#include <stdint.h>
#include <wiringPi.h>
#include "segment.h"

/*
 * 7세그먼트 핀 순서: gpio_pins[0~7] = a b c d e f g dp (BCM)
 * GPIO 4,17,27,22,5,6,13,19 → a~g+dp
 *
 * 세그먼트 ON=LOW 기준 (common anode)
 */

static int g_pins[8];

/* 0~9 digit → a b c d e f g (dp 항상 0) */
static const uint8_t SEG_MAP[10] = {
    0b11111100, /* 0: a b c d e f   */
    0b01100000, /* 1:   b c         */
    0b11011010, /* 2: a b   d e   g */
    0b11110010, /* 3: a b c d     g */
    0b01100110, /* 4:   b c     f g */
    0b10110110, /* 5: a   c d   f g */
    0b10111110, /* 6: a   c d e f g */
    0b11100000, /* 7: a b c         */
    0b11111110, /* 8: a b c d e f g */
    0b11110110, /* 9: a b c d   f g */
};

int segment_init(int gpio_pins[8]) {
    if (wiringPiSetupGpio() < 0) return -1;
    for (int i = 0; i < 8; i++) {
        g_pins[i] = gpio_pins[i];
        pinMode(g_pins[i], OUTPUT);
        digitalWrite(g_pins[i], HIGH);  /* 전체 끄기 (common anode: HIGH=OFF) */
    }
    return 0;
}

void segment_display(int digit) {
    if (digit < 0) {
        for (int i = 0; i < 8; i++)
            digitalWrite(g_pins[i], HIGH);  /* 전체 끄기 */
        return;
    }
    if (digit > 9) return;

    uint8_t pattern = SEG_MAP[digit];
    for (int i = 0; i < 8; i++) {
        int bit = (pattern >> (7 - i)) & 0x01;
        digitalWrite(g_pins[i], !bit);  /* common anode: 비트 반전 */
    }
}

void segment_cleanup(void) {
    for (int i = 0; i < 8; i++)
        digitalWrite(g_pins[i], HIGH);  /* 전체 끄기 */
}
