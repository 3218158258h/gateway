#include "../include/app_bluetooth.h"
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "../thirdparty/log.c/log.h"

#define MAX_BLUETOOTH_DEVICES 4
#define READ_BUFFER_SIZE 256

typedef struct {
    int device_fd;
    unsigned char read_buffer[READ_BUFFER_SIZE];
    int read_buffer_len;
} BluetoothContext;

static BluetoothContext bt_contexts[MAX_BLUETOOTH_DEVICES];
static int bt_context_count = 0;

static BluetoothContext* get_or_create_context(int device_fd) {
    for (int i = 0; i < bt_context_count; i++) {
        if (bt_contexts[i].device_fd == device_fd) {
            return &bt_contexts[i];
        }
    }
    if (bt_context_count < MAX_BLUETOOTH_DEVICES) {
        bt_contexts[bt_context_count].device_fd = device_fd;
        bt_contexts[bt_context_count].read_buffer_len = 0;
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

static const unsigned char fix_header[] = {0xF1, 0xDD};

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

int app_bluetooth_setConnectionType(SerialDevice *serial_device)
{
    if (!serial_device) return -1;
    
    serial_device->super.connection_type = CONNECTION_TYPE_BLE_MESH;
    serial_device->super.vptr->post_read = app_bluetooth_postRead;
    serial_device->super.vptr->pre_write = app_bluetooth_preWrite;
    
    get_or_create_context(serial_device->super.fd);
    
    app_serial_setBaudRate(serial_device, SERIAL_BAUD_RATE_9600);
    app_serial_setBlockMode(serial_device, 0);
    app_serial_flush(serial_device);
    if (app_bluetooth_status(serial_device) == 0)
    {
        if (app_bluetooth_setMAddr(serial_device, "0001") < 0)
        {
            log_error("Bluetooth: set maddr failed");
            return -1;
        }
        if (app_bluetooth_setNetID(serial_device, "1111") < 0)
        {
            log_error("Bluetooth: set netid failed");
            return -1;
        }
        if (app_bluetooth_setBaudRate(serial_device, SERIAL_BAUD_RATE_9600) < 0)
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
    app_serial_setBaudRate(serial_device, SERIAL_BAUD_RATE_115200);
    app_serial_setBlockMode(serial_device, 1);
    sleep(1);
    app_serial_flush(serial_device);
    return 0;
}

int app_bluetooth_status(SerialDevice *serial_device)
{
    if (!serial_device) return -1;
    write(serial_device->super.fd, "AT\r\n", 4);
    return app_bluetooth_waitACK(serial_device);
}

int app_bluetooth_setBaudRate(SerialDevice *serial_device, SerialBaudRate baud_rate)
{
    if (!serial_device) return -1;
    char buf[] = "AT+BAUD8\r\n";
    buf[7] = baud_rate;
    write(serial_device->super.fd, buf, 10);
    return app_bluetooth_waitACK(serial_device);
}

int app_bluetooth_reset(SerialDevice *serial_device)
{
    if (!serial_device) return -1;
    write(serial_device->super.fd, "AT+RESET\r\n", 10);
    return app_bluetooth_waitACK(serial_device);
}

int app_bluetooth_setNetID(SerialDevice *serial_device, char *net_id)
{
    if (!serial_device || !net_id) return -1;
    char buf[] = "AT+NETID1111\r\n";
    memcpy(buf + 8, net_id, 4);
    write(serial_device->super.fd, buf, 14);
    return app_bluetooth_waitACK(serial_device);
}

int app_bluetooth_setMAddr(SerialDevice *serial_device, char *m_addr)
{
    if (!serial_device || !m_addr) return -1;
    char buf[] = "AT+MADDR0001\r\n";
    memcpy(buf + 8, m_addr, 4);
    write(serial_device->super.fd, buf, 14);
    return app_bluetooth_waitACK(serial_device);
}

int app_bluetooth_postRead(Device *device, void *ptr, int *len)
{
    if (!device || !ptr || !len || *len <= 0) {
        if (len) *len = 0;
        return 0;
    }
    
    BluetoothContext *ctx = find_context(device->fd);
    if (!ctx) {
        ctx = get_or_create_context(device->fd);
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

    for (int i = 0; i < ctx->read_buffer_len - 3; i++)
    {
        if (memcmp(ctx->read_buffer + i, "OK\r\n", 4) == 0) {
            app_bluetooth_ignoreBuffer(ctx, i + 4);
            *len = 0;
            return 0;
        }
        else if (memcmp(ctx->read_buffer + i, fix_header, BT_FRAME_HEADER_SIZE) == 0) {
            // 检查是否有足够的字节来组成一个完整的包
            int remaining_after_header = ctx->read_buffer_len - i;
    
            if (remaining_after_header < 3) {
                *len = 0;
                return 0;
            }
            app_bluetooth_ignoreBuffer(ctx, i);
            
            if (ctx->read_buffer_len < 3) {
                *len = 0;
                return 0;
            }
            
            int packet_len = ctx->read_buffer[2];
            
            if (packet_len > READ_BUFFER_SIZE - 3 || packet_len < BT_PAYLOAD_MIN_LEN) {
                log_error("Invalid packet length: %d", packet_len);
                ctx->read_buffer_len = 0;
                *len = 0;
                return 0;
            }
            
            if (ctx->read_buffer_len < packet_len + 3) {
                *len = 0;
                return 0;
            }

            int data_len = packet_len - BT_DEVICE_ID_SIZE;
            int offset = 0;
            int id_len = BT_DEVICE_ID_SIZE;
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
            int id_offset = BT_FRAME_HEADER_SIZE + BT_LENGTH_FIELD_SIZE;    
            memcpy(ptr + offset, ctx->read_buffer + id_offset, BT_DEVICE_ID_SIZE);
            offset += BT_DEVICE_ID_SIZE;
            /* 数据 (位置: 3 + 2 = 5) */
            if (data_len > 0) {
                int data_offset = id_offset + BT_DEVICE_ID_SIZE;
                memcpy(ptr + offset, ctx->read_buffer + data_offset, data_len);
                offset += data_len;
            }
            
            *len = offset;
            app_bluetooth_ignoreBuffer(ctx, BT_FRAME_HEADER_SIZE + BT_LENGTH_FIELD_SIZE + packet_len);
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
    
    int temp = 0;
    unsigned char buf[30];
    memcpy(&temp, ptr, 1);
    if (temp != CONNECTION_TYPE_BLE_MESH)
    {
        *len = 0;
        return 0;
    }

    memcpy(&temp, ptr + 1, 1);
    if (temp != 2)
    {
        *len = 0;
        return 0;
    }
    memcpy(buf, "AT+MESH", 8);
    memcpy(buf + 8, ptr + 3, 2);

    memcpy(&temp, ptr + 2, 1);
    if (temp > 18) {
        log_warn("Bluetooth data too large: %d", temp);
        *len = 0;
        return 0;
    }
    memcpy(buf + 10, ptr + 5, temp);
    memcpy(buf + 10 + temp, "\r\n", 2);
    *len = temp + 12;
    memcpy(ptr, buf, *len);
    return 0;
}
