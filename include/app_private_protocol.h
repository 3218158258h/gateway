#ifndef __APP_PRIVATE_PROTOCOL_H__
#define __APP_PRIVATE_PROTOCOL_H__

#include "app_message.h"
#include <stddef.h>

#define APP_PRIVATE_PROTOCOL_MAX_FRAME_BYTES 8
#define APP_PRIVATE_PROTOCOL_CMD_PREFIX_MAX_LEN 16
#define APP_PRIVATE_PROTOCOL_INIT_CMD_MAX_LEN 64
#define APP_PRIVATE_PROTOCOL_MAX_INIT_CMDS 8
#define APP_PRIVATE_PROTOCOL_MAX_PLACEHOLDERS 8

/* Backward-compatible aliases used by the existing protocol loader. */
#define APP_PROTOCOL_MAX_FRAME_BYTES APP_PRIVATE_PROTOCOL_MAX_FRAME_BYTES
#define APP_PROTOCOL_CMD_PREFIX_MAX_LEN APP_PRIVATE_PROTOCOL_CMD_PREFIX_MAX_LEN
#define APP_PROTOCOL_INIT_CMD_MAX_LEN APP_PRIVATE_PROTOCOL_INIT_CMD_MAX_LEN
#define APP_PROTOCOL_MAX_INIT_CMDS APP_PRIVATE_PROTOCOL_MAX_INIT_CMDS

typedef struct AppProtocolPlaceholderStruct {
    const char *name;
    const char *value;
} AppProtocolPlaceholder;

typedef struct PrivateProtocolConfigStruct {
    ConnectionType connection_type;
    unsigned char frame_header[APP_PRIVATE_PROTOCOL_MAX_FRAME_BYTES];
    int frame_header_len;
    unsigned char frame_tail[APP_PRIVATE_PROTOCOL_MAX_FRAME_BYTES];
    int frame_tail_len;
    unsigned char ack_frame[APP_PRIVATE_PROTOCOL_MAX_FRAME_BYTES];
    int ack_frame_len;
    unsigned char nack_frame[APP_PRIVATE_PROTOCOL_MAX_FRAME_BYTES];
    int nack_frame_len;
    int id_len;
    char mesh_cmd_prefix[APP_PRIVATE_PROTOCOL_CMD_PREFIX_MAX_LEN];
    char status_cmd[APP_PRIVATE_PROTOCOL_INIT_CMD_MAX_LEN];
    char init_cmds[APP_PRIVATE_PROTOCOL_MAX_INIT_CMDS][APP_PRIVATE_PROTOCOL_INIT_CMD_MAX_LEN];
    int init_cmds_count;
} PrivateProtocolConfig;

typedef PrivateProtocolConfig BluetoothProtocolConfig;

void app_private_protocol_load_defaults(PrivateProtocolConfig *config);
int app_private_protocol_parse_hex_bytes(const char *hex, unsigned char *out, int max_len, int *out_len);
void app_private_protocol_unescape_cmd(char *s);
int app_private_protocol_parse_init_cmds(const char *cmds_str,
                                         char out[][APP_PRIVATE_PROTOCOL_INIT_CMD_MAX_LEN],
                                         int max_count);
int app_private_protocol_expand_template(const char *tmpl, char *out, size_t out_len,
                                         const AppProtocolPlaceholder *placeholders, int placeholder_count);
int app_private_protocol_match_bytes(const unsigned char *lhs, int lhs_len,
                                     const unsigned char *rhs, int rhs_len);
int app_private_protocol_is_ack(const PrivateProtocolConfig *config, const unsigned char *bytes, int len);
int app_private_protocol_is_nack(const PrivateProtocolConfig *config, const unsigned char *bytes, int len);
int app_private_protocol_payload_len(const PrivateProtocolConfig *config, int data_len);
int app_private_protocol_validate_frame(const PrivateProtocolConfig *config,
                                        const unsigned char *frame, int frame_len,
                                        int *payload_len);
int app_private_protocol_build_frame(const PrivateProtocolConfig *config,
                                     const unsigned char *message, int message_len,
                                     unsigned char *frame, int frame_len);
int app_private_protocol_build_command(const PrivateProtocolConfig *config,
                                       const unsigned char *message, int message_len,
                                       unsigned char *command, int command_len);
int app_private_protocol_unpack_frame(const PrivateProtocolConfig *config,
                                     const unsigned char *frame, int frame_len,
                                     ConnectionType connection_type,
                                     unsigned char *message, int message_len);

#endif
