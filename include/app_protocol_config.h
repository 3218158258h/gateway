#ifndef __APP_PROTOCOL_CONFIG_H__
#define __APP_PROTOCOL_CONFIG_H__

#include "app_message.h"

#define APP_PROTOCOL_CONFIG_FILE "config/protocols.ini"
#define APP_PROTOCOL_NAME_MAX_LEN 64
#define APP_PROTOCOL_CMD_PREFIX_MAX_LEN 16
#define APP_PROTOCOL_MAX_FRAME_BYTES 8

typedef struct BluetoothProtocolConfigStruct {
    ConnectionType connection_type;
    unsigned char frame_header[APP_PROTOCOL_MAX_FRAME_BYTES];
    int frame_header_len;
    unsigned char frame_tail[APP_PROTOCOL_MAX_FRAME_BYTES];
    int frame_tail_len;
    int id_len;
    char mesh_cmd_prefix[APP_PROTOCOL_CMD_PREFIX_MAX_LEN];
} BluetoothProtocolConfig;

int app_protocol_load_bluetooth(const char *protocol_name, BluetoothProtocolConfig *config);

#endif
