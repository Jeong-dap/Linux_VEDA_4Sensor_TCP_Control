#ifndef TEST_STUB_SOFT_PWM_H
#define TEST_STUB_SOFT_PWM_H

int softPwmCreate(int pin, int initial_value, int range);
void softPwmWrite(int pin, int value);
void softPwmStop(int pin);

#endif /* TEST_STUB_SOFT_PWM_H */
