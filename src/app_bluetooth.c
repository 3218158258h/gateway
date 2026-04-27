#include "../include/app_bluetooth.h"
#include "../include/app_device_layer.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "../include/app_config.h"
#include "../thirdparty/log.c/log.h"

#define READ_BUFFER_SIZE 256
#define BT_ADDR_STR_LEN 4
#define DEFAULT_BT_MADDR "0001"
#define DEFAULT_BT_NETID "1111"
#define DEFAULT_BT_WORK_BAUD 115200
#define BT_CONTEXT_GROW_STEP 8

typedef struct {
    int device_fd;
    unsigned char read_buffer[READ_BUFFER_SIZE];
    int read_buffer_len;
    BluetoothProtocolConfig protocol;
} BluetoothContext;

static BluetoothContext *bt_contexts = NULL;
static int bt_context_count = 0;
static int bt_context_capacity = 0;

/* 确保 bt_contexts 数组容量足够容纳下一个上下文，必要时扩容 */
static int app_bluetooth_ensureContextCapacity(void)
{
    if (bt_context_count < bt_context_capacity) {
        return 0;
    }

    int new_capacity = bt_context_capacity + BT_CONTEXT_GROW_STEP;
    BluetoothContext *new_contexts = realloc(bt_contexts, (size_t)new_capacity * sizeof(BluetoothContext));
    if (!new_contexts) {
        log_error("Bluetooth context allocation failed");
        return -1;
    }

    memset(new_contexts + bt_context_capacity, 0, (size_t)(new_capacity - bt_context_capacity) * sizeof(BluetoothContext));
    bt_contexts = new_contexts;
    bt_context_capacity = new_capacity;
    return 0;
}

/* 按 device_fd 查找或新建运行时上下文（含读缓冲区 + 协议参数），对应设备层"初始化"阶段 */
static BluetoothContext* get_or_create_context(int device_fd, const BluetoothProtocolConfig *protocol) {
    for (int i = 0; i < bt_context_count; i++) {
        if (bt_contexts[i].device_fd == device_fd) {
            if (protocol) {
                bt_contexts[i].protocol = *protocol;
            }
            return &bt_contexts[i];
        }
    }

    if (app_bluetooth_ensureContextCapacity() == 0) {
        bt_contexts[bt_context_count].device_fd = device_fd;
        bt_contexts[bt_context_count].read_buffer_len = 0;
        if (protocol) {
            bt_contexts[bt_context_count].protocol = *protocol;
        } else {
            memset(&bt_contexts[bt_context_count].protocol, 0, sizeof(BluetoothProtocolConfig));
        }
        return &bt_contexts[bt_context_count++];
    }

    return NULL;
}

/* 按 device_fd 查找已存在的运行时上下文，不存在则返回 NULL */
static BluetoothContext* find_context(int device_fd) {
    for (int i = 0; i < bt_context_count; i++) {
        if (bt_contexts[i].device_fd == device_fd) {
            return &bt_contexts[i];
        }
    }
    return NULL;
}

void app_bluetooth_clear_context(int device_fd)
{
    for (int i = 0; i < bt_context_count; i++) {
        if (bt_contexts[i].device_fd == device_fd) {
            if (i < bt_context_count - 1) {
                memmove(&bt_contexts[i], &bt_contexts[i + 1],
                        (size_t)(bt_context_count - i - 1) * sizeof(BluetoothContext));
            }
            bt_context_count--;
            if (bt_context_count == 0) {
                free(bt_contexts);
                bt_contexts = NULL;
                bt_context_capacity = 0;
            }
            return;
        }
    }
}

int app_bluetooth_set_protocol_config(SerialDevice *serial_device, const PrivateProtocolConfig *protocol)
{
    if (!serial_device || !protocol || serial_device->super.fd < 0) {
        return -1;
    }

    BluetoothContext *ctx = get_or_create_context(serial_device->super.fd, protocol);
    if (ctx) {
        ctx->read_buffer_len = 0;
        ctx->protocol = *(const BluetoothProtocolConfig *)protocol;
    }
    return ctx ? 0 : -1;
}

/* 从读缓冲区头部丢弃 n 个字节，将剩余数据前移 */
static void app_bluetooth_ignoreBuffer(BluetoothContext *ctx, int n)
{
    if (!ctx || n <= 0 || n > ctx->read_buffer_len) return;
    
    ctx->read_buffer_len -= n;
    if (ctx->read_buffer_len > 0) {
        memmove(ctx->read_buffer, ctx->read_buffer + n, ctx->read_buffer_len);
    }
}

/* 等待约 200ms 后读取响应，并基于协议定义匹配 ACK / NACK 字节序列。 */
static int app_bluetooth_waitResponse(SerialDevice *serial_device, const PrivateProtocolConfig *protocol)
{
    if (!serial_device || !protocol) {
        return -1;
    }

    /* 协议配置未提供 ACK 帧时，视为无需等待应答。 */
    if (protocol->ack_frame_len <= 0) {
        return 0;
    }

    usleep(200000);
    unsigned char buf[READ_BUFFER_SIZE];
    ssize_t len = read(serial_device->super.fd, buf, sizeof(buf));
    if (len <= 0) {
        return -1;
    }

    if (app_private_protocol_is_ack(protocol, buf, (int)len)) {
        return 0;
    }
    if (app_private_protocol_is_nack(protocol, buf, (int)len)) {
        return -1;
    }
    return -1;
}

/**
 * @brief 向设备发送一条 AT 指令并等待协议定义的 ACK 应答
 *
 * 统一处理 write() 返回值，避免部分写入或写失败被静默忽略导致 ACK 等待逻辑误判。
 * len 须与 cmd 缓冲区的实际字节数严格一致，包含末尾 \r\n。
 */
static int bluetooth_send_cmd_expect_ack(SerialDevice *serial_device, const void *cmd, size_t len)
{
    if (!serial_device || !cmd || len == 0) {
        return -1;
    }

    ssize_t written = write(serial_device->super.fd, cmd, len);
    if (written < 0) {
        log_error("Bluetooth write failed: %s", strerror(errno));
        return -1;
    }
    if ((size_t)written != len) {
        log_warn("Bluetooth partial write: %zd/%zu", written, len);
        return -1;
    }

    BluetoothContext *ctx = find_context(serial_device->super.fd);
    if (!ctx) {
        return -1;
    }

    return app_bluetooth_waitResponse(serial_device, &ctx->protocol);
}

int app_bluetooth_sendCommand(SerialDevice *serial_device, const char *cmd)
{
    if (!serial_device || !cmd) {
        return -1;
    }
    return bluetooth_send_cmd_expect_ack(serial_device, cmd, strlen(cmd));
}

int app_bluetooth_setConnectionType(SerialDevice *serial_device, const char *protocol_name)
{
    return app_device_layer_configure(serial_device, protocol_name);
}

/* 发送 "AT\r\n" 确认模块处于 AT 命令模式，成功返回 0 */
int app_bluetooth_status(SerialDevice *serial_device)
{
    if (!serial_device) return -1;
    return bluetooth_send_cmd_expect_ack(serial_device, "AT\r\n", 4);
}

/* 发送 "AT+BAUDx\r\n" 设置模块工作波特率，baud_rate 为 SerialBaudRate 枚举值（即编码字符）*/
int app_bluetooth_setBaudRate(SerialDevice *serial_device, SerialBaudRate baud_rate)
{
    if (!serial_device) return -1;
    char buf[] = "AT+BAUD8\r\n";
    buf[7] = baud_rate;
    return bluetooth_send_cmd_expect_ack(serial_device, buf, 10);
}

/* 发送 "AT+RESET\r\n" 复位模块，使已配置的参数生效 */
int app_bluetooth_reset(SerialDevice *serial_device)
{
    if (!serial_device) return -1;
    return bluetooth_send_cmd_expect_ack(serial_device, "AT+RESET\r\n", 10);
}

/* 发送 "AT+NETIDxxxx\r\n" 设置模块网络ID，net_id 须为 4字符字符串 */
int app_bluetooth_setNetID(SerialDevice *serial_device, char *net_id)
{
    if (!serial_device || !net_id) return -1;
    char buf[] = "AT+NETID1111\r\n";
    memcpy(buf + 8, net_id, 4);
    return bluetooth_send_cmd_expect_ack(serial_device, buf, 14);
}

/* 发送 "AT+MADDRxxxx\r\n" 设置模块物理主地址，m_addr 须为 4字符字符串 */
int app_bluetooth_setMAddr(SerialDevice *serial_device, char *m_addr)
{
    if (!serial_device || !m_addr) return -1;
    char buf[] = "AT+MADDR0001\r\n";
    memcpy(buf + 8, m_addr, 4);
    return bluetooth_send_cmd_expect_ack(serial_device, buf, 14);
}

/* 接收后处理钩子：从串口原始字节流中按协议帧格式（帧头/帧尾/ID长度）拆帧，
 * 输出标准内部消息格式：[1B连接类型][1B ID长度][1B数据长度][ID][数据] */
int app_bluetooth_postRead(Device *device, void *ptr, int *len)
{
    if (!device || !ptr || !len || *len <= 0) {
        if (len) *len = 0;
        return 0;
    }
    
    BluetoothContext *ctx = find_context(device->fd);
    if (!ctx) {
        ctx = get_or_create_context(device->fd, NULL);
        if (!ctx) {
            *len = 0;
            return 0;
        }
    }
    
    if (*len > READ_BUFFER_SIZE) {
        log_error("Data too large for buffer: %d > %d", *len, READ_BUFFER_SIZE);
        *len = 0;
        return 0;
    }
    
    if (ctx->read_buffer_len + *len > READ_BUFFER_SIZE) {
        log_warn("Bluetooth buffer overflow, discarding old data");
        ctx->read_buffer_len = 0;
    }
    
    memcpy(ctx->read_buffer + ctx->read_buffer_len, ptr, *len);
    ctx->read_buffer_len += *len;

    if (ctx->protocol.frame_header_len <= 0 || ctx->protocol.id_len <= 0) {
        *len = 0;
        return 0;
    }

    for (int i = 0; i < ctx->read_buffer_len; i++) {
        int remaining = ctx->read_buffer_len - i;

        if (app_private_protocol_is_ack(&ctx->protocol, ctx->read_buffer + i, remaining)) {
            app_bluetooth_ignoreBuffer(ctx, i + ctx->protocol.ack_frame_len);
            *len = 0;
            return 0;
        }
        if (app_private_protocol_is_nack(&ctx->protocol, ctx->read_buffer + i, remaining)) {
            app_bluetooth_ignoreBuffer(ctx, i + ctx->protocol.nack_frame_len);
            *len = 0;
            return 0;
        }

        int payload_len = 0;
        int total_len = app_private_protocol_validate_frame(&ctx->protocol,
                                                            ctx->read_buffer + i, remaining,
                                                            &payload_len);
        if (total_len > 0) {
            int out_len = app_private_protocol_unpack_frame(&ctx->protocol,
                                                            ctx->read_buffer + i, total_len,
                                                            device->connection_type,
                                                            (unsigned char *)ptr, READ_BUFFER_SIZE);
            if (out_len < 0) {
                app_bluetooth_ignoreBuffer(ctx, i + 1);
                *len = 0;
                return 0;
            }

            *len = out_len;
            app_bluetooth_ignoreBuffer(ctx, i + total_len);
            return 0;
        }

        if (total_len == 0) {
            if (i > 0) {
                app_bluetooth_ignoreBuffer(ctx, i);
            }
            *len = 0;
            return 0;
        }
    }

    if (ctx->read_buffer_len > ctx->protocol.frame_header_len) {
        int keep = ctx->protocol.frame_header_len - 1;
        if (keep < 0) {
            keep = 0;
        }
        app_bluetooth_ignoreBuffer(ctx, ctx->read_buffer_len - keep);
    }

    *len = 0;
    return 0;
}

/* 发送前处理钩子：将内部消息格式转换为设备私有帧格式（mesh_cmd_prefix + ID + 数据 + \r\n） */
int app_bluetooth_preWrite(Device *device, void *ptr, int *len)
{
    if (!device || !ptr || !len || *len < 3) {
        if (len) *len = 0;
        return 0;
    }
    
    BluetoothContext *ctx = find_context(device->fd);
    if (!ctx) {
        *len = 0;
        return 0;
    }

    size_t command_cap = strlen(ctx->protocol.mesh_cmd_prefix) + (size_t)*len + 4;
    unsigned char *buf = malloc(command_cap);
    if (!buf) {
        *len = 0;
        return 0;
    }

    int built_len = app_private_protocol_build_command(&ctx->protocol, ptr, *len, buf, (int)command_cap);
    if (built_len < 0) {
        free(buf);
        *len = 0;
        return 0;
    }

    *len = built_len;
    memcpy(ptr, buf, (size_t)*len);
    free(buf);
    return 0;
}
