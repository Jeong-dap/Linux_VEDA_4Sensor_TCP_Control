#ifndef CDS_H
#define CDS_H

int  sensor_init(int i2c_addr);  /* 기본 주소: 0x48 */
int  sensor_read(void);          /* 0~255, 높을수록 밝음 */
void sensor_cleanup(void);

#endif /* CDS_H */
