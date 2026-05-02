#ifndef __APP_LINK_ADAPTER_H__
#define __APP_LINK_ADAPTER_H__

#include "app_serial.h"
#include "app_device_layer.h"

/* 接口类型名称最大长度。 */
#define APP_INTERFACE_NAME_MAX_LEN 16

/**
 * @brief 接口层初始化：根据接口类型打开设备文件描述符
 *
 * 接口层负责建立通信通道与物理参数下发（串口/CAN/SPI/I2C），
 * 不负责私有协议初始化指令与帧编解码。
 *
 * @param device         串口设备结构体指针
 * @param device_path    设备文件路径（如 /dev/ttyS0）
 * @param interface_name 接口类型名称（"serial"/"uart"/"spi"/"i2c"/"can"）
 * @return 0 成功；-1 失败
 */
int app_link_adapter_init(SerialDevice *device, const char *device_path, const char *interface_name);

/**
 * @brief 协议应用入口：按协议名加载配置并完成设备层绑定
 *
 * 常规流程：接口初始化 -> 协议配置 -> 启动收发。
 * 本函数属于“协议配置”阶段，负责协议加载、初始化命令执行与收发钩子绑定。
 * 若 protocol_name 为空，表示该设备仅启用链路收发，不执行私有协议初始化。
 *
 * @param device         串口设备结构体指针（已由 app_link_adapter_init 初始化）
 * @param protocol_name  协议名称（对应 config/protocols.ini 中 [protocol.<name>] 节）
 * @return 0 成功；-1 失败
 */
int app_link_adapter_apply_protocol(SerialDevice *device, const char *protocol_name);

#endif
