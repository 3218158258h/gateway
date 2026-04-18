#if !defined(__APP_MESSAGE_H__)
#define __APP_MESSAGE_H__

typedef enum ConnectionTypeEnum
{
    CONNECTION_TYPE_NONE = 0,
    CONNECTION_TYPE_LORA = 1,
    CONNECTION_TYPE_BLE_MESH = 2,
} ConnectionType;

typedef struct MessageStruct
{
    ConnectionType connection_type; // 连接类型
    unsigned char *payload;         // 消息体
    int id_len;                     // 设备id长度
    int data_len;                   // 数据长度
} Message;

/**
 * @brief 初始化消息
 *
 * @param message 消息指针
 * @param binary 二进制数据
 * @param len 二进制数据长度
 * @return int 0 成功，-1 失败
 */
int app_message_initByBinary(Message *message, void *binary, int len);

/**
 * @brief 初始化消息
 * 
 * @param message  消息指针
 * @param json_str json字符串
 * @param len      json字符串长度
 * @return int     0 成功，-1 失败
 */
int app_message_initByJson(Message *message, char *json_str, int len);

/**
 * @brief      保存消息
 * 
 * @param message    消息指针
 * @param binary     二进制数据
 * @param len         二进制数据长度
 * @return int        成功返回实际保存长度，-1 失败
 */
int app_message_saveBinary(Message *message, void *binary, int len);

/**
 * @brief      保存消息
 * 
 * @param message    消息指针
 * @param json_str    json字符串
 * @param len         json字符串长度
 * @return int         0 成功，-1 失败
 */
int app_message_saveJson(Message *message, char *json_str, int len);

void app_message_free(Message *message);

#endif // __APP_MESSAGE_H__
