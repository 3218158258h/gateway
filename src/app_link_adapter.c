#include "../include/app_link_adapter.h"
#include "../include/app_config.h"
#include "../thirdparty/log.c/log.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/spi/spidev.h>
#include <linux/i2c-dev.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>

#define LINK_ADAPTER_TMP_DEVICE "/dev/null"
#define CAN_MESSAGE_ID_LEN 4
#define CAN_MESSAGE_MAX_DATA_LEN 8

static const char *default_section_by_type(AppInterfaceType interface_type)
{
    switch (interface_type) {
    case APP_INTERFACE_SPI:
        return "transport.spi_default";
    case APP_INTERFACE_I2C:
        return "transport.i2c_default";
    case APP_INTERFACE_CAN:
        return "transport.can_default";
    case APP_INTERFACE_SERIAL:
    default:
        return "transport.serial_default";
    }
}

static int section_interface_matches(ConfigManager *cfg_mgr, const char *section,
                                     AppInterfaceType expected_type)
{
    char value[CONFIG_MAX_VALUE_LEN] = {0};
    if (config_get_string(cfg_mgr, section, "interface", "", value, sizeof(value)) != 0 ||
        value[0] == '\0') {
        return 1;
    }
    return app_transport_string_to_interface(value) == expected_type;
}

static int load_physical_config_for_device(SerialDevice *device, const char *device_path,
                                           AppInterfaceType interface_type)
{
    if (!device || !device_path) {
        return -1;
    }

    app_transport_config_init(device);
    device->transport.interface_type = interface_type;
    snprintf(device->transport.device_path, sizeof(device->transport.device_path), "%s", device_path);

    ConfigManager cfg_mgr = {0};
    if (config_init(&cfg_mgr, APP_PHYSICAL_TRANSPORT_CONFIG_FILE) != 0 ||
        config_load(&cfg_mgr) != 0) {
        config_destroy(&cfg_mgr);
        return 0;
    }

    const char *matched_section = NULL;
    for (int i = 0; i < cfg_mgr.item_count; i++) {
        ConfigItem *item = &cfg_mgr.items[i];
        if (strcmp(item->key, "device_path") != 0) {
            continue;
        }
        if (strcmp(item->value, device_path) != 0) {
            continue;
        }
        if (!section_interface_matches(&cfg_mgr, item->section, interface_type)) {
            continue;
        }
        matched_section = item->section;
        break;
    }

    if (!matched_section) {
        matched_section = default_section_by_type(interface_type);
    }

    (void)app_transport_config_load(device, APP_PHYSICAL_TRANSPORT_CONFIG_FILE, matched_section);
    device->transport.interface_type = interface_type;
    snprintf(device->transport.device_path, sizeof(device->transport.device_path), "%s", device_path);
    config_destroy(&cfg_mgr);
    return 0;
}

static int can_post_read(Device *device, void *ptr, int *len)
{
    if (!device || !ptr || !len || *len <= 0) {
        return -1;
    }

    if (*len < (int)sizeof(struct can_frame)) {
        *len = 0;
        return 0;
    }

    struct can_frame *frame = (struct can_frame *)ptr;
    unsigned char out[3 + CAN_MESSAGE_ID_LEN + CAN_MESSAGE_MAX_DATA_LEN];
    int data_len = frame->can_dlc;
    if (data_len < 0) {
        data_len = 0;
    }
    if (data_len > CAN_MESSAGE_MAX_DATA_LEN) {
        data_len = CAN_MESSAGE_MAX_DATA_LEN;
    }

    out[0] = (unsigned char)device->connection_type;
    out[1] = CAN_MESSAGE_ID_LEN;
    out[2] = (unsigned char)data_len;
    out[3] = (unsigned char)((frame->can_id >> 24) & 0xFF);
    out[4] = (unsigned char)((frame->can_id >> 16) & 0xFF);
    out[5] = (unsigned char)((frame->can_id >> 8) & 0xFF);
    out[6] = (unsigned char)(frame->can_id & 0xFF);
    if (data_len > 0) {
        memcpy(out + 7, frame->data, (size_t)data_len);
    }
    memcpy(ptr, out, (size_t)(7 + data_len));
    *len = 7 + data_len;
    return 0;
}

static int can_pre_write(Device *device, void *ptr, int *len)
{
    (void)device;
    if (!ptr || !len || *len < 3) {
        return -1;
    }

    unsigned char *buf = (unsigned char *)ptr;
    int id_len = buf[1];
    int data_len = buf[2];
    int total_len = 3 + id_len + data_len;
    if (id_len < 0 || data_len < 0 || total_len > *len) {
        return -1;
    }

    struct can_frame frame;
    memset(&frame, 0, sizeof(frame));
    frame.can_id = 0;
    int copy_id_len = id_len > CAN_MESSAGE_ID_LEN ? CAN_MESSAGE_ID_LEN : id_len;
    for (int i = 0; i < copy_id_len; i++) {
        frame.can_id = (frame.can_id << 8) | buf[3 + i];
    }
    if (data_len > CAN_MESSAGE_MAX_DATA_LEN) {
        data_len = CAN_MESSAGE_MAX_DATA_LEN;
    }
    frame.can_dlc = (unsigned char)data_len;
    if (data_len > 0) {
        memcpy(frame.data, buf + 3 + id_len, (size_t)data_len);
    }

    memcpy(ptr, &frame, sizeof(frame));
    *len = (int)sizeof(frame);
    return 0;
}

static int init_can_device(SerialDevice *device, const char *device_path)
{
    if (app_device_init(&device->super, LINK_ADAPTER_TMP_DEVICE) != 0) {
        return -1;
    }

    int sock_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock_fd < 0) {
        log_error("Failed to create CAN socket for %s: %s", device_path, strerror(errno));
        app_device_close(&device->super);
        return -1;
    }

    int loopback = device->transport.can.loopback ? 1 : 0;
    if (setsockopt(sock_fd, SOL_CAN_RAW, CAN_RAW_LOOPBACK, &loopback, sizeof(loopback)) < 0) {
        log_warn("Failed to set CAN loopback for %s: %s", device_path, strerror(errno));
    }
    int can_fd_enabled = device->transport.can.fd_mode ? 1 : 0;
    if (setsockopt(sock_fd, SOL_CAN_RAW, CAN_RAW_FD_FRAMES,
                   &can_fd_enabled, sizeof(can_fd_enabled)) < 0) {
        log_warn("Failed to set CAN FD mode for %s: %s", device_path, strerror(errno));
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", device_path);
    if (ioctl(sock_fd, SIOCGIFINDEX, &ifr) < 0) {
        log_error("Failed to get CAN ifindex for %s: %s", device_path, strerror(errno));
        close(sock_fd);
        app_device_close(&device->super);
        return -1;
    }

    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error("Failed to bind CAN socket for %s: %s", device_path, strerror(errno));
        close(sock_fd);
        app_device_close(&device->super);
        return -1;
    }

    int old_fd = device->super.fd;
    device->super.fd = sock_fd;
    if (old_fd >= 0) {
        close(old_fd);
    }
    if (device->super.filename) {
        free(device->super.filename);
    }
    device->super.filename = strdup(device_path);
    if (!device->super.filename) {
        log_error("Failed to save CAN device path: %s", device_path);
        app_device_close(&device->super);
        return -1;
    }

    device->super.vptr->post_read = can_post_read;
    device->super.vptr->pre_write = can_pre_write;
    return 0;
}

static int init_spi_device(SerialDevice *device, const char *device_path)
{
    if (app_device_init(&device->super, (char *)device_path) != 0) {
        return -1;
    }

    uint8_t mode = (uint8_t)device->transport.spi.mode;
    uint8_t bits = (uint8_t)device->transport.spi.bits_per_word;
    uint8_t lsb_first = (uint8_t)(device->transport.spi.lsb_first ? 1 : 0);
    uint32_t speed = (uint32_t)device->transport.spi.clock_hz;

    if (ioctl(device->super.fd, SPI_IOC_WR_MODE, &mode) < 0) {
        log_warn("SPI set mode failed for %s: %s", device_path, strerror(errno));
    }
    if (ioctl(device->super.fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) {
        log_warn("SPI set bits_per_word failed for %s: %s", device_path, strerror(errno));
    }
    if (ioctl(device->super.fd, SPI_IOC_WR_LSB_FIRST, &lsb_first) < 0) {
        log_warn("SPI set lsb_first failed for %s: %s", device_path, strerror(errno));
    }
    if (ioctl(device->super.fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        log_warn("SPI set clock_hz failed for %s: %s", device_path, strerror(errno));
    }
    return 0;
}

static int init_i2c_device(SerialDevice *device, const char *device_path)
{
    if (app_device_init(&device->super, (char *)device_path) != 0) {
        return -1;
    }

    if (ioctl(device->super.fd, I2C_TENBIT, device->transport.i2c.ten_bit_address) < 0) {
        log_warn("I2C set ten_bit_address failed for %s: %s", device_path, strerror(errno));
    }
    if (ioctl(device->super.fd, I2C_SLAVE, device->transport.i2c.address) < 0) {
        log_warn("I2C set slave address failed for %s: %s", device_path, strerror(errno));
    }
    return 0;
}

/* 接口层初始化：根据 interface_name 判断接口类型并打开设备文件（支持 serial/uart/spi/i2c/can）。
 * 这里只负责建立通信通道，不涉及任何设备层指令或协议细节。 */
int app_link_adapter_init(SerialDevice *device, const char *device_path, const char *interface_name)
{
    if (!device || !device_path || device_path[0] == '\0') {
        return -1;
    }

    AppInterfaceType interface_type = app_transport_string_to_interface(interface_name);
    if (interface_type < APP_INTERFACE_SERIAL || interface_type > APP_INTERFACE_CAN) {
        log_error("Unsupported interface '%s'", interface_name ? interface_name : "");
        return -1;
    }

    load_physical_config_for_device(device, device_path, interface_type);

    int result = -1;
    switch (interface_type) {
    case APP_INTERFACE_SERIAL:
        result = app_serial_init(device, (char *)device_path);
        break;
    case APP_INTERFACE_SPI:
        result = init_spi_device(device, device_path);
        break;
    case APP_INTERFACE_I2C:
        result = init_i2c_device(device, device_path);
        break;
    case APP_INTERFACE_CAN:
        result = init_can_device(device, device_path);
        break;
    default:
        result = -1;
        break;
    }

    if (result == 0) {
        device->transport.interface_type = interface_type;
        log_info("Link adapter initialized: interface=%s path=%s",
                 app_transport_interface_to_string(interface_type), device_path);
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
