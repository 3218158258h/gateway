#if !defined(__APP_MESSAGE_H__)
#define __APP_MESSAGE_H__

/* 业务消息统一结构：上层不直接接触底层协议帧。 */
typedef enum ConnectionTypeEnum
{
    CONNECTION_TYPE_NONE = 0,
    CONNECTION_TYPE_LORA = 1,
    CONNECTION_TYPE_BLE_MESH = 2,
} ConnectionType;

typedef struct MessageStruct
{
    ConnectionType connection_type; /* 消息所属连接类型。 */
    unsigned char *payload;         /* 消息体。 */
    int id_len;                     /* 设备 ID 长度。 */
    int data_len;                   /* 业务数据长度。 */
} Message;

/**
 * @brief 从二进制数据初始化消息
 *
 * @param message 消息指针
 * @param binary 二进制数据
 * @param len 二进制数据长度
 * @return int 0 成功，-1 失败
 */
int app_message_initByBinary(Message *message, void *binary, int len);

/**
 * @brief 从 JSON 字符串初始化消息
 * 
 * @param message  消息指针
 * @param json_str json字符串
 * @param len      json字符串长度
 * @return int     0 成功，-1 失败
 */
int app_message_initByJson(Message *message, char *json_str, int len);

/**
 * @brief 保存消息为二进制
 * 
 * @param message    消息指针
 * @param binary     二进制数据
 * @param len         二进制数据长度
 * @return int        成功返回实际保存长度，-1 失败
 */
int app_message_saveBinary(Message *message, void *binary, int len);

/**
 * @brief 保存消息为 JSON
 * 
 * @param message    消息指针
 * @param json_str    json字符串
 * @param len         json字符串长度
 * @return int         0 成功，-1 失败
 */
int app_message_saveJson(Message *message, char *json_str, int len);

/* 释放消息内部资源。 */
void app_message_free(Message *message);

#endif // __APP_MESSAGE_H__
