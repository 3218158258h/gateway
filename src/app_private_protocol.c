#include "../include/app_private_protocol.h"
#include "../include/app_config.h"
#include "../thirdparty/log.c/log.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int private_protocol_placeholder_index(const AppProtocolPlaceholder *placeholders, int count, const char *name)
{
    if (!placeholders || !name) {
        return -1;
    }
    for (int i = 0; i < count; i++) {
        if (placeholders[i].name && strcmp(placeholders[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

void app_private_protocol_load_defaults(PrivateProtocolConfig *config)
{
    if (!config) {
        return;
    }

    /* 默认值要能直接覆盖最常见的 BLE Mesh 场景。 */
    memset(config, 0, sizeof(*config));
    config->connection_type = CONNECTION_TYPE_BLE_MESH;
    config->frame_header[0] = 0xF1;
    config->frame_header[1] = 0xDD;
    config->frame_header_len = 2;
    config->ack_frame[0] = 0x4F;
    config->ack_frame[1] = 0x4B;
    config->ack_frame[2] = 0x0D;
    config->ack_frame[3] = 0x0A;
    config->ack_frame_len = 4;
    config->id_len = 2;
    snprintf(config->mesh_cmd_prefix, sizeof(config->mesh_cmd_prefix), "%s", "AT+MESH");
    snprintf(config->status_cmd, sizeof(config->status_cmd), "%s", "AT\r\n");
    snprintf(config->init_cmds[0], APP_PRIVATE_PROTOCOL_INIT_CMD_MAX_LEN, "%s", "AT+MADDR{m_addr}\r\n");
    snprintf(config->init_cmds[1], APP_PRIVATE_PROTOCOL_INIT_CMD_MAX_LEN, "%s", "AT+NETID{net_id}\r\n");
    snprintf(config->init_cmds[2], APP_PRIVATE_PROTOCOL_INIT_CMD_MAX_LEN, "%s", "AT+BAUD{baud_code}\r\n");
    snprintf(config->init_cmds[3], APP_PRIVATE_PROTOCOL_INIT_CMD_MAX_LEN, "%s", "AT+RESET\r\n");
    config->init_cmds_count = 4;
}

int app_private_protocol_parse_hex_bytes(const char *hex, unsigned char *out, int max_len, int *out_len)
{
    if (!hex || !out || !out_len || max_len <= 0) {
        return -1;
    }

    /* 先去掉空白并统一成大写，再按两位一组解析。 */
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

void app_private_protocol_unescape_cmd(char *s)
{
    if (!s) {
        return;
    }

    /* 配置里写的是可读字符串，落地时要恢复成实际控制字符。 */
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

int app_private_protocol_parse_init_cmds(const char *cmds_str,
                                         char out[][APP_PRIVATE_PROTOCOL_INIT_CMD_MAX_LEN],
                                         int max_count)
{
    if (!cmds_str || !out || max_count <= 0) {
        return 0;
    }

    /* 初始化命令允许用分号串联，保持配置简洁。 */
    char buf[CONFIG_MAX_VALUE_LEN];
    snprintf(buf, sizeof(buf), "%s", cmds_str);

    int count = 0;
    char *saveptr = NULL;
    char *token = strtok_r(buf, ";", &saveptr);
    while (token && count < max_count) {
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
        snprintf(out[count], APP_PRIVATE_PROTOCOL_INIT_CMD_MAX_LEN, "%s", token);
        app_private_protocol_unescape_cmd(out[count]);
        count++;
        token = strtok_r(NULL, ";", &saveptr);
    }

    return count;
}

int app_private_protocol_expand_template(const char *tmpl, char *out, size_t out_len,
                                         const AppProtocolPlaceholder *placeholders, int placeholder_count)
{
    if (!tmpl || !out || out_len == 0) {
        return -1;
    }

    out[0] = '\0';
    size_t j = 0;
    for (size_t i = 0; tmpl[i] != '\0' && j < out_len - 1; ) {
        if (tmpl[i] == '{') {
            const char *end = strchr(tmpl + i, '}');
            if (end) {
                size_t key_len = (size_t)(end - (tmpl + i + 1));
                char key[64];
                if (key_len >= sizeof(key)) {
                    return -1;
                }
                memcpy(key, tmpl + i + 1, key_len);
                key[key_len] = '\0';

                int idx = private_protocol_placeholder_index(placeholders, placeholder_count, key);
                if (idx >= 0 && placeholders[idx].value) {
                    size_t value_len = strlen(placeholders[idx].value);
                    if (j + value_len >= out_len) {
                        return -1;
                    }
                    memcpy(out + j, placeholders[idx].value, value_len);
                    j += value_len;
                    i = (size_t)(end - tmpl) + 1;
                    continue;
                }
            }
        }

        out[j++] = tmpl[i++];
    }
    out[j] = '\0';
    return (int)j;
}

int app_private_protocol_match_bytes(const unsigned char *lhs, int lhs_len,
                                     const unsigned char *rhs, int rhs_len)
{
    if (!lhs || !rhs || lhs_len <= 0 || rhs_len <= 0 || lhs_len != rhs_len) {
        return 0;
    }
    return memcmp(lhs, rhs, (size_t)lhs_len) == 0;
}

int app_private_protocol_is_ack(const PrivateProtocolConfig *config, const unsigned char *bytes, int len)
{
    if (!config || !bytes || len <= 0) {
        return 0;
    }
    return config->ack_frame_len > 0 &&
           len >= config->ack_frame_len &&
           memcmp(bytes, config->ack_frame, (size_t)config->ack_frame_len) == 0;
}

int app_private_protocol_is_nack(const PrivateProtocolConfig *config, const unsigned char *bytes, int len)
{
    if (!config || !bytes || len <= 0 || config->nack_frame_len <= 0) {
        return 0;
    }
    return len >= config->nack_frame_len &&
           memcmp(bytes, config->nack_frame, (size_t)config->nack_frame_len) == 0;
}

int app_private_protocol_payload_len(const PrivateProtocolConfig *config, int data_len)
{
    if (!config || data_len < 0 || config->id_len <= 0) {
        return -1;
    }
    if (data_len > 255 - config->id_len) {
        return -1;
    }
    return config->id_len + data_len;
}

int app_private_protocol_validate_frame(const PrivateProtocolConfig *config,
                                        const unsigned char *frame, int frame_len,
                                        int *payload_len)
{
    if (!config || !frame || frame_len <= 0 || config->frame_header_len <= 0 || config->id_len <= 0) {
        return -1;
    }
    if (frame_len < config->frame_header_len + 1) {
        return 0;
    }
    if (memcmp(frame, config->frame_header, (size_t)config->frame_header_len) != 0) {
        return -1;
    }

    int packet_len = frame[config->frame_header_len];
    if (packet_len < config->id_len) {
        return -1;
    }

    int total_len = config->frame_header_len + 1 + packet_len + config->frame_tail_len;
    if (frame_len < total_len) {
        return 0;
    }
    if (config->frame_tail_len > 0 &&
        memcmp(frame + config->frame_header_len + 1 + packet_len,
               config->frame_tail, (size_t)config->frame_tail_len) != 0) {
        return -1;
    }

    if (payload_len) {
        *payload_len = packet_len;
    }
    return total_len;
}

int app_private_protocol_build_frame(const PrivateProtocolConfig *config,
                                     const unsigned char *message, int message_len,
                                     unsigned char *frame, int frame_len)
{
    if (!config || !message || !frame || message_len < 3 || frame_len <= 0) {
        return -1;
    }
    if (memcmp(message, &config->connection_type, 1) != 0) {
        return -1;
    }
    if (message[1] != (unsigned char)config->id_len) {
        return -1;
    }

    int data_len = message[2];
    int payload_len = app_private_protocol_payload_len(config, data_len);
    if (payload_len < 0 || message_len < 3 + payload_len) {
        return -1;
    }

    int total_len = config->frame_header_len + 1 + payload_len + config->frame_tail_len;
    if (frame_len < total_len) {
        return -1;
    }

    memcpy(frame, config->frame_header, (size_t)config->frame_header_len);
    frame[config->frame_header_len] = (unsigned char)payload_len;
    memcpy(frame + config->frame_header_len + 1, message + 3, (size_t)payload_len);
    if (config->frame_tail_len > 0) {
        memcpy(frame + config->frame_header_len + 1 + payload_len,
               config->frame_tail, (size_t)config->frame_tail_len);
    }
    return total_len;
}

int app_private_protocol_build_command(const PrivateProtocolConfig *config,
                                       const unsigned char *message, int message_len,
                                       unsigned char *command, int command_len)
{
    if (!config || !message || !command || message_len < 3 || command_len <= 0) {
        return -1;
    }
    if (memcmp(message, &config->connection_type, 1) != 0) {
        return -1;
    }
    if (message[1] != (unsigned char)config->id_len) {
        return -1;
    }

    int data_len = message[2];
    int payload_len = app_private_protocol_payload_len(config, data_len);
    if (payload_len < 0 || message_len < 3 + payload_len) {
        return -1;
    }

    size_t prefix_len = strlen(config->mesh_cmd_prefix);
    int total_len = (int)prefix_len + payload_len + 2;
    if (command_len < total_len) {
        return -1;
    }

    memcpy(command, config->mesh_cmd_prefix, prefix_len);
    memcpy(command + prefix_len, message + 3, (size_t)payload_len);
    command[prefix_len + payload_len] = '\r';
    command[prefix_len + payload_len + 1] = '\n';
    return total_len;
}

int app_private_protocol_unpack_frame(const PrivateProtocolConfig *config,
                                      const unsigned char *frame, int frame_len,
                                      ConnectionType connection_type,
                                      unsigned char *message, int message_len)
{
    if (!config || !frame || !message || frame_len <= 0 || message_len <= 0) {
        return -1;
    }

    int payload_len = 0;
    int total_len = app_private_protocol_validate_frame(config, frame, frame_len, &payload_len);
    if (total_len <= 0) {
        return total_len;
    }

    int data_len = payload_len - config->id_len;
    int required_len = 3 + payload_len;
    if (message_len < required_len) {
        return -1;
    }

    message[0] = (unsigned char)connection_type;
    message[1] = (unsigned char)config->id_len;
    message[2] = (unsigned char)data_len;
    memcpy(message + 3, frame + config->frame_header_len + 1, (size_t)payload_len);
    return required_len;
}
