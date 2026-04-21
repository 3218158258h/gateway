#ifndef __APP_LINK_ADAPTER_H__
#define __APP_LINK_ADAPTER_H__

#include "app_serial.h"

#define APP_INTERFACE_NAME_MAX_LEN 16

typedef enum AppInterfaceTypeEnum {
    APP_INTERFACE_SERIAL = 0,
    APP_INTERFACE_SPI,
    APP_INTERFACE_I2C,
    APP_INTERFACE_CAN
} AppInterfaceType;

int app_link_adapter_init(SerialDevice *device, const char *device_path, const char *interface_name);
int app_link_adapter_apply_protocol(SerialDevice *device, const char *protocol_name);

#endif
