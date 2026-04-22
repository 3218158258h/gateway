#ifndef __APP_PROTOCOL_CONFIG_H__
#define __APP_PROTOCOL_CONFIG_H__

#include "app_message.h"

/* 协议配置文件路径 */
#define APP_PROTOCOL_CONFIG_FILE "config/protocols.ini"
/* 协议名称最大长度 */
#define APP_PROTOCOL_NAME_MAX_LEN 64
/* 下行指令前缀最大长度 */
#define APP_PROTOCOL_CMD_PREFIX_MAX_LEN 16
/* 帧头/帧尾最大字节数 */
#define APP_PROTOCOL_MAX_FRAME_BYTES 8
/* 单条设备配置指令模板最大长度（含占位符，存储形式） */
#define APP_PROTOCOL_INIT_CMD_MAX_LEN 64
/* 设备配置指令最大条数 */
#define APP_PROTOCOL_MAX_INIT_CMDS 8

/**
 * @brief 设备协议配置结构体
 *
 * 描述一种设备私有协议的完整参数，包括：
 *   - 帧格式（帧头、帧尾、ID长度）
 *   - 下行指令前缀（用于组装发往设备的命令）
 *   - 设备层初始化流程：状态检查指令 + 配置指令序列
 *
 * 配置指令模板支持如下占位符，由设备层在运行时从 gateway.ini 读取并替换：
 *   {m_addr}    - 模块地址（4字符十六进制，来自 [bluetooth].m_addr）
 *   {net_id}    - 网络ID（4字符十六进制，来自 [bluetooth].net_id）
 *   {baud_code} - 波特率编码字符（来自 SerialBaudRate 枚举值，如 '8' 表示 115200）
 *
 * 指令模板中的 \r\n 字面量在加载时自动转义为实际的 CR LF 字节。
 */
typedef struct BluetoothProtocolConfigStruct {
    ConnectionType connection_type;                                              /* 设备连接类型（ble_mesh / lora / none） */
    unsigned char  frame_header[APP_PROTOCOL_MAX_FRAME_BYTES];                  /* 帧头字节序列 */
    int            frame_header_len;                                             /* 帧头有效字节数 */
    unsigned char  frame_tail[APP_PROTOCOL_MAX_FRAME_BYTES];                    /* 帧尾字节序列（0表示无帧尾校验） */
    int            frame_tail_len;                                               /* 帧尾有效字节数 */
    int            id_len;                                                       /* 设备ID字节数 */
    char           mesh_cmd_prefix[APP_PROTOCOL_CMD_PREFIX_MAX_LEN];            /* 下行Mesh指令前缀（如 "AT+MESH"） */
    char           status_cmd[APP_PROTOCOL_INIT_CMD_MAX_LEN];                   /* 状态检查指令（空字符串表示跳过检查） */
    char           init_cmds[APP_PROTOCOL_MAX_INIT_CMDS][APP_PROTOCOL_INIT_CMD_MAX_LEN]; /* 设备配置指令序列（按序逐条发送） */
    int            init_cmds_count;                                              /* 配置指令有效条数 */
} BluetoothProtocolConfig;

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
