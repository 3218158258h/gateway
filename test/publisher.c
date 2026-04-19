/* dds_publisher_json.c - 发布JSON格式消息到 GatewayCommand 话题 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <dds/dds.h>
#include "GatewayData.h"

#define TOPIC_NAME "GatewayCommand"

static volatile sig_atomic_t g_stop = 0;

static void handle_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

/* 将二进制数据转换为十六进制字符串 */
static char *bin_to_hex(const unsigned char *binary, int len)
{
    if (!binary || len <= 0) return NULL;

    char *hex_str = malloc((size_t)len * 2 + 1);
    if (!hex_str) return NULL;

    for (int i = 0; i < len; i++) {
        sprintf(hex_str + i * 2, "%02X", binary[i]);
    }
    hex_str[len * 2] = '\0';
    return hex_str;
}

/* 构造JSON消息 */
static int build_json_message(char *json_buf, int buf_size,
                               int connection_type,
                               const unsigned char *id, int id_len,
                               const unsigned char *data, int data_len)
{
    char *id_hex = bin_to_hex(id, id_len);
    char *data_hex = bin_to_hex(data, data_len);

    if (!id_hex || !data_hex) {
        free(id_hex);
        free(data_hex);
        return -1;
    }

    int len = snprintf(json_buf, (size_t)buf_size,
        "{\"connection_type\":%d,\"id\":\"%s\",\"data\":\"%s\"}",
        connection_type, id_hex, data_hex);

    free(id_hex);
    free(data_hex);

    return (len > 0 && len < buf_size) ? len : -1;
}

static int parse_device_id_hex(const char *hex, unsigned char out[2])
{
    unsigned int device_id_value = 0;
    if (!hex || strlen(hex) != 4) {
        return -1;
    }
    if (sscanf(hex, "%4x", &device_id_value) != 1) {
        return -1;
    }
    out[0] = (unsigned char)((device_id_value >> 8) & 0xFF);
    out[1] = (unsigned char)(device_id_value & 0xFF);
    return 0;
}

static int parse_u32_arg(const char *s, uint32_t *out)
{
    char *end = NULL;
    unsigned long parsed_value;
    if (!s || !out) return -1;
    errno = 0;
    parsed_value = strtoul(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || parsed_value > UINT32_MAX) {
        return -1;
    }
    *out = (uint32_t)parsed_value;
    return 0;
}

static int parse_int_arg(const char *s, int *out)
{
    char *end = NULL;
    long parsed_value;
    if (!s || !out) return -1;
    errno = 0;
    parsed_value = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' ||
        parsed_value < (long)INT_MIN || parsed_value > (long)INT_MAX) {
        return -1;
    }
    *out = (int)parsed_value;
    return 0;
}

static int parse_connection_type_token(const char *token, int *out)
{
    char lower[32];
    size_t len;
    if (!token || !out) return -1;

    len = strlen(token);
    if (len == 0 || len >= sizeof(lower)) return -1;

    for (size_t i = 0; i < len; i++) {
        char c = token[i];
        if (c >= 'A' && c <= 'Z') {
            lower[i] = (char)(c - 'A' + 'a');
        } else {
            lower[i] = c;
        }
    }
    lower[len] = '\0';

    if (strcmp(lower, "none") == 0 || strcmp(lower, "0") == 0) {
        *out = 0;
        return 0;
    }
    if (strcmp(lower, "lora") == 0 || strcmp(lower, "1") == 0) {
        *out = 1;
        return 0;
    }
    if (strcmp(lower, "ble") == 0 || strcmp(lower, "ble_mesh") == 0 ||
        strcmp(lower, "ble-mesh") == 0 || strcmp(lower, "2") == 0) {
        *out = 2;
        return 0;
    }

    return parse_int_arg(token, out);
}

static int parse_type_sequence(const char *sequence, int *types, int max_types)
{
    char *tmp;
    char *token;
    char *saveptr = NULL;
    int count = 0;

    if (!sequence || !types || max_types <= 0) return -1;

    tmp = strdup(sequence);
    if (!tmp) return -1;

    token = strtok_r(tmp, ",", &saveptr);
    while (token && count < max_types) {
        while (*token == ' ' || *token == '\t') token++;
        if (*token != '\0') {
            if (parse_connection_type_token(token, &types[count]) != 0) {
                free(tmp);
                return -1;
            }
            count++;
        }
        token = strtok_r(NULL, ",", &saveptr);
    }

    free(tmp);
    return (count > 0) ? count : -1;
}

static void print_usage(const char *prog)
{
    printf("Usage: %s [--count N] [--interval-ms N] [--start N] [--device-id HEX4]\n", prog);
    printf("          [--connection-type N] [--device-type NAME] [--type-seq LIST]\n");
    printf("  --count N           send N messages (default: 0, means infinite)\n");
    printf("  --interval-ms N     interval in milliseconds (default: 1000)\n");
    printf("  --start N           start value for incrementing counter (default: 1)\n");
    printf("  --device-id HEX4    2-byte device id in hex, e.g. 0001 (default: 0001)\n");
    printf("  --connection-type N connection type value (default: 2)\n");
    printf("  --device-type NAME  connection type alias: ble_mesh|lora|none\n");
    printf("  --type-seq LIST     cyclic type sequence, e.g. ble_mesh,lora\n");
}

int main(int argc, char *argv[])
{
    dds_entity_t participant;
    dds_entity_t topic;
    dds_entity_t writer;

    uint32_t count = 0;             /* 0 = infinite */
    uint32_t interval_ms = 1000;
    uint32_t seq = 1;
    int connection_type = 2;
    const char *device_id_arg = "0001";
    int type_seq[32];
    size_t type_seq_len = 0;
    const char *type_seq_arg = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) {
            if (parse_u32_arg(argv[++i], &count) != 0) {
                fprintf(stderr, "Invalid --count: %s\n", argv[i]);
                return -1;
            }
        } else if (strcmp(argv[i], "--interval-ms") == 0 && i + 1 < argc) {
            if (parse_u32_arg(argv[++i], &interval_ms) != 0) {
                fprintf(stderr, "Invalid --interval-ms: %s\n", argv[i]);
                return -1;
            }
        } else if (strcmp(argv[i], "--start") == 0 && i + 1 < argc) {
            if (parse_u32_arg(argv[++i], &seq) != 0) {
                fprintf(stderr, "Invalid --start: %s\n", argv[i]);
                return -1;
            }
        } else if (strcmp(argv[i], "--device-id") == 0 && i + 1 < argc) {
            device_id_arg = argv[++i];
        } else if (strcmp(argv[i], "--connection-type") == 0 && i + 1 < argc) {
            if (parse_int_arg(argv[++i], &connection_type) != 0) {
                fprintf(stderr, "Invalid --connection-type: %s\n", argv[i]);
                return -1;
            }
        } else if ((strcmp(argv[i], "--device-type") == 0 || strcmp(argv[i], "--type") == 0) && i + 1 < argc) {
            if (parse_connection_type_token(argv[++i], &connection_type) != 0) {
                fprintf(stderr, "Invalid --device-type: %s\n", argv[i]);
                return -1;
            }
        } else if (strcmp(argv[i], "--type-seq") == 0 && i + 1 < argc) {
            type_seq_arg = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown or invalid argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return -1;
        }
    }

    unsigned char id[2];
    if (parse_device_id_hex(device_id_arg, id) != 0) {
        fprintf(stderr, "Invalid --device-id: %s (expected 4 hex chars)\n", device_id_arg);
        return -1;
    }
    if (type_seq_arg) {
        int parsed_type_seq_len = parse_type_sequence(type_seq_arg, type_seq, (int)(sizeof(type_seq) / sizeof(type_seq[0])));
        if (parsed_type_seq_len < 0) {
            fprintf(stderr, "Invalid --type-seq: %s\n", type_seq_arg);
            return -1;
        }
        type_seq_len = (size_t)parsed_type_seq_len;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    printf("DDS 发布者启动 (JSON格式)\n");
    printf("发布话题: %s\n", TOPIC_NAME);
    if (type_seq_len > 0) {
        printf("device_id=%s type_seq=%s count=%u interval_ms=%u start=%u\n",
               device_id_arg, type_seq_arg, count, interval_ms, seq);
    } else {
        printf("device_id=%s connection_type=%d count=%u interval_ms=%u start=%u\n",
               device_id_arg, connection_type, count, interval_ms, seq);
    }

    participant = dds_create_participant(DDS_DOMAIN_DEFAULT, NULL, NULL);
    if (participant < 0) {
        fprintf(stderr, "创建参与者失败: %s\n", dds_strretcode(participant));
        return -1;
    }

    topic = dds_create_topic(participant, &Gateway_CommandType_desc, TOPIC_NAME, NULL, NULL);
    if (topic < 0) {
        fprintf(stderr, "创建话题失败: %s\n", dds_strretcode(topic));
        dds_delete(participant);
        return -1;
    }

    dds_qos_t *qos = dds_create_qos();
    dds_qset_reliability(qos, DDS_RELIABILITY_RELIABLE, DDS_SECS(10));
    writer = dds_create_writer(participant, topic, qos, NULL);
    dds_delete_qos(qos);

    if (writer < 0) {
        fprintf(stderr, "创建写入者失败: %s\n", dds_strretcode(writer));
        dds_delete(participant);
        return -1;
    }

    printf("等待订阅者...\n");
    dds_sleepfor(DDS_SECS(1));

    uint32_t sent = 0;
    while (!g_stop && (count == 0 || sent < count)) {
        char json_buf[4096];
        unsigned char payload[4];
        int current_type = (type_seq_len > 0) ? type_seq[sent % type_seq_len] : connection_type;
        payload[0] = (unsigned char)((seq >> 24) & 0xFF);
        payload[1] = (unsigned char)((seq >> 16) & 0xFF);
        payload[2] = (unsigned char)((seq >> 8) & 0xFF);
        payload[3] = (unsigned char)(seq & 0xFF);

        int json_len = build_json_message(json_buf, (int)sizeof(json_buf),
                                          current_type,
                                          id, 2,
                                          payload, 4);

        if (json_len < 0) {
            fprintf(stderr, "构造JSON消息失败\n");
            break;
        }

        Gateway_CommandType msg;
        memset(&msg, 0, sizeof(msg));
        if ((size_t)json_len > sizeof(msg.data)) {
            fprintf(stderr, "JSON too large: %d > %zu\n", json_len, sizeof(msg.data));
            break;
        }
        memcpy(msg.data, json_buf, (size_t)json_len);
        msg.length = (uint32_t)json_len;

        int ret = dds_write(writer, &msg);
        if (ret != DDS_RETCODE_OK) {
            fprintf(stderr, "发送失败: %s\n", dds_strretcode(ret));
            break;
        }

        printf("[TX %u] seq=%u type=%d payload=%02X%02X%02X%02X json=%s\n",
               sent + 1, seq,
               current_type,
               payload[0], payload[1], payload[2], payload[3],
               json_buf);
        fflush(stdout);

        sent++;
        seq++;

        if (interval_ms > 0) {
            struct timespec req;
            req.tv_sec = (time_t)(interval_ms / 1000u);
            req.tv_nsec = (long)(interval_ms % 1000u) * 1000000L;
            nanosleep(&req, NULL);
        }
    }

    printf("publisher exit: sent=%u\n", sent);
    dds_delete(participant);
    return 0;
}
