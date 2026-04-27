#if !defined(__APP_BLUETOOTH_H__)
#define __APP_BLUETOOTH_H__

#include "app_serial.h"
#include "app_protocol_config.h"
#include "app_private_protocol.h"

/* 蓝牙数据包长度字段字节数。 */
#define BT_LENGTH_FIELD_SIZE    1
/* 蓝牙数据包最小有效数据长度。 */
#define BT_MIN_DATA_LEN         0

/**
 * @brief 设备层配置入口：按协议完成蓝牙/Mesh模块初始化流程
 *
 * 遵循固定的设备层生命周期：
 *   1. 加载设备运行时参数（m_addr、net_id、baud_rate）与协议配置
 *   2. 将串口切换至配置波特率（9600），非阻塞模式，刷新缓冲区
 *   3. 发送状态检查指令（如 AT）确认模块处于 AT 命令模式
 *   4. 按顺序发送协议配置文件中定义的 init_cmds，完成设备参数配置
 *   5. 切换至工作波特率，阻塞模式，完成进入正常工作阶段
 *
 * @param serial_device  已初始化的串口设备指针（接口层已完成打开）
 * @param protocol_name  协议名称（对应 config/protocols.ini [protocol.<name>]）
 * @return 0 成功；-1 失败
 */
int app_bluetooth_setConnectionType(SerialDevice *serial_device, const char *protocol_name);
int app_bluetooth_set_protocol_config(SerialDevice *serial_device, const PrivateProtocolConfig *protocol);
void app_bluetooth_clear_context(int device_fd);

int app_bluetooth_sendCommand(SerialDevice *serial_device, const char *cmd);

/**
 * @brief 发送 AT 状态检查指令，验证模块是否处于 AT 命令模式
 * @param serial_device 串口设备指针
 * @return 0 模块就绪；-1 无应答或通信失败
 */
int app_bluetooth_status(SerialDevice *serial_device);

/**
 * @brief 通过 AT 指令设置模块通信波特率
 * @param serial_device 串口设备指针
 * @param baud_rate     目标波特率枚举值
 * @return 0 成功；-1 失败
 */
int app_bluetooth_setBaudRate(SerialDevice *serial_device, SerialBaudRate baud_rate);

/**
 * @brief 通过 AT 指令重置模块
 * @param serial_device 串口设备指针
 * @return 0 成功；-1 失败
 */
int app_bluetooth_reset(SerialDevice *serial_device);

/**
 * @brief 通过 AT 指令设置模块网络 ID
 * @param serial_device 串口设备指针
 * @param net_id        4字符网络ID字符串
 * @return 0 成功；-1 失败
 */
int app_bluetooth_setNetID(SerialDevice *serial_device, char *net_id);

/**
 * @brief 通过 AT 指令设置模块物理地址（主地址）
 * @param serial_device 串口设备指针
 * @param m_addr        4字符地址字符串
 * @return 0 成功；-1 失败
 */
int app_bluetooth_setMAddr(SerialDevice *serial_device, char *m_addr);

/**
 * @brief 接收后处理钩子：从原始串口流中拆帧，输出标准内部消息格式
 *
 * 按协议配置（frame_header/frame_tail/id_len/ack_frame/nack_frame）解析帧边界，
 * 提取设备ID和有效载荷，组装为内部消息格式（1字节连接类型 + 1字节ID长度 + 1字节数据长度 + ID + 数据）。
 *
 * @param device 设备指针
 * @param ptr    读缓冲区（输入原始字节，输出内部消息格式；原地改写）
 * @param len    输入：读取字节数；输出：内部消息有效字节数（0 表示帧不完整）
 * @return 0（始终成功，异常时输出 len=0）
 */
int app_bluetooth_postRead(Device *device, void *ptr, int *len);

/**
 * @brief 发送前处理钩子：将内部消息格式转换为设备私有帧格式
 *
 * 从内部消息格式（1字节类型 + 1字节ID长度 + 1字节数据长度 + ID + 数据）
 * 组装私有下行指令（mesh_cmd_prefix + ID + 数据 + "\r\n"），原地改写缓冲区。
 *
 * @param device 设备指针
 * @param ptr    发送缓冲区（输入内部消息格式，输出设备指令帧；原地改写）
 * @param len    输入：消息字节数；输出：指令帧字节数（0 表示无需发送）
 * @return 0（始终成功，异常时输出 len=0）
 */
int app_bluetooth_preWrite(Device *device, void *ptr, int *len);

#endif // __APP_BLUETOOTH_H__
