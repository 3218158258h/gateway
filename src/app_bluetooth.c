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

static SerialBaudRate app_bluetooth_resolveBaudRate(int baud_rate)
{
    if (baud_rate == 9600) {
        return SERIAL_BAUD_RATE_9600;
    }
    return SERIAL_BAUD_RATE_115200;
}

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

static BluetoothContext* find_context(int device_fd) {
    for (int i = 0; i < bt_context_count; i++) {
        if (bt_contexts[i].device_fd == device_fd) {
            return &bt_contexts[i];
        }
    }
    return NULL;
}

static void app_bluetooth_ignoreBuffer(BluetoothContext *ctx, int n)
{
    if (!ctx || n <= 0 || n > ctx->read_buffer_len) return;
    
    ctx->read_buffer_len -= n;
    if (ctx->read_buffer_len > 0) {
        memmove(ctx->read_buffer, ctx->read_buffer + n, ctx->read_buffer_len);
    }
}

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
 * @brief 发送AT命令并等待ACK
 *
 * 这里统一处理write返回值，避免部分写入或写失败被静默忽略，
 * 导致后续ACK等待逻辑误判。
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

int app_bluetooth_setConnectionType(SerialDevice *serial_device, const char *protocol_name)
{
    if (!serial_device) return -1;

    char m_addr[BT_ADDR_STR_LEN + 1] = {0};
    char net_id[BT_ADDR_STR_LEN + 1] = {0};
    SerialBaudRate work_baud = SERIAL_BAUD_RATE_115200;
    app_bluetooth_loadRuntimeConfig(m_addr, sizeof(m_addr), net_id, sizeof(net_id), &work_baud);
    
    BluetoothProtocolConfig protocol;
    if (app_protocol_load_bluetooth(protocol_name, &protocol) != 0) {
        return -1;
    }

    serial_device->super.connection_type = protocol.connection_type;
    serial_device->super.vptr->post_read = app_bluetooth_postRead;
    serial_device->super.vptr->pre_write = app_bluetooth_preWrite;
    
    get_or_create_context(serial_device->super.fd, &protocol);
    
    app_serial_setBaudRate(serial_device, SERIAL_BAUD_RATE_9600);
    app_serial_setBlockMode(serial_device, 0);
    app_serial_flush(serial_device);
    if (app_bluetooth_status(serial_device) == 0)
    {
        if (app_bluetooth_setMAddr(serial_device, m_addr) < 0)
        {
            log_error("Bluetooth: set maddr failed");
            return -1;
        }
        if (app_bluetooth_setNetID(serial_device, net_id) < 0)
        {
            log_error("Bluetooth: set netid failed");
            return -1;
        }
        if (app_bluetooth_setBaudRate(serial_device, work_baud) < 0)
        {
            log_error("Bluetooth: set baudrate failed");
            return -1;
        }
        if (app_bluetooth_reset(serial_device) < 0)
        {
            log_error("Bluetooth: reset failed");
            return -1;
        }
    }
    app_serial_setBaudRate(serial_device, work_baud);
    app_serial_setBlockMode(serial_device, 1);
    sleep(1);
    app_serial_flush(serial_device);
    return 0;
}

int app_bluetooth_status(SerialDevice *serial_device)
{
    if (!serial_device) return -1;
    return bluetooth_send_cmd_expect_ack(serial_device, "AT\r\n", 4);
}

int app_bluetooth_setBaudRate(SerialDevice *serial_device, SerialBaudRate baud_rate)
{
    if (!serial_device) return -1;
    char buf[] = "AT+BAUD8\r\n";
    buf[7] = baud_rate;
    return bluetooth_send_cmd_expect_ack(serial_device, buf, 10);
}

int app_bluetooth_reset(SerialDevice *serial_device)
{
    if (!serial_device) return -1;
    return bluetooth_send_cmd_expect_ack(serial_device, "AT+RESET\r\n", 10);
}

int app_bluetooth_setNetID(SerialDevice *serial_device, char *net_id)
{
    if (!serial_device || !net_id) return -1;
    char buf[] = "AT+NETID1111\r\n";
    memcpy(buf + 8, net_id, 4);
    return bluetooth_send_cmd_expect_ack(serial_device, buf, 14);
}

int app_bluetooth_setMAddr(SerialDevice *serial_device, char *m_addr)
{
    if (!serial_device || !m_addr) return -1;
    char buf[] = "AT+MADDR0001\r\n";
    memcpy(buf + 8, m_addr, 4);
    return bluetooth_send_cmd_expect_ack(serial_device, buf, 14);
}

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
            
            int max_payload_len = READ_BUFFER_SIZE - frame_header_len - BT_LENGTH_FIELD_SIZE - frame_tail_len;
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

    int temp = 0;
    unsigned char buf[64];
    size_t cmd_prefix_len = strlen(ctx->protocol.mesh_cmd_prefix);
    if (cmd_prefix_len == 0 || cmd_prefix_len >= sizeof(buf) - 2) {
        *len = 0;
        return 0;
    }

    memcpy(&temp, ptr, 1);
    if (temp != (int)ctx->protocol.connection_type)
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
