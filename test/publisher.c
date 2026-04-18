/* dds_publisher_json.c - 发布JSON格式消息到 GatewayCommand 话题 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dds/dds.h>
#include "GatewayData.h"

#define TOPIC_NAME    "GatewayCommand"

/* 将二进制数据转换为十六进制字符串 */
static char *bin_to_hex(const unsigned char *binary, int len)
{
    if (!binary || len <= 0) return NULL;

    char *hex_str = malloc(len * 2 + 1);
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

    int len = snprintf(json_buf, buf_size,
        "{\"connection_type\":%d,\"id\":\"%s\",\"data\":\"%s\"}",
        connection_type, id_hex, data_hex);

    free(id_hex);
    free(data_hex);

    return (len > 0 && len < buf_size) ? len : -1;
}

int main(int argc, char *argv[])
{
    dds_entity_t participant;
    dds_entity_t topic;
    dds_entity_t writer;

    printf("DDS 发布者启动 (JSON格式)\n");
    printf("发布话题: %s\n", TOPIC_NAME);

    /* 创建参与者 */
    participant = dds_create_participant(DDS_DOMAIN_DEFAULT, NULL, NULL);
    if (participant < 0) {
        fprintf(stderr, "创建参与者失败: %s\n", dds_strretcode(participant));
        return -1;
    }

    /* 创建话题 - 使用 GatewayData.h 中的 descriptor */
    topic = dds_create_topic(participant, &Gateway_CommandType_desc, TOPIC_NAME, NULL, NULL);
    if (topic < 0) {
        fprintf(stderr, "创建话题失败: %s\n", dds_strretcode(topic));
        dds_delete(participant);
        return -1;
    }

    /* 创建写入者 */
    dds_qos_t *qos = dds_create_qos();
    dds_qset_reliability(qos, DDS_RELIABILITY_RELIABLE, DDS_SECS(10));
    writer = dds_create_writer(participant, topic, qos, NULL);
    dds_delete_qos(qos);

    if (writer < 0) {
        fprintf(stderr, "创建写入者失败: %s\n", dds_strretcode(writer));
        dds_delete(participant);
        return -1;
    }

    /* 等待订阅者 */
    printf("等待订阅者...\n");
    dds_sleepfor(DDS_SECS(1));

    /* 构造JSON消息 */
    char json_buf[4096];
    unsigned char id[] = {0x00, 0x01};
    unsigned char data[] = "HELLO_TEST";

    int json_len = build_json_message(json_buf, sizeof(json_buf),
                                       2,  /* connection_type = BLE_MESH */
                                       id, 2,
                                       data, 10);

    if (json_len < 0) {
        fprintf(stderr, "构造JSON消息失败\n");
        dds_delete(participant);
        return -1;
    }

    /* 发送消息 */
    Gateway_CommandType msg;
    memset(&msg, 0, sizeof(msg));
    memcpy(msg.data, json_buf, json_len);
    msg.length = json_len;

    printf("\n发送JSON消息:\n");
    printf("  connection_type: 2 (BLE_MESH)\n");
    printf("  id: 0001\n");
    printf("  data: 48454C4C4F5F54455354 (HELLO_TEST)\n");
    printf("  JSON: %s\n", json_buf);
    printf("  总长度: %u 字节\n", msg.length);

    int ret = dds_write(writer, &msg);
    if (ret != DDS_RETCODE_OK) {
        fprintf(stderr, "发送失败: %s\n", dds_strretcode(ret));
    } else {
        printf("消息发送成功!\n");
    }

    /* 等待消息被接收 */
    dds_sleepfor(DDS_SECS(2));

    /* 清理 */
    dds_delete(participant);
    return 0;
}
