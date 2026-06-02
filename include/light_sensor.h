#ifndef LIGHT_SENSOR_H
#define LIGHT_SENSOR_H

int  sensor_init(int gpio_pin1, int gpio_pin2);
int  sensor_read(void);   /* 0=정상, 1=차단(어느 하나라도) */
void sensor_cleanup(void);

#endif /* LIGHT_SENSOR_H */
