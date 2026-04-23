/**
 * @file app_protocol_config.c
 * @brief 设备协议配置加载模块
 *
 * 功能说明：
 * - 从 config/protocols.ini 读取指定协议节（[protocol.<name>]）的全部参数
 * - 参数包括：帧格式（帧头/帧尾/ID长度）、下行指令前缀、
 *             状态检查指令（status_cmd）、设备配置指令序列（init_cmds）
 * - init_cmds 支持占位符 {m_addr} {net_id} {baud_code}，由设备层在运行时替换
 * - 指令模板中的 \r \n 字面量在加载时自动转义为实际 CR/LF 字节
 * - 加载失败时回退到内置默认值，保证系统可启动
 */

#include "../include/app_protocol_config.h"
#include "../include/app_config.h"
#include "../thirdparty/log.c/log.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

/* 未指定协议名称时使用的默认协议 */
#define DEFAULT_PROTOCOL_NAME "ble_mesh_default"

/**
 * @brief 将 connection_type 字符串解析为枚举值
 *
 * @param value          配置文件中的字符串值
 * @param connection_type 输出枚举值
 * @return 0 解析成功；-1 未知类型
 */
static int app_protocol_parse_connection_type(const char *value, ConnectionType *connection_type)
{
    if (!value || !connection_type) {
        return -1;
    }
    if (strcasecmp(value, "ble_mesh") == 0) {
        *connection_type = CONNECTION_TYPE_BLE_MESH;
        return 0;
    }
    if (strcasecmp(value, "lora") == 0) {
        *connection_type = CONNECTION_TYPE_LORA;
        return 0;
    }
    if (strcasecmp(value, "none") == 0) {
        *connection_type = CONNECTION_TYPE_NONE;
        return 0;
    }
    return -1;
}

/**
 * @brief 填充协议配置默认值（ble_mesh_default 协议参数）
 *
 * 当配置文件不存在或指定字段缺失时，使用这里的默认值，
 * 保证系统可在无配置文件的情况下启动。
 *
 * @param config 输出配置结构体指针
 */
static void app_protocol_load_defaults(BluetoothProtocolConfig *config)
{
    if (!config) {
        return;
    }
    memset(config, 0, sizeof(*config));

    /* 帧格式默认值 */
    config->connection_type = CONNECTION_TYPE_BLE_MESH;
    config->frame_header[0] = 0xF1;
    config->frame_header[1] = 0xDD;
    config->frame_header_len = 2;
    config->frame_tail_len = 0;
    config->id_len = 2;
    snprintf(config->mesh_cmd_prefix, sizeof(config->mesh_cmd_prefix), "%s", "AT+MESH");

    /* 设备层默认初始化指令：状态检查 + 地址/网络ID/波特率/复位 */
    snprintf(config->status_cmd, sizeof(config->status_cmd), "AT\r\n");
    snprintf(config->init_cmds[0], APP_PROTOCOL_INIT_CMD_MAX_LEN, "AT+MADDR{m_addr}\r\n");
    snprintf(config->init_cmds[1], APP_PROTOCOL_INIT_CMD_MAX_LEN, "AT+NETID{net_id}\r\n");
    snprintf(config->init_cmds[2], APP_PROTOCOL_INIT_CMD_MAX_LEN, "AT+BAUD{baud_code}\r\n");
    snprintf(config->init_cmds[3], APP_PROTOCOL_INIT_CMD_MAX_LEN, "AT+RESET\r\n");
    config->init_cmds_count = 4;
}

/**
 * @brief 从协议配置文件加载蓝牙/无线设备协议参数
 *
 * 读取 config/protocols.ini 中 [protocol.<protocol_name>] 节：
 *   - connection_type: 连接类型字符串
 *   - frame_header: 帧头十六进制字节串
 *   - frame_tail: 帧尾十六进制字节串（留空表示无帧尾校验）
 *   - id_len: 设备ID字节数
 *   - mesh_cmd_prefix: 下行Mesh指令前缀
 *   - status_cmd: 状态检查指令模板（\r\n 自动转义）
 *   - init_cmds: 分号分隔的设备配置指令序列（\r\n 自动转义）
 *
 * 加载失败或字段缺失时保留 app_protocol_load_defaults 设定的默认值。
 *
 * @param protocol_name 协议名称（对应 protocols.ini [protocol.<name>]）
 * @param config        输出协议配置结构体指针
 * @return 0 成功（含回退默认值）；-1 参数错误
 */
int app_protocol_load_bluetooth(const char *protocol_name, BluetoothProtocolConfig *config)
{
    if (!config) {
        return -1;
    }

    /* 先填入默认值，后续逐字段覆盖 */
    app_protocol_load_defaults(config);

    const char *resolved_name = protocol_name && protocol_name[0] ? protocol_name : DEFAULT_PROTOCOL_NAME;
    char section[CONFIG_MAX_SECTION_LEN] = {0};
    snprintf(section, sizeof(section), "protocol.%s", resolved_name);

    ConfigManager cfg_mgr = {0};
    if (config_init(&cfg_mgr, APP_PROTOCOL_CONFIG_FILE) != 0 || config_load(&cfg_mgr) != 0) {
        log_warn("Protocol config load failed (%s), using defaults for %s",
                 APP_PROTOCOL_CONFIG_FILE, resolved_name);
        return 0;
    }

    char value[CONFIG_MAX_VALUE_LEN] = {0};

    /* 解析 connection_type */
    if (config_get_string(&cfg_mgr, section, "connection_type", "", value, sizeof(value)) == 0 &&
        value[0] != '\0') {
        ConnectionType connection_type;
        if (app_protocol_parse_connection_type(value, &connection_type) == 0) {
            config->connection_type = connection_type;
        } else {
            log_warn("Invalid %s.connection_type=%s, fallback default", section, value);
        }
    }

    /* 解析帧头（十六进制字节串） */
    if (config_get_string(&cfg_mgr, section, "frame_header", "", value, sizeof(value)) == 0 &&
        value[0] != '\0') {
        int frame_header_len = 0;
        if (app_private_protocol_parse_hex_bytes(value, config->frame_header, APP_PROTOCOL_MAX_FRAME_BYTES,
                                          &frame_header_len) == 0) {
            config->frame_header_len = frame_header_len;
        } else {
            log_warn("Invalid %s.frame_header=%s, fallback default", section, value);
        }
    }

    /* 解析帧尾（十六进制字节串，留空表示无帧尾） */
    if (config_get_string(&cfg_mgr, section, "frame_tail", "", value, sizeof(value)) == 0 &&
        value[0] != '\0') {
        int frame_tail_len = 0;
        if (app_private_protocol_parse_hex_bytes(value, config->frame_tail, APP_PROTOCOL_MAX_FRAME_BYTES,
                                          &frame_tail_len) == 0) {
            config->frame_tail_len = frame_tail_len;
        } else {
            log_warn("Invalid %s.frame_tail=%s, ignore", section, value);
            config->frame_tail_len = 0;
        }
    }

    /* 解析 id_len（设备ID字节数，范围 1~8） */
    config->id_len = config_get_int(&cfg_mgr, section, "id_len", config->id_len);
    if (config->id_len <= 0 || config->id_len > 8) {
        log_warn("Invalid %s.id_len=%d, fallback default", section, config->id_len);
        config->id_len = 2;
    }

    /* 解析下行指令前缀 */
    if (config_get_string(&cfg_mgr, section, "mesh_cmd_prefix", "",
                          value, sizeof(value)) == 0 &&
        value[0] != '\0') {
        snprintf(config->mesh_cmd_prefix, sizeof(config->mesh_cmd_prefix), "%s", value);
    }

    /* 解析状态检查指令（允许为空，空表示跳过状态检查） */
    if (config_get_string(&cfg_mgr, section, "status_cmd", "", value, sizeof(value)) == 0) {
        snprintf(config->status_cmd, sizeof(config->status_cmd), "%s", value);
        app_private_protocol_unescape_cmd(config->status_cmd);
    }

    /* 解析 ACK 帧（留空表示不等待 ACK） */
    if (config_get_string(&cfg_mgr, section, "ack_frame", "", value, sizeof(value)) == 0) {
        if (value[0] == '\0') {
            memset(config->ack_frame, 0, sizeof(config->ack_frame));
            config->ack_frame_len = 0;
        } else {
            int ack_len = 0;
            if (app_private_protocol_parse_hex_bytes(value, config->ack_frame, APP_PROTOCOL_MAX_FRAME_BYTES,
                                                     &ack_len) == 0) {
                config->ack_frame_len = ack_len;
            } else {
                log_warn("Invalid %s.ack_frame=%s, fallback default", section, value);
            }
        }
    }

    /* 解析 NACK 帧（十六进制字节串，留空表示不启用） */
    if (config_get_string(&cfg_mgr, section, "nack_frame", "", value, sizeof(value)) == 0 &&
        value[0] != '\0') {
        int nack_len = 0;
        if (app_private_protocol_parse_hex_bytes(value, config->nack_frame, APP_PROTOCOL_MAX_FRAME_BYTES,
                                                 &nack_len) == 0) {
            config->nack_frame_len = nack_len;
        } else {
            log_warn("Invalid %s.nack_frame=%s, ignore", section, value);
            config->nack_frame_len = 0;
        }
    }

    /* 解析设备配置指令序列（分号分隔，\r\n 自动转义） */
    if (config_get_string(&cfg_mgr, section, "init_cmds", "", value, sizeof(value)) == 0 &&
        value[0] != '\0') {
        int count = app_private_protocol_parse_init_cmds(value, config->init_cmds, APP_PROTOCOL_MAX_INIT_CMDS);
        if (count > 0) {
            config->init_cmds_count = count;
        }
    }

    config_destroy(&cfg_mgr);
    return 0;
}
