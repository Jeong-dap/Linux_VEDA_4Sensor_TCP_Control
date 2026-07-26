#ifndef TEST_STUB_WIRING_PI_I2C_H
#define TEST_STUB_WIRING_PI_I2C_H

int wiringPiI2CSetup(int device_id);
int wiringPiI2CWrite(int fd, int data);
int wiringPiI2CRead(int fd);

#endif /* TEST_STUB_WIRING_PI_I2C_H */
