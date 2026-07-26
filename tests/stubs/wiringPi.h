#ifndef TEST_STUB_WIRING_PI_H
#define TEST_STUB_WIRING_PI_H

#define OUTPUT 1
#define LOW 0
#define HIGH 1

int wiringPiSetupGpio(void);
void pinMode(int pin, int mode);
void digitalWrite(int pin, int value);

#endif /* TEST_STUB_WIRING_PI_H */
