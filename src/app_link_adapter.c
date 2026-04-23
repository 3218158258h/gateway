#include "../include/app_link_adapter.h"
#include "../thirdparty/log.c/log.h"

/* 接口层初始化：根据 interface_name 判断接口类型并打开设备文件（目前仅支持串口/UART）。
 * 这里只负责建立通信通道，不涉及任何设备层指令或协议细节。 */
int app_link_adapter_init(SerialDevice *device, const char *device_path, const char *interface_name)
{
    if (!device || !device_path || device_path[0] == '\0') {
        return -1;
    }

    AppInterfaceType interface_type = app_transport_string_to_interface(interface_name);
    if (interface_type != APP_INTERFACE_SERIAL) {
        log_error("Unsupported interface '%s' for now, only serial is implemented",
                  interface_name ? interface_name : "");
        return -1;
    }

    int result = app_serial_init(device, (char *)device_path);
    if (result == 0) {
        device->transport.interface_type = interface_type;
    }
    return result;
}

/* 设备层配置：从 protocols.ini 加载协议参数，按序发送 init_cmds 完成设备初始化，
 * 并绑定 postRead/preWrite 钩子，使设备进入正常工作状态。 */
int app_link_adapter_apply_protocol(SerialDevice *device, const char *protocol_name)
{
    if (!device) {
        return -1;
    }
    return app_device_layer_configure(device, protocol_name);
}
