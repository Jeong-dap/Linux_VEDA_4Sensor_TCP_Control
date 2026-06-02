#ifndef BUZZER_H
#define BUZZER_H

int  buzzer_init(int gpio_pin);
void buzzer_on(void);
void buzzer_off(void);
void buzzer_play_melody(void);   /* Canon in D — blocking, interruptible */
void buzzer_stop_melody(void);
void buzzer_cleanup(void);

#endif /* BUZZER_H */
