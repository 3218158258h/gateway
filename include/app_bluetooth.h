#if !defined(__APP_BLUETOOTH_H__)
#define __APP_BLUETOOTH_H__

#include "app_serial.h"
#include "app_protocol_config.h"

/* 蓝牙数据包格式常量 */
#define BT_LENGTH_FIELD_SIZE    1   /* 长度字段字节数 */
#define BT_MIN_DATA_LEN         0   /* 最小数据长度 */

int app_bluetooth_setConnectionType(SerialDevice *serial_device, const char *protocol_name);

int app_bluetooth_status(SerialDevice *serial_device);
int app_bluetooth_setBaudRate(SerialDevice *serial_device, SerialBaudRate baud_rate);
int app_bluetooth_reset(SerialDevice *serial_device);
int app_bluetooth_setNetID(SerialDevice *serial_device, char *net_id);
int app_bluetooth_setMAddr(SerialDevice *serial_device, char *m_addr);

int app_bluetooth_postRead(Device *device, void *ptr, int *len);
int app_bluetooth_preWrite(Device *device, void *ptr, int *len);

#endif // __APP_BLUETOOTH_H__
