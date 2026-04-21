#include "../include/app_protocol_config.h"
#include "../include/app_config.h"
#include "../thirdparty/log.c/log.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define DEFAULT_PROTOCOL_NAME "ble_mesh_default"

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

static int app_protocol_parse_hex_bytes(const char *hex, unsigned char *out, int max_len, int *out_len)
{
    if (!hex || !out || !out_len || max_len <= 0) {
        return -1;
    }

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

static void app_protocol_load_defaults(BluetoothProtocolConfig *config)
{
    if (!config) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->connection_type = CONNECTION_TYPE_BLE_MESH;
    config->frame_header[0] = 0xF1;
    config->frame_header[1] = 0xDD;
    config->frame_header_len = 2;
    config->frame_tail_len = 0;
    config->id_len = 2;
    snprintf(config->mesh_cmd_prefix, sizeof(config->mesh_cmd_prefix), "%s", "AT+MESH");
}

int app_protocol_load_bluetooth(const char *protocol_name, BluetoothProtocolConfig *config)
{
    if (!config) {
        return -1;
    }

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

    if (config_get_string(&cfg_mgr, section, "connection_type", "", value, sizeof(value)) == 0 &&
        value[0] != '\0') {
        ConnectionType connection_type;
        if (app_protocol_parse_connection_type(value, &connection_type) == 0) {
            config->connection_type = connection_type;
        } else {
            log_warn("Invalid %s.connection_type=%s, fallback default", section, value);
        }
    }

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

    config->id_len = config_get_int(&cfg_mgr, section, "id_len", config->id_len);
    if (config->id_len <= 0 || config->id_len > 8) {
        log_warn("Invalid %s.id_len=%d, fallback default", section, config->id_len);
        config->id_len = 2;
    }

    if (config_get_string(&cfg_mgr, section, "mesh_cmd_prefix", "",
                          value, sizeof(value)) == 0 &&
        value[0] != '\0') {
        snprintf(config->mesh_cmd_prefix, sizeof(config->mesh_cmd_prefix), "%s", value);
    }

    config_destroy(&cfg_mgr);
    return 0;
}
