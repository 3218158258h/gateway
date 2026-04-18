/**
 * @file app_message.c
 * @brief 消息格式转换模块 - 二进制与JSON互转
 * 
 * 功能说明：
 * - 二进制消息格式解析与构建
 * - JSON消息格式解析与构建
 * - 十六进制字符串与二进制数据互转
 * 
 * 消息格式（二进制）：
 * | 连接类型(1字节) | ID长度(1字节) | 数据长度(1字节) | ID | 数据 |
 * 
 * 消息格式（JSON）：
 * {"connection_type":1, "id":"AABBCC", "data":"112233"}
 */

#include "../include/app_message.h"
#include <string.h>
#include "../thirdparty/log.c/log.h"
#include "../thirdparty/cJSON/cJSON.h"
#include <stdlib.h>

/**
 * @brief 二进制数据转十六进制字符串
 * 
 * 将二进制数据转换为可读的十六进制字符串格式。
 * 例如：{0xAB, 0xCD} -> "ABCD"
 * 
 * @param binary 二进制数据指针
 * @param len 数据长度
 * @return 十六进制字符串指针（需调用方free），失败返回NULL
 */
static char *bin_to_str(unsigned char *binary, int len)
{
    if (!binary || len <= 0) {
        return NULL;
    }
    
    // 分配字符串内存（每字节对应2个十六进制字符）
    char *hex_str = malloc(len * 2 + 1);
    if (!hex_str)
    {
        log_warn("Not enough memory");
        return NULL;
    }

    // 逐字节转换为十六进制
    for (int i = 0; i < len; i++)
    {
        sprintf(hex_str + i * 2, "%02X", binary[i]);
    }
    hex_str[len * 2] = '\0';
    return hex_str;
}

/**
 * @brief 十六进制字符串转二进制数据
 * 
 * 将十六进制字符串解析为二进制数据。
 * 例如："AABBCC" -> {0xAA, 0xBB, 0xCC}
 * 
 * @param hex_str 十六进制字符串
 * @param binary 输出二进制缓冲区
 * @param len 缓冲区大小
 * @return 转换后的二进制长度，失败返回-1
 */
static int str_to_bin(const char *hex_str, unsigned char *binary, int len)
{
    if (!hex_str || !binary || len <= 0) {
        return -1;
    }
    
    size_t hex_len = strlen(hex_str);
    
    // 检查字符串长度是否为偶数
    if (hex_len % 2 != 0)
    {
        log_warn("Hex string is not valid");
        return -1;
    }
    
    // 检查缓冲区是否足够
    if (len < (int)(hex_len / 2))
    {
        log_warn("Buffer len is not enough");
        return -1;
    }
    
    len = hex_len / 2;
    
    // 逐字符解析十六进制
    for (int i = 0; i < len; i++)
    {
        char high = hex_str[i * 2];      // 高4位字符
        char low = hex_str[i * 2 + 1];   // 低4位字符
        
        binary[i] = 0;

        // 解析高4位
        if (high <= '9' && high >= '0')
        {
            binary[i] = high - '0';
        }
        else if (high >= 'a' && high <= 'f')
        {
            binary[i] = high - 'a' + 10;
        }
        else if (high >= 'A' && high <= 'F')
        {
            binary[i] = high - 'A' + 10;
        }
        else
        {
            log_warn("Invalid hex character: %c", high);
            return -1;
        }
        binary[i] <<= 4;  // 左移4位

        // 解析低4位
        if (low <= '9' && low >= '0')
        {
            binary[i] |= low - '0';
        }
        else if (low >= 'a' && low <= 'f')
        {
            binary[i] |= low - 'a' + 10;
        }
        else if (low >= 'A' && low <= 'F')
        {
            binary[i] |= low - 'A' + 10;
        }
        else
        {
            log_warn("Invalid hex character: %c", low);
            return -1;
        }
    }
    return len;
}

/**
 * @brief 从二进制数据初始化消息结构
 * 
 * 解析二进制格式的消息数据，填充消息结构体。
 * 二进制格式：[连接类型(1)][ID长度(1)][数据长度(1)][ID][数据]
 * 
 * @param message 消息结构体指针
 * @param binary 二进制数据指针
 * @param len 数据长度
 * @return 0成功，-1失败
 */
int app_message_initByBinary(Message *message, void *binary, int len)
{
    if (!message || !binary || len < 3) {
        log_warn("Invalid input parameters");
        return -1;
    }
    
    // 清零消息结构体
    memset(message, 0, sizeof(Message));
    
    // 解析头部字段
    memcpy(&message->connection_type, binary, 1);       // 连接类型
    memcpy(&message->id_len, binary + 1, 1);            // ID长度
    memcpy(&message->data_len, binary + 2, 1);          // 数据长度

    // 验证消息长度
    if (len != message->id_len + message->data_len + 3)
    {
        log_warn("Message is not valid: expected %d, got %d", 
                 message->id_len + message->data_len + 3, len);
        return -1;
    }

    // 分配payload内存
    message->payload = malloc(message->id_len + message->data_len);

    if (!message->payload)
    {
        log_warn("Not enough for message");
        return -1;
    }

    // 复制payload数据（ID + 数据）
    memcpy(message->payload, binary + 3, message->id_len + message->data_len);
    return 0;
}

/**
 * @brief 从JSON字符串初始化消息结构
 * 
 * 解析JSON格式的消息数据，填充消息结构体。
 * JSON格式：{"connection_type":1, "id":"AABBCC", "data":"112233"}
 * 
 * @param message 消息结构体指针
 * @param json_str JSON字符串指针
 * @param len 字符串长度
 * @return 0成功，-1失败
 */
int app_message_initByJson(Message *message, char *json_str, int len)
{
    if (!message || !json_str || len <= 0) {
        log_warn("Invalid input parameters");
        return -1;
    }
    
    // 清零消息结构体
    memset(message, 0, sizeof(Message));
    
    // 解析JSON对象
    cJSON *json_object = cJSON_ParseWithLength(json_str, len);
    if (!json_object) {
        log_warn("Failed to parse JSON");
        return -1;
    }

    // 获取连接类型字段
    cJSON *connection_type = cJSON_GetObjectItem(json_object, "connection_type");
    if (!connection_type) {
        log_warn("Missing connection_type field");
        cJSON_Delete(json_object);
        return -1;
    }
    message->connection_type = connection_type->valueint;

    // 获取ID字段（十六进制字符串）
    cJSON *id = cJSON_GetObjectItem(json_object, "id");
    if (!id || !cJSON_IsString(id)) {
        log_warn("Missing or invalid id field");
        cJSON_Delete(json_object);
        return -1;
    }

    // 验证ID字符串长度（必须是偶数）
    if (strlen(id->valuestring) % 2 != 0)
    {
        log_warn("Message id is not valid");
        cJSON_Delete(json_object);
        return -1;
    }
    message->id_len = strlen(id->valuestring) / 2;

    // 获取数据字段（十六进制字符串）
    cJSON *data = cJSON_GetObjectItem(json_object, "data");
    if (!data || !cJSON_IsString(data)) {
        log_warn("Missing or invalid data field");
        cJSON_Delete(json_object);
        return -1;
    }
    
    // 验证数据字符串长度（必须是偶数）
    if (strlen(data->valuestring) % 2 != 0)
    {
        log_warn("Message data is not valid");
        cJSON_Delete(json_object);
        return -1;
    }
    message->data_len = strlen(data->valuestring) / 2;

    // 分配payload内存
    message->payload = malloc(message->id_len + message->data_len);

    if (!message->payload)
    {
        log_warn("Not enough memory for message");
        cJSON_Delete(json_object);
        return -1;
    }

    // 将ID从十六进制字符串转为二进制
    if (str_to_bin(id->valuestring, message->payload, message->id_len) < 0)
    {
        log_warn("ID conversion failed");
        free(message->payload);
        message->payload = NULL;
        cJSON_Delete(json_object);
        return -1;
    }
    
    // 将数据从十六进制字符串转为二进制
    if (str_to_bin(data->valuestring, message->payload + message->id_len, message->data_len) < 0)
    {
        log_warn("Data conversion failed");
        free(message->payload);
        message->payload = NULL;
        cJSON_Delete(json_object);
        return -1;
    }

    cJSON_Delete(json_object);
    return 0;
}

/**
 * @brief 将消息结构保存为二进制格式
 * 
 * 将消息结构体序列化为二进制格式。
 * 
 * @param message 消息结构体指针
 * @param binary 输出二进制缓冲区
 * @param len 缓冲区大小
 * @return 实际写入长度，失败返回-1
 */
int app_message_saveBinary(Message *message, void *binary, int len)
{
    if (!message || !binary || len < 3) {
        return -1;
    }
    
    // 计算总长度
    int total_len = message->id_len + message->data_len + 3;
    if (len < total_len)
    {
        log_warn("Buffer not enough for message");
        return -1;
    }

    // 写入头部字段
    memcpy(binary, &message->connection_type, 1);       // 连接类型
    memcpy(binary + 1, &message->id_len, 1);            // ID长度
    memcpy(binary + 2, &message->data_len, 1);          // 数据长度
    
    // 写入payload数据
    if (message->payload && (message->id_len + message->data_len) > 0) {
        memcpy(binary + 3, message->payload, message->id_len + message->data_len);
    }

    return total_len;
}

/**
 * @brief 将消息结构保存为JSON格式
 * 
 * 将消息结构体序列化为JSON字符串格式。
 * 
 * @param message 消息结构体指针
 * @param json_str 输出JSON字符串缓冲区
 * @param len 缓冲区大小
 * @return 0成功，-1失败
 */
int app_message_saveJson(Message *message, char *json_str, int len)
{
    if (!message || !json_str || len <= 0) {
        return -1;
    }

    // 创建JSON对象
    cJSON *json_object = cJSON_CreateObject();
    if (!json_object) return -1;

    char *id_str = NULL;
    char *data_str = NULL;
    
    // 将ID转为十六进制字符串
    if (message->payload && message->id_len > 0) {
        id_str = bin_to_str(message->payload, message->id_len);
    }
    
    // 将数据转为十六进制字符串
    if (message->payload && message->data_len > 0) {
        data_str = bin_to_str(message->payload + message->id_len, message->data_len);
    }

    int ret = -1;
    
    // 检查转换结果
    if ((message->id_len > 0 && !id_str) || (message->data_len > 0 && !data_str)) {
        log_warn("Failed to convert binary to string");
        goto cleanup;
    }

    // 构建JSON对象
    cJSON_AddNumberToObject(json_object, "connection_type", message->connection_type);
    cJSON_AddStringToObject(json_object, "id", id_str ? id_str : "");
    cJSON_AddStringToObject(json_object, "data", data_str ? data_str : "");

    // 序列化为字符串
    char *str = cJSON_PrintUnformatted(json_object);
    if (!str) {
        log_warn("Failed to print JSON");
        goto cleanup;
    }

    int str_len = (int)strlen(str);
    
    // 检查缓冲区大小
    if (len < str_len + 1) {
        log_warn("Buffer not enough for message");
        cJSON_free(str);
        goto cleanup;
    }

    // 复制结果到输出缓冲区
    memcpy(json_str, str, str_len + 1);
    cJSON_free(str);
    ret = 0;

cleanup:
    // 释放资源
    if (id_str) free(id_str); 
    if (data_str) free(data_str);
    cJSON_Delete(json_object);
    return ret;
}

/**
 * @brief 释放消息结构体内存
 * 
 * 释放消息中动态分配的payload内存。
 * 
 * @param message 消息结构体指针
 */
void app_message_free(Message *message)
{
    if (message && message->payload)
    {
        free(message->payload);
        message->payload = NULL;
        message->id_len = 0;
        message->data_len = 0;
    }
}
