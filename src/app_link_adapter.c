#include "../include/app_link_adapter.h"
#include "../include/app_bluetooth.h"
#include "../thirdparty/log.c/log.h"

#include <strings.h>

static AppInterfaceType app_link_adapter_parse_interface(const char *interface_name)
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

int app_link_adapter_init(SerialDevice *device, const char *device_path, const char *interface_name)
{
    if (!device || !device_path || device_path[0] == '\0') {
        return -1;
    }

    AppInterfaceType interface_type = app_link_adapter_parse_interface(interface_name);
    if (interface_type != APP_INTERFACE_SERIAL) {
        log_error("Unsupported interface '%s' for now, only serial is implemented",
                  interface_name ? interface_name : "");
        return -1;
    }

    return app_serial_init(device, (char *)device_path);
}

int app_link_adapter_apply_protocol(SerialDevice *device, const char *protocol_name)
{
    if (!device) {
        return -1;
    }
    return app_bluetooth_setConnectionType(device, protocol_name);
}
