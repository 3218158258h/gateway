#ifndef __APP_DEVICE_LAYER_H__
#define __APP_DEVICE_LAYER_H__

#include "app_serial.h"

/* 设备层初始化：打开设备并按 interface_name 选择对应通信接口初始化路径。 */
int app_device_layer_init(SerialDevice *device, const char *device_path, const char *interface_name);
/* 应用协议层配置：加载协议参数、下发初始化指令、绑定收发处理钩子。 */
int app_device_layer_configure(SerialDevice *device, const char *protocol_name);
/* 启动设备收发任务。 */
int app_device_layer_start(Device *device);
/* 停止设备收发任务。 */
int app_device_layer_stop(Device *device);
/* 关闭设备并释放设备层资源。 */
void app_device_layer_close(Device *device);

#endif
