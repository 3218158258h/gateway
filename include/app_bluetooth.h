#if !defined(__APP_BLUETOOTH_H__)
#define __APP_BLUETOOTH_H__

#include "app_message.h"
#include "app_serial.h"

/* 蓝牙数据包格式常量 */
#define BT_FRAME_HEADER_SIZE    2   /* 帧头字节数: F1 DD */
#define BT_LENGTH_FIELD_SIZE    1   /* 长度字段字节数 */
#define BT_DEVICE_ID_SIZE       2   /* 设备ID字节数 */
#define BT_MIN_DATA_LEN         0   /* 最小数据长度 */

/* 长度字段的最小值 = 设备ID大小（只有ID，没有数据的情况） */
#define BT_PAYLOAD_MIN_LEN     BT_DEVICE_ID_SIZE   // = 2

/* 整个数据包的最小字节数 = 帧头(2) + 长度(1) + ID(2) = 5 */
#define BT_PACKET_MIN_SIZE     (BT_FRAME_HEADER_SIZE + BT_LENGTH_FIELD_SIZE + BT_DEVICE_ID_SIZE)


int app_bluetooth_setConnectionType(SerialDevice *serial_device);

int app_bluetooth_status(SerialDevice *serial_device);
int app_bluetooth_setBaudRate(SerialDevice *serial_device, SerialBaudRate baud_rate);
int app_bluetooth_reset(SerialDevice *serial_device);
int app_bluetooth_setNetID(SerialDevice *serial_device, char *net_id);
int app_bluetooth_setMAddr(SerialDevice *serial_device, char *m_addr);

int app_bluetooth_postRead(Device *device, void *ptr, int *len);
int app_bluetooth_preWrite(Device *device, void *ptr, int *len);

#endif // __APP_BLUETOOTH_H__
