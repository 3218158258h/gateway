#include "../include/app_serial.h"
#include "../include/app_config.h"
#include "../thirdparty/log.c/log.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>

void app_transport_config_init(SerialDevice *serial_device)
{
    if (!serial_device) {
        return;
    }
    memset(serial_device, 0, sizeof(*serial_device));
    serial_device->transport.interface_type = APP_INTERFACE_SERIAL;
    serial_device->transport.uart.baud_rate = SERIAL_BAUD_RATE_9600;
    serial_device->transport.uart.stop_bits = STOP_BITS_ONE;
    serial_device->transport.uart.parity = PARITY_NONE;
    serial_device->transport.uart.block_mode = 0;
}

AppInterfaceType app_transport_string_to_interface(const char *interface_name)
{
    if (!interface_name || interface_name[0] == '\0' ||
        strcasecmp(interface_name, "serial") == 0 ||
        strcasecmp(interface_name, "uart") == 0) {
        return APP_INTERFACE_SERIAL;
    }
    if (strcasecmp(interface_name, "spi") == 0) {
        return APP_INTERFACE_SPI;
    }
    if (strcasecmp(interface_name, "i2c") == 0 || strcasecmp(interface_name, "iic") == 0) {
        return APP_INTERFACE_I2C;
    }
    if (strcasecmp(interface_name, "can") == 0) {
        return APP_INTERFACE_CAN;
    }
    return APP_INTERFACE_CAN + 1;
}

const char *app_transport_interface_to_string(AppInterfaceType interface_type)
{
    switch (interface_type) {
    case APP_INTERFACE_SERIAL:
        return "uart";
    case APP_INTERFACE_SPI:
        return "spi";
    case APP_INTERFACE_I2C:
        return "i2c";
    case APP_INTERFACE_CAN:
        return "can";
    default:
        return "unknown";
    }
}

static SerialBaudRate app_transport_resolve_baud_rate(int baud_rate)
{
    if (baud_rate == 9600) {
        return SERIAL_BAUD_RATE_9600;
    }
    return SERIAL_BAUD_RATE_115200;
}

static StopBits app_transport_resolve_stop_bits(int stop_bits)
{
    return stop_bits == 2 ? STOP_BITS_TWO : STOP_BITS_ONE;
}

static Parity app_transport_resolve_parity(int parity)
{
    switch (parity) {
    case 1:
        return PARITY_ODD;
    case 2:
        return PARITY_EVEN;
    default:
        return PARITY_NONE;
    }
}

int app_transport_config_load(SerialDevice *serial_device, const char *config_file, const char *section_name)
{
    if (!serial_device || !config_file || !section_name || section_name[0] == '\0') {
        return -1;
    }

    app_transport_config_init(serial_device);

    ConfigManager cfg_mgr = {0};
    if (config_init(&cfg_mgr, config_file) != 0 || config_load(&cfg_mgr) != 0) {
        log_warn("Transport config load failed (%s), using defaults for %s",
                 config_file, section_name);
        return 0;
    }

    char value[CONFIG_MAX_VALUE_LEN] = {0};
    if (config_get_string(&cfg_mgr, section_name, "interface", "",
                          value, sizeof(value)) == 0 && value[0] != '\0') {
        serial_device->transport.interface_type = app_transport_string_to_interface(value);
    }

    if (config_get_string(&cfg_mgr, section_name, "device_path", "",
                          serial_device->transport.device_path,
                          sizeof(serial_device->transport.device_path)) != 0) {
        serial_device->transport.device_path[0] = '\0';
    }

    serial_device->transport.uart.baud_rate = app_transport_resolve_baud_rate(config_get_int(
        &cfg_mgr, section_name, "baud_rate", 9600));
    serial_device->transport.uart.stop_bits = app_transport_resolve_stop_bits(config_get_int(
        &cfg_mgr, section_name, "stop_bits", 1));
    serial_device->transport.uart.parity = app_transport_resolve_parity(config_get_int(
        &cfg_mgr, section_name, "parity", 0));
    serial_device->transport.uart.block_mode = config_get_int(
        &cfg_mgr, section_name, "block_mode", serial_device->transport.uart.block_mode);

    serial_device->transport.can.bitrate = (unsigned int)config_get_int(
        &cfg_mgr, section_name, "bitrate", (int)serial_device->transport.can.bitrate);
    serial_device->transport.can.loopback = config_get_bool(
        &cfg_mgr, section_name, "loopback", serial_device->transport.can.loopback);
    serial_device->transport.can.listen_only = config_get_bool(
        &cfg_mgr, section_name, "listen_only", serial_device->transport.can.listen_only);
    serial_device->transport.can.fd_mode = config_get_bool(
        &cfg_mgr, section_name, "fd_mode", serial_device->transport.can.fd_mode);

    serial_device->transport.spi.clock_hz = (unsigned int)config_get_int(
        &cfg_mgr, section_name, "clock_hz", (int)serial_device->transport.spi.clock_hz);
    serial_device->transport.spi.mode = (unsigned int)config_get_int(
        &cfg_mgr, section_name, "mode", (int)serial_device->transport.spi.mode);
    serial_device->transport.spi.bits_per_word = (unsigned int)config_get_int(
        &cfg_mgr, section_name, "bits_per_word", (int)serial_device->transport.spi.bits_per_word);
    serial_device->transport.spi.lsb_first = config_get_bool(
        &cfg_mgr, section_name, "lsb_first", serial_device->transport.spi.lsb_first);
    serial_device->transport.spi.chip_select = (unsigned int)config_get_int(
        &cfg_mgr, section_name, "chip_select", (int)serial_device->transport.spi.chip_select);

    serial_device->transport.i2c.bus_speed_hz = (unsigned int)config_get_int(
        &cfg_mgr, section_name, "bus_speed_hz", (int)serial_device->transport.i2c.bus_speed_hz);
    serial_device->transport.i2c.address = (unsigned short)config_get_int(
        &cfg_mgr, section_name, "address", serial_device->transport.i2c.address);
    serial_device->transport.i2c.ten_bit_address = config_get_bool(
        &cfg_mgr, section_name, "ten_bit_address", serial_device->transport.i2c.ten_bit_address);
    serial_device->transport.i2c.clock_stretching = config_get_bool(
        &cfg_mgr, section_name, "clock_stretching", serial_device->transport.i2c.clock_stretching);

    config_destroy(&cfg_mgr);
    return 0;
}

static int app_serial_get_options(SerialDevice *serial_device, struct termios *options)
{
    if (!serial_device || !options) return -1;
    if (tcgetattr(serial_device->super.fd, options) != 0) {
        return -1;
    }
    return 0;
}

static int app_serial_set_options(SerialDevice *serial_device, struct termios *options)
{
    if (!serial_device || !options) return -1;
    if (tcsetattr(serial_device->super.fd, TCSAFLUSH, options) != 0) {
        return -1;
    }
    return 0;
}

static int app_serial_setCS8(SerialDevice *serial_device)
{
    if (!serial_device) return -1;
    struct termios options;
    if (app_serial_get_options(serial_device, &options) != 0) return -1;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    return app_serial_set_options(serial_device, &options);
}

static int app_serial_setRaw(SerialDevice *serial_device)
{
    if (!serial_device) return -1;
    struct termios options;
    if (app_serial_get_options(serial_device, &options) != 0) return -1;
    cfmakeraw(&options);
    return app_serial_set_options(serial_device, &options);
}

int app_serial_init(SerialDevice *serial_device, char *filename)
{
    if (!serial_device || !filename) return -1;
    app_transport_config_load(serial_device, APP_PHYSICAL_TRANSPORT_CONFIG_FILE, "transport.serial_default");
    serial_device->transport.interface_type = APP_INTERFACE_SERIAL;
    snprintf(serial_device->transport.device_path, sizeof(serial_device->transport.device_path), "%s", filename);

    if (app_device_init(&serial_device->super, filename) < 0)
    {
        return -1;
    }

    app_serial_setCS8(serial_device);
    app_serial_setBaudRate(serial_device, serial_device->transport.uart.baud_rate);
    app_serial_setParity(serial_device, serial_device->transport.uart.parity);
    app_serial_setStopBits(serial_device, serial_device->transport.uart.stop_bits);
    app_serial_setRaw(serial_device);
    app_serial_setBlockMode(serial_device, serial_device->transport.uart.block_mode);

    return tcflush(serial_device->super.fd, TCIOFLUSH);
}

int app_serial_setBaudRate(SerialDevice *serial_device, SerialBaudRate baud_rate)
{
    if (!serial_device) return -1;
    struct termios options;
    if (app_serial_get_options(serial_device, &options) != 0) return -1;

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
        log_error("Unsupported baud rate: %d", baud_rate);
        return -1;
    }
    if (app_serial_set_options(serial_device, &options) != 0) {
        return -1;
    }
    serial_device->transport.uart.baud_rate = baud_rate;
    return 0;
}

int app_serial_setStopBits(SerialDevice *serial_device, StopBits stop_bits)
{
    if (!serial_device) return -1;
    struct termios options;
    if (app_serial_get_options(serial_device, &options) != 0) return -1;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag |= stop_bits;
    serial_device->transport.uart.stop_bits = stop_bits;
    return app_serial_set_options(serial_device, &options);
}

int app_serial_setParity(SerialDevice *serial_device, Parity parity)
{
    if (!serial_device) return -1;
    struct termios options;
    if (app_serial_get_options(serial_device, &options) != 0) return -1;
    options.c_cflag &= ~(PARENB | PARODD);
    options.c_cflag |= parity;
    serial_device->transport.uart.parity = parity;
    return app_serial_set_options(serial_device, &options);
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
    if (app_serial_get_options(serial_device, &options) != 0) return -1;

    if (block_mode)
    {
        options.c_cc[VTIME] = 0;
        options.c_cc[VMIN] = 1;
    }
    else
    {
        // VTIME单位为0.1秒，因此VTIME=5表示0.5秒
        options.c_cc[VTIME] = 5;
        options.c_cc[VMIN] = 0;
    }

    serial_device->transport.uart.block_mode = block_mode;
    return app_serial_set_options(serial_device, &options);
}
