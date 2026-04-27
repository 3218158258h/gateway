#include "../include/app_iface_spi.h"
#include "../thirdparty/log.c/log.h"

#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#define SPI_MAX_TRANSFER_LEN 64
#define SPI_DEFAULT_POLL_MS 500

static void sleep_poll_interval(int poll_interval_ms)
{
    int interval = poll_interval_ms > 0 ? poll_interval_ms : SPI_DEFAULT_POLL_MS;
    usleep((useconds_t)(interval * 1000));
}

void *app_iface_spi_background_task(void *argv)
{
    SerialDevice *serial_device = (SerialDevice *)argv;
    if (!serial_device) {
        return NULL;
    }

    Device *device = &serial_device->super;
    int error_count = 0;
    while (device->is_running) {
        unsigned int transfer_len = serial_device->transport.spi.transfer_len;
        if (transfer_len == 0) {
            sleep_poll_interval(serial_device->transport.spi.poll_interval_ms);
            continue;
        }
        if (transfer_len > SPI_MAX_TRANSFER_LEN) {
            transfer_len = SPI_MAX_TRANSFER_LEN;
        }

        unsigned char tx[SPI_MAX_TRANSFER_LEN];
        unsigned char rx[SPI_MAX_TRANSFER_LEN];
        memset(tx, 0, sizeof(tx));
        memset(rx, 0, sizeof(rx));

        struct spi_ioc_transfer tr;
        memset(&tr, 0, sizeof(tr));
        tr.tx_buf = (unsigned long)tx;
        tr.rx_buf = (unsigned long)rx;
        tr.len = transfer_len;
        tr.speed_hz = serial_device->transport.spi.clock_hz;
        tr.bits_per_word = (unsigned char)serial_device->transport.spi.bits_per_word;

        if (ioctl(device->fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
            error_count++;
            if (error_count <= 3 || (error_count % 20) == 0) {
                log_warn("[spi] poll_failed device=%s len=%u errno=%d(%s) count=%d",
                         device->filename ? device->filename : "",
                         transfer_len,
                         errno,
                         strerror(errno),
                         error_count);
            }
            sleep_poll_interval(serial_device->transport.spi.poll_interval_ms);
            continue;
        }

        error_count = 0;
        unsigned char packet[3 + 1 + SPI_MAX_TRANSFER_LEN];
        packet[0] = (unsigned char)device->connection_type;
        packet[1] = 1;
        packet[2] = (unsigned char)transfer_len;
        packet[3] = (unsigned char)serial_device->transport.spi.chip_select;
        if (transfer_len > 0) {
            memcpy(packet + 4, rx, transfer_len);
        }
        app_buffer_write(device->recv_buffer, packet, (int)(4 + transfer_len));
        app_task_register(device->vptr->recv_task, device);
        sleep_poll_interval(serial_device->transport.spi.poll_interval_ms);
    }
    return NULL;
}
