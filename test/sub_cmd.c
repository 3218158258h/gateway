/* dds_subscriber_json.c - 订阅 GatewayData 话题 (JSON格式) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dds/dds.h>
#include "GatewayData.h"

#define TOPIC_NAME    "GatewayCommand"

/* 简单的JSON字段提取 (不依赖cJSON库) */
static int extract_json_field(const char *json, const char *field,
                               char *value, int value_size)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", field);

    const char *pos = strstr(json, pattern);
    if (!pos) return -1;

    pos += strlen(pattern);
    while (*pos == ' ') pos++;

    if (*pos == '"') {
        /* 字符串类型 */
        pos++;
        int i = 0;
        while (*pos != '"' && *pos != '\0' && i < value_size - 1) {
            value[i++] = *pos++;
        }
        value[i] = '\0';
        return 0;
    } else if (*pos >= '0' && *pos <= '9') {
        /* 数字类型 */
        int i = 0;
        while ((*pos >= '0' && *pos <= '9') && i < value_size - 1) {
            value[i++] = *pos++;
        }
        value[i] = '\0';
        return 0;
    }

    return -1;
}

/* 十六进制字符串转二进制 */
static int hex_to_bytes(const char *hex, unsigned char *bytes, int max_len)
{
    int len = strlen(hex);
    if (len % 2 != 0) return -1;

    int byte_len = len / 2;
    if (byte_len > max_len) byte_len = max_len;

    for (int i = 0; i < byte_len; i++) {
        unsigned int val;
        sscanf(hex + i * 2, "%2x", &val);
        bytes[i] = (unsigned char)val;
    }

    return byte_len;
}

/* 数据接收回调 */
static void data_available(dds_entity_t reader)
{
    Gateway_DataType msg;
    void *samples[1] = { &msg };
    dds_sample_info_t infos[1];

    int ret = dds_take(reader, samples, infos, 1, 1);
    if (ret > 0 && infos[0].valid_data) {
        /* 确保字符串结尾 */
        msg.data[msg.length] = '\0';

        printf("\n========== 收到网关数据 ==========\n");
        printf("数据长度: %u 字节\n", msg.length);
        printf("JSON内容: %s\n", (char *)msg.data);

        /* 解析JSON字段 */
        char conn_type_str[16] = {0};
        char id_hex[256] = {0};
        char data_hex[1024] = {0};

        if (extract_json_field((char *)msg.data, "connection_type",
                               conn_type_str, sizeof(conn_type_str)) == 0) {
            printf("连接类型: %s\n", conn_type_str);
        }

        if (extract_json_field((char *)msg.data, "id",
                               id_hex, sizeof(id_hex)) == 0) {
            printf("ID (hex): %s\n", id_hex);

            /* 转换为二进制显示 */
            unsigned char id_bytes[128];
            int id_len = hex_to_bytes(id_hex, id_bytes, sizeof(id_bytes));
            if (id_len > 0) {
                printf("ID (bin): ");
                for (int i = 0; i < id_len; i++) {
                    printf("%02X ", id_bytes[i]);
                }
                printf("\n");
            }
        }

        if (extract_json_field((char *)msg.data, "data",
                               data_hex, sizeof(data_hex)) == 0) {
            printf("数据 (hex): %s\n", data_hex);

            /* 转换为二进制并尝试显示为文本 */
            unsigned char data_bytes[512];
            int data_len = hex_to_bytes(data_hex, data_bytes, sizeof(data_bytes));
            if (data_len > 0) {
                printf("数据 (bin): ");
                for (int i = 0; i < data_len && i < 32; i++) {
                    printf("%02X ", data_bytes[i]);
                }
                printf("\n");

                /* 如果是可打印字符，显示文本 */
                int printable = 1;
                for (int i = 0; i < data_len; i++) {
                    if (data_bytes[i] != 0 && !isprint(data_bytes[i])) {
                        printable = 0;
                        break;
                    }
                }
                if (printable && data_len > 0) {
                    data_bytes[data_len] = '\0';
                    printf("数据 (txt): %s\n", data_bytes);
                }
            }
        }

        printf("==================================\n");
        fflush(stdout);
    }
}

int main(int argc, char *argv[])
{
    dds_entity_t participant;
    dds_entity_t topic;
    dds_entity_t reader;
    dds_entity_t waitset;
    dds_entity_t readcond;

    printf("DDS 订阅者启动 (JSON格式)\n");
    printf("订阅话题: %s\n", TOPIC_NAME);

    /* 创建参与者 */
    participant = dds_create_participant(DDS_DOMAIN_DEFAULT, NULL, NULL);
    if (participant < 0) {
        fprintf(stderr, "创建参与者失败: %s\n", dds_strretcode(participant));
        return -1;
    }

    /* 创建话题 - 使用 GatewayData.h 中的 descriptor */
    topic = dds_create_topic(participant, &Gateway_DataType_desc, TOPIC_NAME, NULL, NULL);
    if (topic < 0) {
        fprintf(stderr, "创建话题失败: %s\n", dds_strretcode(topic));
        dds_delete(participant);
        return -1;
    }

    /* 创建读取者 */
    dds_qos_t *qos = dds_create_qos();
    dds_qset_reliability(qos, DDS_RELIABILITY_RELIABLE, DDS_SECS(10));
    reader = dds_create_reader(participant, topic, qos, NULL);
    dds_delete_qos(qos);

    if (reader < 0) {
        fprintf(stderr, "创建读取者失败: %s\n", dds_strretcode(reader));
        dds_delete(participant);
        return -1;
    }

    /* 创建等待集 */
    waitset = dds_create_waitset(participant);
    readcond = dds_create_readcondition(reader, DDS_ANY_STATE);
    dds_waitset_attach(waitset, readcond, reader);

    printf("等待网关数据...\n\n");

    /* 主循环 */
    while (1) {
        dds_attach_t triggered;
        int ret = dds_waitset_wait(waitset, &triggered, 1, DDS_SECS(10));
        if (ret > 0) {
            data_available(reader);
        }
    }

    /* 清理 */
    dds_delete(participant);
    return 0;
}
