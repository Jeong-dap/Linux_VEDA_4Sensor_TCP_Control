#ifndef LED_H
#define LED_H

int  led_init(int gpio_pin);
void led_on(void);
void led_off(void);
void led_set_brightness(int level);   /* BRIGHTNESS_LOW=1 / MID=2 / HIGH=3 */
void led_cleanup(void);

#endif /* LED_H */
