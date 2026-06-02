#ifndef SEGMENT_H
#define SEGMENT_H

int  segment_init(int gpio_pins[8]);
void segment_display(int digit);   /* 0~9, -1=꺼짐 */
void segment_cleanup(void);

#endif /* SEGMENT_H */
