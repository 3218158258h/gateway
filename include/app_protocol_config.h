#ifndef __APP_PROTOCOL_CONFIG_H__
#define __APP_PROTOCOL_CONFIG_H__

#include "app_private_protocol.h"

/* 协议配置文件路径 */
#define APP_PROTOCOL_CONFIG_FILE "config/protocols.ini"
/* 协议名称最大长度 */
#define APP_PROTOCOL_NAME_MAX_LEN 64

/**
 * @brief 从协议配置文件加载蓝牙/无线设备协议参数
 *
 * 读取 config/protocols.ini 中 [protocol.<protocol_name>] 节的所有字段，
 * 包括帧格式、设备层初始化指令序列等。加载失败时使用内置默认值。
 *
 * @param protocol_name 协议名称（对应 protocols.ini 中 [protocol.<name>] 节）
 * @param config        输出协议配置结构体指针
 * @return 0 成功；-1 参数错误
 */
int app_protocol_load_bluetooth(const char *protocol_name, BluetoothProtocolConfig *config);

#endif
