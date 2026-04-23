#include "../include/app_device_layer.h"
#include "../include/app_link_adapter.h"
#include "../include/app_bluetooth.h"
#include "../include/app_protocol_config.h"
#include "../include/app_config.h"
#include "../thirdparty/log.c/log.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define DEVICE_LAYER_ADDR_LEN 4
#define DEVICE_LAYER_DEFAULT_MADDR "0001"
#define DEVICE_LAYER_DEFAULT_NETID "1111"
#define DEVICE_LAYER_DEFAULT_WORK_BAUD 115200

typedef struct DeviceLayerRuntimeConfigStruct {
    char m_addr[DEVICE_LAYER_ADDR_LEN + 1];
    char net_id[DEVICE_LAYER_ADDR_LEN + 1];
    SerialBaudRate work_baud;
} DeviceLayerRuntimeConfig;

static DeviceLayerRuntimeConfig g_runtime_config;
static int g_runtime_config_loaded = 0;

static void app_device_layer_set_state(Device *device, DeviceState state)
{
    if (!device) {
        return;
    }
    device->lifecycle_state = state;
}

static int app_device_layer_load_runtime_config(DeviceLayerRuntimeConfig *runtime)
{
    if (!runtime) {
        return -1;
    }

    if (g_runtime_config_loaded) {
        *runtime = g_runtime_config;
        return 0;
    }

    /* 先加载默认值，确保配置缺失时仍有可运行的兜底参数。 */
    snprintf(runtime->m_addr, sizeof(runtime->m_addr), "%s", DEVICE_LAYER_DEFAULT_MADDR);
    snprintf(runtime->net_id, sizeof(runtime->net_id), "%s", DEVICE_LAYER_DEFAULT_NETID);
    runtime->work_baud = (DEVICE_LAYER_DEFAULT_WORK_BAUD == 9600)
        ? SERIAL_BAUD_RATE_9600
        : SERIAL_BAUD_RATE_115200;

    ConfigManager cfg_mgr = {0};
    if (config_init(&cfg_mgr, APP_GATEWAY_CONFIG_FILE) != 0) {
        g_runtime_config = *runtime;
        g_runtime_config_loaded = 1;
        return 0;
    }
    if (config_load(&cfg_mgr) != 0) {
        config_destroy(&cfg_mgr);
        g_runtime_config = *runtime;
        g_runtime_config_loaded = 1;
        return 0;
    }

    char value[CONFIG_MAX_VALUE_LEN] = {0};
    /* 运行参数来自 gateway.ini 的 bluetooth 节，避免写死在设备逻辑里。 */
    if (config_get_string(&cfg_mgr, "bluetooth", "m_addr", DEVICE_LAYER_DEFAULT_MADDR,
                          value, sizeof(value)) == 0 && strlen(value) == DEVICE_LAYER_ADDR_LEN) {
        snprintf(runtime->m_addr, sizeof(runtime->m_addr), "%s", value);
    }
    if (config_get_string(&cfg_mgr, "bluetooth", "net_id", DEVICE_LAYER_DEFAULT_NETID,
                          value, sizeof(value)) == 0 && strlen(value) == DEVICE_LAYER_ADDR_LEN) {
        snprintf(runtime->net_id, sizeof(runtime->net_id), "%s", value);
    }

    int baud_rate = config_get_int(&cfg_mgr, "bluetooth", "baud_rate", DEVICE_LAYER_DEFAULT_WORK_BAUD);
    if (baud_rate == 9600) {
        runtime->work_baud = SERIAL_BAUD_RATE_9600;
    } else {
        runtime->work_baud = SERIAL_BAUD_RATE_115200;
    }

    config_destroy(&cfg_mgr);
    g_runtime_config = *runtime;
    g_runtime_config_loaded = 1;
    return 0;
}

static int app_device_layer_send_command(SerialDevice *serial_device, const char *cmd)
{
    return app_bluetooth_sendCommand(serial_device, cmd);
}

static int app_device_layer_apply_protocol(SerialDevice *serial_device, const char *protocol_name)
{
    if (!serial_device || !serial_device->super.vptr || serial_device->super.fd < 0) {
        return -1;
    }

    /* 设备层只编排流程，不直接拼协议字节。 */
    app_device_layer_set_state(&serial_device->super, DEVICE_STATE_CONFIGURING);

    DeviceLayerRuntimeConfig runtime = {0};
    app_device_layer_load_runtime_config(&runtime);

    BluetoothProtocolConfig protocol = {0};
    if (app_protocol_load_bluetooth(protocol_name, &protocol) != 0) {
        app_device_layer_set_state(&serial_device->super, DEVICE_STATE_ERROR);
        return -1;
    }

    /* 协议层和传输层在这里完成绑定。 */
    app_bluetooth_clear_context(serial_device->super.fd);
    if (app_bluetooth_set_protocol_config(serial_device, &protocol) != 0) {
        app_device_layer_set_state(&serial_device->super, DEVICE_STATE_ERROR);
        return -1;
    }

    serial_device->super.connection_type = protocol.connection_type;
    serial_device->super.vptr->post_read = app_bluetooth_postRead;
    serial_device->super.vptr->pre_write = app_bluetooth_preWrite;

    if (app_serial_setBaudRate(serial_device, SERIAL_BAUD_RATE_9600) != 0 ||
        app_serial_setBlockMode(serial_device, 0) != 0 ||
        app_serial_flush(serial_device) != 0) {
        app_device_layer_set_state(&serial_device->super, DEVICE_STATE_ERROR);
        return -1;
    }

    if (protocol.status_cmd[0] != '\0') {
        if (app_device_layer_send_command(serial_device, protocol.status_cmd) != 0) {
            app_device_layer_set_state(&serial_device->super, DEVICE_STATE_ERROR);
            return -1;
        }
    }

    for (int i = 0; i < protocol.init_cmds_count; i++) {
        char expanded[APP_PROTOCOL_INIT_CMD_MAX_LEN];
        AppProtocolPlaceholder placeholders[] = {
            {"m_addr", runtime.m_addr},
            {"net_id", runtime.net_id},
            {"baud_code", (runtime.work_baud == SERIAL_BAUD_RATE_9600) ? "4" : "8"},
        };
        if (app_private_protocol_expand_template(protocol.init_cmds[i], expanded, sizeof(expanded),
                                                 placeholders, 3) < 0) {
            continue;
        }
        if (app_device_layer_send_command(serial_device, expanded) != 0) {
            app_device_layer_set_state(&serial_device->super, DEVICE_STATE_ERROR);
            return -1;
        }
    }

    if (app_serial_setBaudRate(serial_device, runtime.work_baud) != 0 ||
        app_serial_setBlockMode(serial_device, 1) != 0) {
        app_device_layer_set_state(&serial_device->super, DEVICE_STATE_ERROR);
        return -1;
    }
    sleep(1);
    if (app_serial_flush(serial_device) != 0) {
        app_device_layer_set_state(&serial_device->super, DEVICE_STATE_ERROR);
        return -1;
    }

    app_device_layer_set_state(&serial_device->super, DEVICE_STATE_CONFIGURED);
    return 0;
}

int app_device_layer_init(SerialDevice *device, const char *device_path, const char *interface_name)
{
    if (!device || !device_path || device_path[0] == '\0') {
        return -1;
    }

    int result = app_link_adapter_init(device, device_path, interface_name);
    if (result == 0) {
        app_device_layer_set_state(&device->super, DEVICE_STATE_INITIALIZED);
    }
    return result;
}

int app_device_layer_configure(SerialDevice *device, const char *protocol_name)
{
    if (!device) {
        return -1;
    }
    DeviceState state = app_device_get_state(&device->super);
    if (state == DEVICE_STATE_UNINITIALIZED || state == DEVICE_STATE_RUNNING ||
        state == DEVICE_STATE_ERROR) {
        return -1;
    }
    return app_device_layer_apply_protocol(device, protocol_name);
}

int app_device_layer_start(Device *device)
{
    if (!device) {
        return -1;
    }
    DeviceState state = app_device_get_state(device);
    if (state != DEVICE_STATE_CONFIGURED && state != DEVICE_STATE_STOPPED) {
        return -1;
    }
    if (state == DEVICE_STATE_RUNNING) {
        return 0;
    }
    if (app_device_start(device) != 0) {
        app_device_layer_set_state(device, DEVICE_STATE_ERROR);
        return -1;
    }
    app_device_layer_set_state(device, DEVICE_STATE_RUNNING);
    return 0;
}

int app_device_layer_stop(Device *device)
{
    if (!device) {
        return -1;
    }
    DeviceState state = app_device_get_state(device);
    if (state == DEVICE_STATE_UNINITIALIZED) {
        return 0;
    }
    app_device_stop(device);
    if (state != DEVICE_STATE_ERROR) {
        app_device_layer_set_state(device, DEVICE_STATE_STOPPED);
    }
    return 0;
}

void app_device_layer_close(Device *device)
{
    if (!device) {
        return;
    }
    int device_fd = device->fd;
    app_device_stop(device);
    app_bluetooth_clear_context(device_fd);
    app_device_close(device);
    app_device_layer_set_state(device, DEVICE_STATE_UNINITIALIZED);
}
