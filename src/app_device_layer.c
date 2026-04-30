#include "../include/app_device_layer.h"
#include "../include/app_link_adapter.h"
#include "../include/app_bluetooth.h"
#include "../include/app_protocol_config.h"
#include "../thirdparty/log.c/log.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void app_device_layer_set_state(Device *device, DeviceState state)
{
    if (!device) {
        return;
    }
    device->lifecycle_state = state;
}

static int app_device_layer_send_command(SerialDevice *serial_device, const char *cmd)
{
    return app_bluetooth_sendCommand(serial_device, cmd);
}

static int app_device_layer_apply_protocol_non_serial(SerialDevice *serial_device,
                                                       const char *protocol_name,
                                                       const BluetoothProtocolConfig *protocol)
{
    if (!serial_device || !protocol) {
        return -1;
    }

    serial_device->super.connection_type = protocol->connection_type;
    snprintf(serial_device->transport.protocol_name,
             sizeof(serial_device->transport.protocol_name),
             "%s",
             (protocol_name && protocol_name[0]) ? protocol_name : "");

    /*
     * 非串口接口的处理原则：
     * - 可以复用“协议配置中的 connection_type”等元信息
     * - 但不复用串口蓝牙私有帧收发钩子
     */
    if (serial_device->transport.interface_type == APP_INTERFACE_CAN) {
        /* CAN 设备在链路层已绑定 can_post_read/can_pre_write，不覆盖钩子。 */
        app_device_layer_set_state(&serial_device->super, DEVICE_STATE_CONFIGURED);
        log_info("[protocol] event=apply_success device=%s protocol=%s interface=%s connection_type=%d init_cmds=%d ack_len=%d",
                 serial_device->super.filename ? serial_device->super.filename : "",
                 protocol_name ? protocol_name : "",
                 app_transport_interface_to_string(serial_device->transport.interface_type),
                 (int)protocol->connection_type,
                 protocol->init_cmds_count,
                 protocol->ack_frame_len);
        return 0;
    }
    if (serial_device->transport.interface_type == APP_INTERFACE_SPI ||
        serial_device->transport.interface_type == APP_INTERFACE_I2C) {
        /* SPI/I2C 走接口专用事务模型，不绑定蓝牙串口帧钩子。 */
        serial_device->super.vptr->post_read = NULL;
        serial_device->super.vptr->pre_write = NULL;
    }
    app_device_layer_set_state(&serial_device->super, DEVICE_STATE_CONFIGURED);
    log_info("[protocol] event=apply_success device=%s protocol=%s interface=%s connection_type=%d init_cmds=%d ack_len=%d",
             serial_device->super.filename ? serial_device->super.filename : "",
             protocol_name ? protocol_name : "",
             app_transport_interface_to_string(serial_device->transport.interface_type),
             (int)protocol->connection_type,
             protocol->init_cmds_count,
             protocol->ack_frame_len);
    return 0;
}

static int app_device_layer_apply_protocol_serial(SerialDevice *serial_device,
                                                  const char *protocol_name,
                                                  const BluetoothProtocolConfig *protocol)
{
    if (!serial_device || !protocol || !serial_device->super.vptr || serial_device->super.fd < 0) {
        log_error("[protocol] event=apply_rejected reason=invalid_device_handle protocol=%s",
                  protocol_name ? protocol_name : "");
        return -1;
    }

    /* 设备层只编排流程，不直接拼协议字节。 */
    app_device_layer_set_state(&serial_device->super, DEVICE_STATE_CONFIGURING);

    /* 串口协议常见流程：先用 9600 建链配置，再切到工作波特率。 */
    SerialBaudRate work_baud = (protocol->work_baud == 9600)
                               ? SERIAL_BAUD_RATE_9600
                               : SERIAL_BAUD_RATE_115200;

    /* 协议层和传输层在这里完成绑定。 */
    app_bluetooth_clear_context(serial_device->super.fd);
    if (app_bluetooth_set_protocol_config(serial_device, protocol) != 0) {
        log_error("[protocol] event=bind_failed device=%s protocol=%s",
                  serial_device->super.filename ? serial_device->super.filename : "",
                  protocol_name ? protocol_name : "");
        app_device_layer_set_state(&serial_device->super, DEVICE_STATE_ERROR);
        return -1;
    }

    serial_device->super.connection_type = protocol->connection_type;
    snprintf(serial_device->transport.protocol_name,
             sizeof(serial_device->transport.protocol_name),
             "%s",
             (protocol_name && protocol_name[0]) ? protocol_name : "");
    serial_device->super.vptr->post_read = app_bluetooth_postRead;
    serial_device->super.vptr->pre_write = app_bluetooth_preWrite;

    if (app_serial_setBaudRate(serial_device, SERIAL_BAUD_RATE_9600) != 0 ||
        app_serial_setBlockMode(serial_device, 0) != 0 ||
        app_serial_flush(serial_device) != 0) {
        app_device_layer_set_state(&serial_device->super, DEVICE_STATE_ERROR);
        return -1;
    }

    if (protocol->status_cmd[0] != '\0') {
        if (app_device_layer_send_command(serial_device, protocol->status_cmd) != 0) {
            log_error("[protocol] event=status_check_failed device=%s protocol=%s cmd=%s",
                      serial_device->super.filename ? serial_device->super.filename : "",
                      protocol_name ? protocol_name : "",
                      protocol->status_cmd);
            app_device_layer_set_state(&serial_device->super, DEVICE_STATE_ERROR);
            return -1;
        }
    }

    /* 初始化指令支持占位符替换，避免同协议多设备复制粘贴。 */
    for (int i = 0; i < protocol->init_cmds_count; i++) {
        char expanded[APP_PROTOCOL_INIT_CMD_MAX_LEN];
        AppProtocolPlaceholder placeholders[] = {
            {"m_addr", protocol->m_addr},
            {"net_id", protocol->net_id},
            {"baud_code", (work_baud == SERIAL_BAUD_RATE_9600) ? "4" : "8"},
        };
        if (app_private_protocol_expand_template(protocol->init_cmds[i], expanded, sizeof(expanded),
                                                 placeholders, 3) < 0) {
            continue;
        }
        if (app_device_layer_send_command(serial_device, expanded) != 0) {
            log_error("[protocol] event=init_command_failed device=%s protocol=%s index=%d cmd=%s",
                      serial_device->super.filename ? serial_device->super.filename : "",
                      protocol_name ? protocol_name : "",
                      i,
                      expanded);
            app_device_layer_set_state(&serial_device->super, DEVICE_STATE_ERROR);
            return -1;
        }
    }

    if (app_serial_setBaudRate(serial_device, work_baud) != 0 ||
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
    log_info("[protocol] event=apply_success device=%s protocol=%s connection_type=%d init_cmds=%d ack_len=%d",
             serial_device->super.filename ? serial_device->super.filename : "",
             protocol_name ? protocol_name : "",
             (int)protocol->connection_type,
             protocol->init_cmds_count,
             protocol->ack_frame_len);
    return 0;
}

static int app_device_layer_apply_protocol(SerialDevice *serial_device, const char *protocol_name)
{
    if (!serial_device || !serial_device->super.vptr || serial_device->super.fd < 0) {
        log_error("[protocol] event=apply_rejected reason=invalid_device_handle protocol=%s",
                  protocol_name ? protocol_name : "");
        return -1;
    }

    /*
     * 空协议模式：
     * - 设备仍可启动链路收发
     * - 不做私有协议初始化，不挂蓝牙串口封包钩子
     */
    if (!protocol_name || protocol_name[0] == '\0') {
        /* 空协议表示该设备仅启用链路层收发，不做私有协议初始化与组帧。 */
        serial_device->super.connection_type = CONNECTION_TYPE_NONE;
        serial_device->transport.protocol_name[0] = '\0';
        if (serial_device->transport.interface_type == APP_INTERFACE_SERIAL) {
            /* 串口在无私有协议场景下也不挂蓝牙帧处理钩子。 */
            serial_device->super.vptr->post_read = NULL;
            serial_device->super.vptr->pre_write = NULL;
        }
        app_device_layer_set_state(&serial_device->super, DEVICE_STATE_CONFIGURED);
        log_info("[protocol] event=skip_empty device=%s interface=%s",
                 serial_device->super.filename ? serial_device->super.filename : "",
                 app_transport_interface_to_string(serial_device->transport.interface_type));
        return 0;
    }

    app_device_layer_set_state(&serial_device->super, DEVICE_STATE_CONFIGURING);

    BluetoothProtocolConfig protocol = {0};
    if (app_protocol_load_bluetooth(protocol_name, &protocol) != 0) {
        log_error("[protocol] event=load_failed device=%s protocol=%s interface=%s",
                  serial_device->super.filename ? serial_device->super.filename : "",
                  protocol_name ? protocol_name : "",
                  app_transport_interface_to_string(serial_device->transport.interface_type));
        app_device_layer_set_state(&serial_device->super, DEVICE_STATE_ERROR);
        return -1;
    }

    if (serial_device->transport.interface_type == APP_INTERFACE_SERIAL) {
        return app_device_layer_apply_protocol_serial(serial_device, protocol_name, &protocol);
    }

    return app_device_layer_apply_protocol_non_serial(serial_device, protocol_name, &protocol);
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
