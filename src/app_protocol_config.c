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

#include <ctype.h>
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
 * @brief 将十六进制字符串解析为字节数组
 *
 * 支持忽略中间空白字符，不区分大小写。
 * 用于解析 frame_header / frame_tail 配置项。
 *
 * @param hex       输入十六进制字符串（如 "F1DD"）
 * @param out       输出字节数组
 * @param max_len   输出数组最大长度
 * @param out_len   实际输出字节数
 * @return 0 成功；-1 失败（格式非法或超长）
 */
static int app_protocol_parse_hex_bytes(const char *hex, unsigned char *out, int max_len, int *out_len)
{
    if (!hex || !out || !out_len || max_len <= 0) {
        return -1;
    }

    /* 去除空白并转大写 */
    char normalized[CONFIG_MAX_VALUE_LEN] = {0};
    int normalized_len = 0;
    for (int i = 0; hex[i] != '\0' && normalized_len < (int)sizeof(normalized) - 1; i++) {
        if (!isspace((unsigned char)hex[i])) {
            normalized[normalized_len++] = (char)toupper((unsigned char)hex[i]);
        }
    }
    if (normalized_len == 0 || (normalized_len % 2) != 0) {
        return -1;
    }

    int bytes_len = normalized_len / 2;
    if (bytes_len > max_len) {
        return -1;
    }
    for (int i = 0; i < bytes_len; i++) {
        char byte_str[3] = {normalized[i * 2], normalized[i * 2 + 1], '\0'};
        unsigned int byte_value = 0;
        if (sscanf(byte_str, "%2x", &byte_value) != 1) {
            return -1;
        }
        out[i] = (unsigned char)byte_value;
    }
    *out_len = bytes_len;
    return 0;
}

/**
 * @brief 将指令字符串中的 \r \n 字面量（2字节）转义为实际 CR/LF 控制字符
 *
 * 配置文件中写 AT\r\n，加载后存储为实际 0x0D 0x0A，
 * 这样指令可直接传给 write()。
 *
 * @param s 待处理的字符串（原地修改）
 */
static void app_protocol_unescape_cmd(char *s)
{
    if (!s) {
        return;
    }
    char *src = s;
    char *dst = s;
    while (*src != '\0') {
        if (src[0] == '\\' && src[1] == 'r') {
            *dst++ = '\r';
            src += 2;
        } else if (src[0] == '\\' && src[1] == 'n') {
            *dst++ = '\n';
            src += 2;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

/**
 * @brief 解析分号分隔的指令序列字符串，存入输出数组
 *
 * 每条指令末尾的 \r\n 字面量会被自动转义为实际字节。
 * 空指令（仅空白）会被跳过。
 *
 * @param cmds_str    分号分隔的指令序列字符串（来自配置文件）
 * @param out         输出指令数组
 * @param max_count   输出数组最大条数
 * @return 解析出的有效指令条数
 */
static int app_protocol_parse_init_cmds(const char *cmds_str,
                                        char out[][APP_PROTOCOL_INIT_CMD_MAX_LEN],
                                        int max_count)
{
    if (!cmds_str || !out || max_count <= 0) {
        return 0;
    }

    /* 复制一份以便 strtok_r 修改 */
    char buf[CONFIG_MAX_VALUE_LEN];
    snprintf(buf, sizeof(buf), "%s", cmds_str);

    int count = 0;
    char *saveptr = NULL;
    char *token = strtok_r(buf, ";", &saveptr);
    while (token && count < max_count) {
        /* 去除首尾空白 */
        while (*token == ' ' || *token == '\t') {
            token++;
        }
        size_t tlen = strlen(token);
        while (tlen > 0 && (token[tlen - 1] == ' ' || token[tlen - 1] == '\t')) {
            token[--tlen] = '\0';
        }
        if (tlen == 0) {
            token = strtok_r(NULL, ";", &saveptr);
            continue;
        }
        snprintf(out[count], APP_PROTOCOL_INIT_CMD_MAX_LEN, "%s", token);
        /* 将 \r \n 字面量转义为实际字节 */
        app_protocol_unescape_cmd(out[count]);
        count++;
        token = strtok_r(NULL, ";", &saveptr);
    }
    return count;
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
        if (app_protocol_parse_hex_bytes(value, config->frame_header, APP_PROTOCOL_MAX_FRAME_BYTES,
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
        if (app_protocol_parse_hex_bytes(value, config->frame_tail, APP_PROTOCOL_MAX_FRAME_BYTES,
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

    /* 解析状态检查指令（\r\n 字面量自动转义为实际字节） */
    if (config_get_string(&cfg_mgr, section, "status_cmd", "", value, sizeof(value)) == 0 &&
        value[0] != '\0') {
        snprintf(config->status_cmd, sizeof(config->status_cmd), "%s", value);
        app_protocol_unescape_cmd(config->status_cmd);
    }

    /* 解析设备配置指令序列（分号分隔，\r\n 自动转义） */
    if (config_get_string(&cfg_mgr, section, "init_cmds", "", value, sizeof(value)) == 0 &&
        value[0] != '\0') {
        int count = app_protocol_parse_init_cmds(value, config->init_cmds, APP_PROTOCOL_MAX_INIT_CMDS);
        if (count > 0) {
            config->init_cmds_count = count;
        }
    }

    config_destroy(&cfg_mgr);
    return 0;
}
