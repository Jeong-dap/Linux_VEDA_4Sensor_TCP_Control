#ifndef LIGHT_SENSOR_H
#define LIGHT_SENSOR_H

int  sensor_init(int gpio_pin);
int  sensor_read(void);   /* 0=정상, 1=차단 */
void sensor_cleanup(void);

#endif /* LIGHT_SENSOR_H */

