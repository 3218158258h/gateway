#include "../include/app_bluetooth.h"
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

/* 将整数波特率解析为 SerialBaudRate 枚举值；不支持的值默认返回 115200 */
static SerialBaudRate app_bluetooth_resolveBaudRate(int baud_rate)
{
    if (baud_rate == 9600) {
        return SERIAL_BAUD_RATE_9600;
    }
    return SERIAL_BAUD_RATE_115200;
}

/* 从 gateway.ini [bluetooth] 节读取运行时设备参数（m_addr / net_id / baud_rate），
 * 读取失败时保留入参中已设置的默认值。属于设备层参数，与接口层参数相互独立。*/
static void app_bluetooth_loadRuntimeConfig(char *m_addr, size_t m_addr_len,
                                            char *net_id, size_t net_id_len,
                                            SerialBaudRate *work_baud)
{
    if (!m_addr || !net_id || !work_baud || m_addr_len < BT_ADDR_STR_LEN + 1 || net_id_len < BT_ADDR_STR_LEN + 1) {
        return;
    }

    snprintf(m_addr, m_addr_len, "%s", DEFAULT_BT_MADDR);
    snprintf(net_id, net_id_len, "%s", DEFAULT_BT_NETID);
    *work_baud = app_bluetooth_resolveBaudRate(DEFAULT_BT_WORK_BAUD);

    ConfigManager cfg_mgr = {0};
    if (config_init(&cfg_mgr, APP_DEFAULT_CONFIG_FILE) != 0) {
        log_warn("Bluetooth config load failed, using defaults");
        return;
    }
    if (config_load(&cfg_mgr) != 0) {
        log_warn("Bluetooth config load failed, using defaults");
        config_destroy(&cfg_mgr);
        return;
    }

    char value[CONFIG_MAX_VALUE_LEN] = {0};
    if (config_get_string(&cfg_mgr, "bluetooth", "m_addr", DEFAULT_BT_MADDR, value, sizeof(value)) == 0 &&
        strlen(value) == BT_ADDR_STR_LEN) {
        snprintf(m_addr, m_addr_len, "%s", value);
    }
    if (config_get_string(&cfg_mgr, "bluetooth", "net_id", DEFAULT_BT_NETID, value, sizeof(value)) == 0 &&
        strlen(value) == BT_ADDR_STR_LEN) {
        snprintf(net_id, net_id_len, "%s", value);
    }

    int baud_rate = config_get_int(&cfg_mgr, "bluetooth", "baud_rate", DEFAULT_BT_WORK_BAUD);
    if (baud_rate != 9600 && baud_rate != 115200) {
        log_warn("Invalid [bluetooth].baud_rate=%d, fallback to %d", baud_rate, DEFAULT_BT_WORK_BAUD);
        baud_rate = DEFAULT_BT_WORK_BAUD;
    }
    *work_baud = app_bluetooth_resolveBaudRate(baud_rate);

    config_destroy(&cfg_mgr);
}

/* 按 device_fd 查找或新建运行时上下文（含读缓冲区 + 协议参数），对应设备层"初始化"阶段 */
static BluetoothContext* get_or_create_context(int device_fd, const BluetoothProtocolConfig *protocol) {
    for (int i = 0; i < bt_context_count; i++) {
        if (bt_contexts[i].device_fd == device_fd) {
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

/* 从读缓冲区头部丢弃 n 个字节，将剩余数据前移 */
static void app_bluetooth_ignoreBuffer(BluetoothContext *ctx, int n)
{
    if (!ctx || n <= 0 || n > ctx->read_buffer_len) return;
    
    ctx->read_buffer_len -= n;
    if (ctx->read_buffer_len > 0) {
        memmove(ctx->read_buffer, ctx->read_buffer + n, ctx->read_buffer_len);
    }
}

/* 等待约 200ms 后读取最多 4 字节，判断是否为 "OK\r\n" 应答 */
static int app_bluetooth_waitACK(SerialDevice *serial_device)
{
    usleep(200000);
    unsigned char buf[4];
    ssize_t len = read(serial_device->super.fd, buf, 4);
    if (len >= 4 && memcmp(buf, "OK\r\n", 4) == 0)
    {
        return 0;
    }
    return -1;
}

/**
 * @brief 向设备发送一条 AT 指令并等待 "OK\r\n" 应答
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

    return app_bluetooth_waitACK(serial_device);
}

/**
 * @brief 将设备配置指令模板中的占位符替换为运行时参数
 *
 * 支持以下占位符（区分大小写）：
 *   {m_addr}    - 替换为 4字符模块主地址（如 "0001"，来自 [bluetooth].m_addr）
 *   {net_id}    - 替换为 4字符网络ID（如 "1111"，来自 [bluetooth].net_id）
 *   {baud_code} - 替换为单字节波特率编码字符（SerialBaudRate 枚举值，如 '8' 表示 115200）
 *
 * 模板中的 \r\n 在加载时已由协议配置模块转义为实际 CR/LF 字节，本函数原样保留。
 *
 * @param tmpl      指令模板字符串（\r\n 已为实际字节）
 * @param out       输出缓冲区
 * @param out_len   输出缓冲区长度
 * @param m_addr    模块主地址字符串
 * @param net_id    网络ID字符串
 * @param baud_code 波特率编码字符（SerialBaudRate 枚举值）
 * @return 输出字节数（≥0）；-1 表示参数错误或缓冲区不足
 */
static int app_bluetooth_expand_cmd(const char *tmpl, char *out, size_t out_len,
                                     const char *m_addr, const char *net_id, char baud_code)
{
    if (!tmpl || !out || out_len == 0) {
        return -1;
    }

    size_t i = 0; /* 模板索引 */
    size_t j = 0; /* 输出索引 */
    while (tmpl[i] != '\0' && j < out_len - 1) {
        if (tmpl[i] == '{') {
            if (strncmp(tmpl + i, "{m_addr}", 8) == 0) {
                /* 替换 {m_addr} 为模块主地址字符串 */
                size_t addr_len = strlen(m_addr);
                if (j + addr_len >= out_len) {
                    return -1;
                }
                memcpy(out + j, m_addr, addr_len);
                j += addr_len;
                i += 8;
            } else if (strncmp(tmpl + i, "{net_id}", 8) == 0) {
                /* 替换 {net_id} 为网络ID字符串 */
                size_t id_len_val = strlen(net_id);
                if (j + id_len_val >= out_len) {
                    return -1;
                }
                memcpy(out + j, net_id, id_len_val);
                j += id_len_val;
                i += 8;
            } else if (strncmp(tmpl + i, "{baud_code}", 11) == 0) {
                /* 替换 {baud_code} 为单字节波特率编码字符 */
                if (j + 1 >= out_len) {
                    return -1;
                }
                out[j++] = baud_code;
                i += 11;
            } else {
                /* 未知占位符，原样输出 '{' */
                out[j++] = tmpl[i++];
            }
        } else {
            out[j++] = tmpl[i++];
        }
    }
    out[j] = '\0';
    return (int)j;
}

int app_bluetooth_setConnectionType(SerialDevice *serial_device, const char *protocol_name)
{
    if (!serial_device) return -1;

    /* 加载运行时设备参数：模块地址、网络ID、工作波特率（来自 gateway.ini [bluetooth] 节）
     * 这些是设备层参数，与接口层的串口路径等参数相互独立 */
    char m_addr[BT_ADDR_STR_LEN + 1] = {0};
    char net_id[BT_ADDR_STR_LEN + 1] = {0};
    SerialBaudRate work_baud = SERIAL_BAUD_RATE_115200;
    app_bluetooth_loadRuntimeConfig(m_addr, sizeof(m_addr), net_id, sizeof(net_id), &work_baud);

    /* 加载协议配置：帧格式（帧头/帧尾/ID长度）、下行指令前缀、
     * 状态检查指令（status_cmd）及设备配置指令序列（init_cmds） */
    BluetoothProtocolConfig protocol;
    if (app_protocol_load_bluetooth(protocol_name, &protocol) != 0) {
        return -1;
    }

    /* 绑定连接类型与读写钩子（postRead/preWrite），供后续正常工作阶段使用 */
    serial_device->super.connection_type = protocol.connection_type;
    serial_device->super.vptr->post_read = app_bluetooth_postRead;
    serial_device->super.vptr->pre_write = app_bluetooth_preWrite;

    /* 设备层"初始化"阶段：分配运行时上下文（读缓冲区 + 协议参数） */
    get_or_create_context(serial_device->super.fd, &protocol);

    /* 接口层已由上层完成设备文件打开；此处将串口切换到配置波特率（9600），
     * 进入设备层"配置"阶段准备 */
    app_serial_setBaudRate(serial_device, SERIAL_BAUD_RATE_9600);
    app_serial_setBlockMode(serial_device, 0);
    app_serial_flush(serial_device);

    /* 设备层"配置"阶段：先发送状态检查指令确认模块处于 AT 命令模式，
     * 再按序发送 init_cmds 完成设备参数配置 */
    int status_ok = 0;
    if (protocol.status_cmd[0] != '\0') {
        /* 发送状态检查指令（如 "AT\r\n"），等待 "OK\r\n" 应答 */
        status_ok = (bluetooth_send_cmd_expect_ack(serial_device,
                                                   protocol.status_cmd,
                                                   strlen(protocol.status_cmd)) == 0);
    } else {
        /* 无状态检查指令，直接进入配置 */
        status_ok = 1;
    }

    if (status_ok) {
        char baud_code = (char)work_baud; /* SerialBaudRate 枚举值即为波特率编码字符 */
        /* 按序发送所有设备配置指令；每条指令先替换占位符，再下发并等待 ACK */
        for (int i = 0; i < protocol.init_cmds_count; i++) {
            char expanded[APP_PROTOCOL_INIT_CMD_MAX_LEN];
            if (app_bluetooth_expand_cmd(protocol.init_cmds[i], expanded, sizeof(expanded),
                                          m_addr, net_id, baud_code) < 0) {
                log_warn("Bluetooth: init_cmd[%d] expand failed, skip", i);
                continue;
            }
            if (bluetooth_send_cmd_expect_ack(serial_device, expanded, strlen(expanded)) < 0) {
                log_error("Bluetooth: init_cmd[%d] failed", i);
                return -1;
            }
        }
    }

    /* 设备层"正常工作"阶段：切换至工作波特率，启用阻塞模式，完成设备层初始化流程 */
    app_serial_setBaudRate(serial_device, work_baud);
    app_serial_setBlockMode(serial_device, 1);
    sleep(1);
    app_serial_flush(serial_device);
    return 0;
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

    if (ctx->read_buffer_len < 4) {
        *len = 0;
        return 0;
    }

    const int frame_header_len = ctx->protocol.frame_header_len;
    const int frame_tail_len = ctx->protocol.frame_tail_len;
    const int id_len = ctx->protocol.id_len;
    if (frame_header_len <= 0 || id_len <= 0) {
        *len = 0;
        return 0;
    }

    for (int i = 0; i < ctx->read_buffer_len - 3; i++)
    {
        if (memcmp(ctx->read_buffer + i, "OK\r\n", 4) == 0) {
            app_bluetooth_ignoreBuffer(ctx, i + 4);
            *len = 0;
            return 0;
        }
        else if (memcmp(ctx->read_buffer + i, ctx->protocol.frame_header, frame_header_len) == 0) {
            // 检查是否有足够的字节来组成一个完整的包
            int remaining_after_header = ctx->read_buffer_len - i;
    
            if (remaining_after_header < frame_header_len + BT_LENGTH_FIELD_SIZE) {
                *len = 0;
                return 0;
            }
            app_bluetooth_ignoreBuffer(ctx, i);
            
            if (ctx->read_buffer_len < frame_header_len + BT_LENGTH_FIELD_SIZE) {
                *len = 0;
                return 0;
            }
            
            int packet_len = ctx->read_buffer[frame_header_len];
            
            int fixed_overhead = frame_header_len + BT_LENGTH_FIELD_SIZE + frame_tail_len;
            if (fixed_overhead >= READ_BUFFER_SIZE) {
                log_error("Invalid protocol frame overhead: %d", fixed_overhead);
                ctx->read_buffer_len = 0;
                *len = 0;
                return 0;
            }
            int max_payload_len = READ_BUFFER_SIZE - fixed_overhead;
            if (packet_len > max_payload_len ||
                packet_len < id_len) {
                log_error("Invalid packet length: %d", packet_len);
                ctx->read_buffer_len = 0;
                *len = 0;
                return 0;
            }
            
            if (ctx->read_buffer_len < packet_len + frame_header_len + BT_LENGTH_FIELD_SIZE + frame_tail_len) {
                *len = 0;
                return 0;
            }
            if (frame_tail_len > 0) {
                int tail_offset = frame_header_len + BT_LENGTH_FIELD_SIZE + packet_len;
                if (memcmp(ctx->read_buffer + tail_offset, ctx->protocol.frame_tail, frame_tail_len) != 0) {
                    app_bluetooth_ignoreBuffer(ctx, 1);
                    *len = 0;
                    return 0;
                }
            }

            int data_len = packet_len - id_len;
            int offset = 0;
            /* 类型 */
            memcpy(ptr + offset, &device->connection_type, 1);
            offset += 1;
            /* ID长度 */
            memcpy(ptr + offset, &id_len, 1);
            offset += 1;
            /* 数据长度 */
            memcpy(ptr + offset, &data_len, 1);
            offset += 1;
            /* ID (位置: 帧头2 + 长度1 = 3) */
            int id_offset = frame_header_len + BT_LENGTH_FIELD_SIZE;
            memcpy(ptr + offset, ctx->read_buffer + id_offset, id_len);
            offset += id_len;
            /* 数据 (位置: 3 + 2 = 5) */
            if (data_len > 0) {
                int data_offset = id_offset + id_len;
                memcpy(ptr + offset, ctx->read_buffer + data_offset, data_len);
                offset += data_len;
            }
            
            *len = offset;
            app_bluetooth_ignoreBuffer(ctx, frame_header_len + BT_LENGTH_FIELD_SIZE + packet_len + frame_tail_len);
            return 0;
        }   
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

    unsigned char incoming_type = 0;
    int temp = 0;
    unsigned char buf[64];
    size_t cmd_prefix_len = strlen(ctx->protocol.mesh_cmd_prefix);
    if (cmd_prefix_len == 0 || cmd_prefix_len >= sizeof(buf) - 2) {
        *len = 0;
        return 0;
    }

    memcpy(&incoming_type, ptr, 1);
    if ((ConnectionType)incoming_type != ctx->protocol.connection_type)
    {
        *len = 0;
        return 0;
    }

    memcpy(&temp, ptr + 1, 1);
    if (temp != ctx->protocol.id_len)
    {
        *len = 0;
        return 0;
    }
    memcpy(buf, ctx->protocol.mesh_cmd_prefix, cmd_prefix_len);
    memcpy(buf + cmd_prefix_len, ptr + 3, (size_t)ctx->protocol.id_len);

    memcpy(&temp, ptr + 2, 1);
    if (temp > (int)(sizeof(buf) - cmd_prefix_len - ctx->protocol.id_len - 2)) {
        log_warn("Bluetooth data too large: %d", temp);
        *len = 0;
        return 0;
    }
    memcpy(buf + cmd_prefix_len + ctx->protocol.id_len, ptr + 3 + ctx->protocol.id_len, temp);
    memcpy(buf + cmd_prefix_len + ctx->protocol.id_len + temp, "\r\n", 2);
    *len = temp + (int)cmd_prefix_len + ctx->protocol.id_len + 2;
    memcpy(ptr, buf, *len);
    return 0;
}
