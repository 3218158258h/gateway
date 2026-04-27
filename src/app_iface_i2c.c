#include "../include/app_iface_i2c.h"
#include "../thirdparty/log.c/log.h"

#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#define I2C_MAX_POLL_LEN 64
#define I2C_DEFAULT_POLL_MS 500

static void sleep_poll_interval(int poll_interval_ms)
{
    int interval = poll_interval_ms > 0 ? poll_interval_ms : I2C_DEFAULT_POLL_MS;
    usleep((useconds_t)(interval * 1000));
}

void *app_iface_i2c_background_task(void *argv)
{
    SerialDevice *serial_device = (SerialDevice *)argv;
    if (!serial_device) {
        return NULL;
    }

    Device *device = &serial_device->super;
    int error_count = 0;
    while (device->is_running) {
        unsigned int read_len = serial_device->transport.i2c.read_len;
        if (read_len == 0) {
            sleep_poll_interval(serial_device->transport.i2c.poll_interval_ms);
            continue;
        }
        if (read_len > I2C_MAX_POLL_LEN) {
            read_len = I2C_MAX_POLL_LEN;
        }

        int reg_width = serial_device->transport.i2c.register_addr_width;
        if (reg_width != 2) {
            reg_width = 1;
        }
        unsigned short reg_addr = serial_device->transport.i2c.register_addr;
        unsigned char reg_buf[2] = {0};
        if (reg_width == 2) {
            reg_buf[0] = (unsigned char)((reg_addr >> 8) & 0xFF);
            reg_buf[1] = (unsigned char)(reg_addr & 0xFF);
        } else {
            reg_buf[0] = (unsigned char)(reg_addr & 0xFF);
        }

        unsigned char read_buf[I2C_MAX_POLL_LEN] = {0};
        struct i2c_msg msgs[2];
        memset(msgs, 0, sizeof(msgs));
        msgs[0].addr = serial_device->transport.i2c.address;
        msgs[0].flags = 0;
        msgs[0].len = (unsigned short)reg_width;
        msgs[0].buf = reg_buf;
        msgs[1].addr = serial_device->transport.i2c.address;
        msgs[1].flags = I2C_M_RD;
        msgs[1].len = (unsigned short)read_len;
        msgs[1].buf = read_buf;

        struct i2c_rdwr_ioctl_data ioctl_data;
        ioctl_data.msgs = msgs;
        ioctl_data.nmsgs = 2;

        if (ioctl(device->fd, I2C_RDWR, &ioctl_data) < 0) {
            error_count++;
            if (error_count <= 3 || (error_count % 20) == 0) {
                log_warn("[i2c] poll_failed device=%s addr=0x%02X reg=0x%04X errno=%d(%s) count=%d",
                         device->filename ? device->filename : "",
                         (unsigned int)serial_device->transport.i2c.address,
                         (unsigned int)reg_addr,
                         errno,
                         strerror(errno),
                         error_count);
            }
            sleep_poll_interval(serial_device->transport.i2c.poll_interval_ms);
            continue;
        }

        error_count = 0;
        unsigned char packet[3 + 1 + 1 + I2C_MAX_POLL_LEN];
        packet[0] = (unsigned char)device->connection_type;
        packet[1] = 1;
        packet[2] = (unsigned char)(read_len + 1);
        packet[3] = (unsigned char)serial_device->transport.i2c.address;
        packet[4] = (unsigned char)(reg_addr & 0xFF);
        if (read_len > 0) {
            memcpy(packet + 5, read_buf, read_len);
        }
        app_buffer_write(device->recv_buffer, packet, (int)(5 + read_len));
        app_task_register(device->vptr->recv_task, device);
        sleep_poll_interval(serial_device->transport.i2c.poll_interval_ms);
    }
    return NULL;
}
