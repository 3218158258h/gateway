#include "../include/app_serial.h"
#include <unistd.h>
#include <string.h>

typedef int (*SerialTermiosUpdater)(SerialDevice *serial_device, struct termios *options, void *ctx);

static int app_serial_update_termios(SerialDevice *serial_device,
                                     SerialTermiosUpdater updater,
                                     void *ctx)
{
    if (!serial_device || !updater) return -1;

    struct termios options;
    if (tcgetattr(serial_device->super.fd, &options) != 0)
    {
        return -1;
    }

    if (updater(serial_device, &options, ctx) != 0) {
        return -1;
    }

    return tcsetattr(serial_device->super.fd, TCSAFLUSH, &options);
}

static int serial_update_cs8(SerialDevice *serial_device, struct termios *options, void *ctx)
{
    (void)serial_device;
    (void)ctx;
    options->c_cflag &= ~CSIZE;
    options->c_cflag |= CS8;
    return 0;
}

static int app_serial_setCS8(SerialDevice *serial_device)
{
    return app_serial_update_termios(serial_device, serial_update_cs8, NULL);
}

static int serial_update_raw(SerialDevice *serial_device, struct termios *options, void *ctx)
{
    (void)serial_device;
    (void)ctx;
    cfmakeraw(options);
    return 0;
}

static int app_serial_setRaw(SerialDevice *serial_device)
{
    return app_serial_update_termios(serial_device, serial_update_raw, NULL);
}

static int serial_update_baud_rate(SerialDevice *dev, struct termios *options, void *ctx)
{
    SerialBaudRate value = *(SerialBaudRate *)ctx;
    dev->baud_rate = value;

    switch (value)
    {
    case SERIAL_BAUD_RATE_9600:
        cfsetispeed(options, B9600);
        cfsetospeed(options, B9600);
        break;
    case SERIAL_BAUD_RATE_115200:
        cfsetispeed(options, B115200);
        cfsetospeed(options, B115200);
        break;
    default:
        break;
    }
    return 0;
}

static int serial_update_stop_bits(SerialDevice *dev, struct termios *options, void *ctx)
{
    StopBits value = *(StopBits *)ctx;
    dev->stop_bits = value;
    options->c_cflag &= ~CSTOPB;
    options->c_cflag |= value;
    return 0;
}

static int serial_update_parity(SerialDevice *dev, struct termios *options, void *ctx)
{
    Parity value = *(Parity *)ctx;
    dev->parity = value;
    options->c_cflag &= ~(PARENB | PARODD);
    options->c_cflag |= value;
    return 0;
}

static int serial_update_block_mode(SerialDevice *dev, struct termios *options, void *ctx)
{
    (void)dev;
    int enabled = *(int *)ctx;
    if (enabled)
    {
        options->c_cc[VTIME] = 0;
        options->c_cc[VMIN] = 1;
    }
    else
    {
        // VTIME单位为0.1秒，因此VTIME=5表示0.5秒
        options->c_cc[VTIME] = 5;
        options->c_cc[VMIN] = 0;
    }
    return 0;
}

int app_serial_init(SerialDevice *serial_device, char *filename)
{
    if (!serial_device || !filename) return -1;
    
    if (app_device_init(&serial_device->super, filename) < 0)
    {
        return -1;
    }

    app_serial_setCS8(serial_device);
    app_serial_setBaudRate(serial_device, SERIAL_BAUD_RATE_9600);
    app_serial_setParity(serial_device, PARITY_NONE);
    app_serial_setStopBits(serial_device, STOP_BITS_ONE);
    app_serial_setRaw(serial_device);
    app_serial_setBlockMode(serial_device, 0);

    return tcflush(serial_device->super.fd, TCIOFLUSH);
}

int app_serial_setBaudRate(SerialDevice *serial_device, SerialBaudRate baud_rate)
{
    if (!serial_device) return -1;
    return app_serial_update_termios(serial_device, serial_update_baud_rate, &baud_rate);
}

int app_serial_setStopBits(SerialDevice *serial_device, StopBits stop_bits)
{
    if (!serial_device) return -1;
    return app_serial_update_termios(serial_device, serial_update_stop_bits, &stop_bits);
}

int app_serial_setParity(SerialDevice *serial_device, Parity parity)
{
    if (!serial_device) return -1;
    return app_serial_update_termios(serial_device, serial_update_parity, &parity);
}

int app_serial_flush(SerialDevice *serial_device)
{
    if (!serial_device) return -1;
    return tcflush(serial_device->super.fd, TCIOFLUSH);
}

int app_serial_setBlockMode(SerialDevice *serial_device, int block_mode)
{
    if (!serial_device) return -1;
    return app_serial_update_termios(serial_device, serial_update_block_mode, &block_mode);
}
