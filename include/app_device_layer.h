#ifndef __APP_DEVICE_LAYER_H__
#define __APP_DEVICE_LAYER_H__

#include "app_serial.h"

int app_device_layer_init(SerialDevice *device, const char *device_path, const char *interface_name);
int app_device_layer_configure(SerialDevice *device, const char *protocol_name);
int app_device_layer_start(Device *device);
int app_device_layer_stop(Device *device);
void app_device_layer_close(Device *device);

#endif
