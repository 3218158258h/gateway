#include "../include/app_iface_spi.h"
#include "../thirdparty/log.c/log.h"

#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

/* 单次 SPI 轮询最大传输长度，避免栈缓冲区与事务长度失控。 */
#define SPI_MAX_TRANSFER_LEN 64
/* 未配置轮询周期时使用的默认轮询间隔（毫秒）。 */
#define SPI_DEFAULT_POLL_MS 500

/* 统一轮询休眠入口：保证未配置时仍有稳定退避节奏。 */
static void sleep_poll_interval(int poll_interval_ms)
{
    int interval = poll_interval_ms > 0 ? poll_interval_ms : SPI_DEFAULT_POLL_MS;
    usleep((useconds_t)(interval * 1000));
}

/**
 * SPI 后台轮询线程：周期执行 `SPI_IOC_MESSAGE(1)`，并把返回字节封装成内部帧。
 *
 * 内部帧格式：
 * [connection_type(1)][id_len(1)][data_len(1)][id(1=chip_select)][data(N=rx)]
 */
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
        /* transfer_len=0 视为关闭主动轮询：线程只保活休眠。 */
        if (transfer_len == 0) {
            sleep_poll_interval(serial_device->transport.spi.poll_interval_ms);
            continue;
        }
        /* 长度做硬上限保护，避免异常配置导致超长事务。 */
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
            /* 错误日志限频：前 3 次 + 每 20 次打印一次。 */
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
        /* 内部统一消息头：类型 + 固定 1 字节 ID + 数据长度。 */
        packet[0] = (unsigned char)device->connection_type;
        packet[1] = 1;
        packet[2] = (unsigned char)transfer_len;
        /* ID 使用 chip_select，便于区分同总线不同片选设备。 */
        packet[3] = (unsigned char)serial_device->transport.spi.chip_select;
        if (transfer_len > 0) {
            memcpy(packet + 4, rx, transfer_len);
        }
        /* 推送到设备接收缓冲区并触发统一接收任务。 */
        app_buffer_write(device->recv_buffer, packet, (int)(4 + transfer_len));
        app_task_register(device->vptr->recv_task, device);
        sleep_poll_interval(serial_device->transport.spi.poll_interval_ms);
    }
    return NULL;
}
