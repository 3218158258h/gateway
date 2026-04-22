#include "../include/app_link_adapter.h"
#include "../include/app_bluetooth.h"
#include "../thirdparty/log.c/log.h"

#include <strings.h>

/* 将接口名称字符串（"serial"/"spi"/"i2c"/"can"）解析为枚举值；
 * 空字符串或 "uart" 也映射为 SERIAL；未知名称返回一个超出枚举范围的值 */
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

/* 接口层初始化：根据 interface_name 判断接口类型并打开设备文件（目前仅支持串口/UART）。
 * 此函数只负责建立通信通道，不涉及任何设备层指令或协议细节。*/
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

/* 设备层配置：从 protocols.ini 加载协议参数，按序发送 init_cmds 完成设备初始化，
 * 并绑定 postRead/preWrite 钩子，使设备进入正常工作状态。*/
int app_link_adapter_apply_protocol(SerialDevice *device, const char *protocol_name)
{
    if (!device) {
        return -1;
    }
    return app_bluetooth_setConnectionType(device, protocol_name);
}
