#include <wiringPi.h>
#include <softTone.h>
#include <unistd.h>
#include "buzzer.h"

#define BUZZER_FREQ 1000   /* Hz — 경보음 주파수 */

static int          g_pin  = -1;
static volatile int g_stop = 0;

/* ── Für Elise — Beethoven (도입부) ────────────────────────────── */
typedef struct { int freq; int ms; } Note;

/* ♩ = 100 BPM */
#define Q    600   /* quarter        4분음 */
#define E    300   /* eighth         8분음 */
#define DQ   900   /* dotted quarter 점4분 (Q+E) */
#define H   1200   /* half           2분음 */
#define R      0   /* rest           쉼표  */

/* A minor */
#define C4  262
#define E4  330
#define Gs4 415
#define A4  440
#define B4  494
#define C5  523
#define D5  587
#define Ds5 622
#define E5  659

static const Note fur_elise[] = {
    /* ── 주제 A (1회) ── */
    {E5,E}, {Ds5,E}, {E5,E}, {Ds5,E}, {E5,E}, {B4,E}, {D5,E}, {C5,E},
    {A4,Q}, {R,E},   {C4,E}, {E4,E},  {A4,E},
    {B4,Q}, {R,E},   {E4,E}, {Gs4,E}, {B4,E},
    {C5,Q}, {R,E},   {E4,E}, {E5,E},  {Ds5,E},

    /* ── 주제 A (2회) ── */
    {E5,E}, {Ds5,E}, {E5,E}, {B4,E},  {D5,E}, {C5,E},
    {A4,Q}, {R,E},   {C4,E}, {E4,E},  {A4,E},
    {B4,Q}, {R,E},   {E4,E}, {C5,E},  {B4,E},
    {A4,DQ},
};

int buzzer_init(int gpio_pin) {
    if (wiringPiSetupGpio() < 0) return -1;
    g_pin = gpio_pin;
    softToneCreate(g_pin);
    return 0;
}

void buzzer_on(void) {
    softToneWrite(g_pin, BUZZER_FREQ);
}

void buzzer_off(void) {
    g_stop = 1;
    softToneWrite(g_pin, 0);
}

void buzzer_play_melody(void) {
    g_stop = 0;
    int n = (int)(sizeof(fur_elise) / sizeof(fur_elise[0]));
    for (int i = 0; i < n && !g_stop; i++) {
        softToneWrite(g_pin, fur_elise[i].freq);
        usleep((useconds_t)fur_elise[i].ms * 950);
        softToneWrite(g_pin, 0);
        usleep((useconds_t)fur_elise[i].ms * 50);
    }
    softToneWrite(g_pin, 0);
}

void buzzer_stop_melody(void) {
    g_stop = 1;
    softToneWrite(g_pin, 0);
}

void buzzer_cleanup(void) {
    g_stop = 1;
    softToneWrite(g_pin, 0);
}
