#include "../include/app_serial.h"
#include <unistd.h>
#include <string.h>

static int app_serial_setCS8(SerialDevice *serial_device)
{
    struct termios options;
    if (tcgetattr(serial_device->super.fd, &options) != 0)
    {
        return -1;
    }

    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;

    return tcsetattr(serial_device->super.fd, TCSAFLUSH, &options);
}

static int app_serial_setRaw(SerialDevice *serial_device)
{
    struct termios options;
    if (tcgetattr(serial_device->super.fd, &options) != 0)
    {
        return -1;
    }

    cfmakeraw(&options);

    return tcsetattr(serial_device->super.fd, TCSAFLUSH, &options);
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
    
    struct termios options;
    if (tcgetattr(serial_device->super.fd, &options) != 0)
    {
        return -1;
    }

    serial_device->baud_rate = baud_rate;

    switch (baud_rate)
    {
    case SERIAL_BAUD_RATE_9600:
        cfsetispeed(&options, B9600);
        cfsetospeed(&options, B9600);
        break;
    case SERIAL_BAUD_RATE_115200:
        cfsetispeed(&options, B115200);
        cfsetospeed(&options, B115200);
        break;
    default:
        break;
    }

    return tcsetattr(serial_device->super.fd, TCSAFLUSH, &options);
}

int app_serial_setStopBits(SerialDevice *serial_device, StopBits stop_bits)
{
    if (!serial_device) return -1;
    
    struct termios options;
    if (tcgetattr(serial_device->super.fd, &options) != 0)
    {
        return -1;
    }

    serial_device->stop_bits = stop_bits;

    options.c_cflag &= ~CSTOPB;
    options.c_cflag |= stop_bits;

    return tcsetattr(serial_device->super.fd, TCSAFLUSH, &options);
}

int app_serial_setParity(SerialDevice *serial_device, Parity parity)
{
    if (!serial_device) return -1;
    
    struct termios options;
    if (tcgetattr(serial_device->super.fd, &options) != 0)
    {
        return -1;
    }

    serial_device->parity = parity;

    options.c_cflag &= ~(PARENB | PARODD);
    options.c_cflag |= parity;

    return tcsetattr(serial_device->super.fd, TCSAFLUSH, &options);
}

int app_serial_flush(SerialDevice *serial_device)
{
    if (!serial_device) return -1;
    return tcflush(serial_device->super.fd, TCIOFLUSH);
}

int app_serial_setBlockMode(SerialDevice *serial_device, int block_mode)
{
    if (!serial_device) return -1;
    
    struct termios options;
    if (tcgetattr(serial_device->super.fd, &options) != 0)
    {
        return -1;
    }

    if (block_mode)
    {
        options.c_cc[VTIME] = 0;
        options.c_cc[VMIN] = 1;
    }
    else
    {
        // VTIME unit is 0.1 seconds, so VTIME=5 means 0.5 seconds
        options.c_cc[VTIME] = 5;
        options.c_cc[VMIN] = 0;
    }

    return tcsetattr(serial_device->super.fd, TCSAFLUSH, &options);
}
