#ifndef __APP_LINK_ADAPTER_H__
#define __APP_LINK_ADAPTER_H__

#include "app_serial.h"
#include "app_device_layer.h"

/* 接口类型名称最大长度。 */
#define APP_INTERFACE_NAME_MAX_LEN 16

/**
 * @brief 接口层初始化：根据接口类型打开设备文件描述符
 *
 * 接口层只负责建立通信通道（如打开串口），不涉及设备层的
 * 帧格式、AT指令等协议细节。
 *
 * @param device         串口设备结构体指针
 * @param device_path    设备文件路径（如 /dev/ttyS0）
 * @param interface_name 接口类型名称（"serial"/"uart"/"spi"/"i2c"/"can"）
 * @return 0 成功；-1 失败
 */
int app_link_adapter_init(SerialDevice *device, const char *device_path, const char *interface_name);

/**
 * @brief 设备层配置：根据协议名称完成设备初始化指令序列与帧钩子绑定
 *
 * 设备层按固定流程运行：初始化（接口层已完成）→ 配置（发送 init_cmds）→ 正常工作。
 * 此函数完成"配置"阶段：从 config/protocols.ini 加载该设备的协议参数，
 * 按序发送初始化指令，并绑定收发钩子函数（postRead / preWrite）。
 *
 * @param device         串口设备结构体指针（已由 app_link_adapter_init 初始化）
 * @param protocol_name  协议名称（对应 config/protocols.ini 中 [protocol.<name>] 节）
 * @return 0 成功；-1 失败
 */
int app_link_adapter_apply_protocol(SerialDevice *device, const char *protocol_name);

#endif
