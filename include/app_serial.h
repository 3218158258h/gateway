#if !defined(__APP_SERIAL_H__)
#define __APP_SERIAL_H__

#include "app_device.h"
#include <termios.h>
#include <stddef.h>

typedef enum
{
    /* 物理接口类型：同一套设备层可以挂到不同底层接口上。 */
    APP_INTERFACE_SERIAL = 0,
    APP_INTERFACE_UART = 0,
    APP_INTERFACE_SPI,
    APP_INTERFACE_I2C,
    APP_INTERFACE_CAN
} AppInterfaceType;

typedef enum
{
    /* 波特率枚举值直接映射到协议配置中的编码字符。 */
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

typedef struct PhysicalTransportConfigStruct
{
    AppInterfaceType interface_type;  /* 物理接口类型。 */
    char device_path[256];            /* 设备路径。 */
    struct
    {
        SerialBaudRate baud_rate;     /* 波特率。 */
        StopBits stop_bits;           /* 停止位。 */
        Parity parity;                /* 校验位。 */
        int block_mode;               /* 1=阻塞，0=非阻塞。 */
    } uart;
    struct
    {
        unsigned int bitrate;         /* CAN 比特率。 */
        int loopback;                 /* 回环模式。 */
        int listen_only;              /* 仅监听。 */
        int fd_mode;                  /* CAN FD 模式。 */
    } can;
    struct
    {
        unsigned int clock_hz;        /* SPI 时钟频率。 */
        unsigned int mode;            /* SPI 模式。 */
        unsigned int bits_per_word;   /* 每字数据位数。 */
        int lsb_first;                /* 位序。 */
        unsigned int chip_select;     /* 片选号。 */
    } spi;
    struct
    {
        unsigned int bus_speed_hz;    /* I2C 总线速率。 */
        unsigned short address;       /* 从设备地址。 */
        int ten_bit_address;          /* 10 位地址模式。 */
        int clock_stretching;         /* 时钟拉伸。 */
    } i2c;
} PhysicalTransportConfig;

typedef struct SerialDeviceStruct
{
    Device super;                      /* 设备父类。 */
    PhysicalTransportConfig transport;  /* 物理传输配置。 */
} SerialDevice;

/**
 * @brief 将接口名称转换为物理接口类型
 *
 * @param interface_name 接口名（serial/uart/spi/i2c/can）
 * @return 接口类型；未知时返回超出枚举范围的值
 */
AppInterfaceType app_transport_string_to_interface(const char *interface_name);

/**
 * @brief 将物理接口类型转换为字符串
 *
 * @param interface_type 接口类型
 * @return 接口名
 */
const char *app_transport_interface_to_string(AppInterfaceType interface_type);

/**
 * @brief 初始化物理接口配置为默认值
 *
 * @param serial_device 设备对象
 */
void app_transport_config_init(SerialDevice *serial_device);

/**
 * @brief 从配置文件加载物理接口参数
 *
 * @param serial_device 设备对象
 * @param config_file 配置文件路径
 * @param section_name 配置节名
 * @return 0 成功；-1 失败
 */
int app_transport_config_load(SerialDevice *serial_device, const char *config_file, const char *section_name);

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
