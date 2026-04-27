#ifndef __APP_PRIVATE_PROTOCOL_H__
#define __APP_PRIVATE_PROTOCOL_H__

#include "app_message.h"
#include <stddef.h>

/* 私有协议层的通用容量限制，避免协议配置无限增长。 */
#define APP_PRIVATE_PROTOCOL_MAX_FRAME_BYTES 8
#define APP_PRIVATE_PROTOCOL_CMD_PREFIX_MAX_LEN 16
#define APP_PRIVATE_PROTOCOL_INIT_CMD_MAX_LEN 64
#define APP_PRIVATE_PROTOCOL_MAX_INIT_CMDS 8
#define APP_PRIVATE_PROTOCOL_MAX_PLACEHOLDERS 8

/* 为现有加载器保留的兼容别名。 */
#define APP_PROTOCOL_MAX_FRAME_BYTES APP_PRIVATE_PROTOCOL_MAX_FRAME_BYTES
#define APP_PROTOCOL_CMD_PREFIX_MAX_LEN APP_PRIVATE_PROTOCOL_CMD_PREFIX_MAX_LEN
#define APP_PROTOCOL_INIT_CMD_MAX_LEN APP_PRIVATE_PROTOCOL_INIT_CMD_MAX_LEN
#define APP_PROTOCOL_MAX_INIT_CMDS APP_PRIVATE_PROTOCOL_MAX_INIT_CMDS

typedef struct AppProtocolPlaceholderStruct {
    const char *name;
    const char *value;
} AppProtocolPlaceholder;

typedef struct PrivateProtocolConfigStruct {
    /* 连接类型决定这份协议配置应用到哪一类设备。 */
    ConnectionType connection_type;
    /* 帧头/帧尾/ACK/NACK 全部使用十六进制字节匹配。 */
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

/* 使用默认值填充协议配置，保证缺省场景也能正常工作。 */
void app_private_protocol_load_defaults(PrivateProtocolConfig *config);
/* 将十六进制字符串解析成字节数组。 */
int app_private_protocol_parse_hex_bytes(const char *hex, unsigned char *out, int max_len, int *out_len);
/* 把配置里的转义命令还原成实际控制字符。 */
void app_private_protocol_unescape_cmd(char *s);
/* 将分号分隔的初始化命令拆成数组。 */
int app_private_protocol_parse_init_cmds(const char *cmds_str,
                                         char out[][APP_PRIVATE_PROTOCOL_INIT_CMD_MAX_LEN],
                                         int max_count);
/* 按占位符表替换模板字符串。 */
int app_private_protocol_expand_template(const char *tmpl, char *out, size_t out_len,
                                          const AppProtocolPlaceholder *placeholders, int placeholder_count);
/* 按字节级规则比较两段数据。 */
int app_private_protocol_match_bytes(const unsigned char *lhs, int lhs_len,
                                      const unsigned char *rhs, int rhs_len);
/* 判断数据是否匹配 ACK/NACK 帧。 */
int app_private_protocol_is_ack(const PrivateProtocolConfig *config, const unsigned char *bytes, int len);
int app_private_protocol_is_nack(const PrivateProtocolConfig *config, const unsigned char *bytes, int len);
/* 根据业务数据长度计算协议有效载荷长度。 */
int app_private_protocol_payload_len(const PrivateProtocolConfig *config, int data_len);
/* 校验收到的帧是否完整有效。 */
int app_private_protocol_validate_frame(const PrivateProtocolConfig *config,
                                         const unsigned char *frame, int frame_len,
                                         int *payload_len);
/* 组装完整协议帧。 */
int app_private_protocol_build_frame(const PrivateProtocolConfig *config,
                                      const unsigned char *message, int message_len,
                                      unsigned char *frame, int frame_len);
/* 组装协议命令串。 */
int app_private_protocol_build_command(const PrivateProtocolConfig *config,
                                        const unsigned char *message, int message_len,
                                        unsigned char *command, int command_len);
/* 从原始帧中拆出业务数据。 */
int app_private_protocol_unpack_frame(const PrivateProtocolConfig *config,
                                      const unsigned char *frame, int frame_len,
                                      ConnectionType connection_type,
                                      unsigned char *message, int message_len);

#endif
