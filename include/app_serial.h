#if !defined(__APP_SERIAL_H__)
#define __APP_SERIAL_H__

#include "app_device.h"
#include <termios.h>

typedef enum
{
    SERIAL_BAUD_RATE_9600 = '4',
    SERIAL_BAUD_RATE_115200 = '8',
} SerialBaudRate;

typedef enum
{
    STOP_BITS_ONE = 0,
    STOP_BITS_TWO = CSTOPB,
} StopBits;

typedef enum
{
    PARITY_NONE = 0,
    PARITY_ODD = PARENB | PARODD,
    PARITY_EVEN = PARENB,
} Parity;

typedef struct SerialDeviceStruct
{
    Device super;             // 父类
    SerialBaudRate baud_rate; // 波特率
    StopBits stop_bits;       // 停止位
    Parity parity;            // 校验位
} SerialDevice;

/**
 * @brief 初始化串口设备
 *
 * @param serial_device 串口设备
 * @return int 0: 成功; -1: 失败
 */
int app_serial_init(SerialDevice *serial_device, char *filename);

/**
 * @brief 设置串口波特率
 *
 * @param serial_device 串口设备
 * @param baud_rate 波特率
 * @return int 0: 成功; -1: 失败
 */
int app_serial_setBaudRate(SerialDevice *serial_device, SerialBaudRate baud_rate);

/**
 * @brief 设置串口停止位
 *
 * @param serial_device 串口设备
 * @param stop_bits 停止位
 * @return int 0: 成功; -1: 失败
 */
int app_serial_setStopBits(SerialDevice *serial_device, StopBits stop_bits);

/**
 * @brief 设置串口校验位
 *
 * @param serial_device 串口设备
 * @param parity 校验位
 * @return int 0: 成功; -1: 失败
 */
int app_serial_setParity(SerialDevice *serial_device, Parity parity);

/**
 * @brief 刷新串口
 * 
 * @param serial_device 
 * @return int 
 */
int app_serial_flush(SerialDevice *serial_device);

/**
 * @brief 设置串口阻塞模式
 * 
 * @param Serial_device 
 * @param block_mode 1为阻塞，0为非阻塞
 * @return int 
 */
int app_serial_setBlockMode(SerialDevice *serial_device, int block_mode);

#endif // __APP_SERIAL_H__
