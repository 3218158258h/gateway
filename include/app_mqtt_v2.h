/**
 * @file app_mqtt_v2.h
 * @brief MQTT 客户端抽象层，支持断线重连和消息回调
 *
 * 改进内容：
 * - 断线自动重连（指数退避策略）
 * - 消息发送队列
 * - 连接状态管理
 * - 回调机制
 *
 * @author Gateway Team
 * @version 2.0
 */

#ifndef __APP_MQTT_V2_H__
#define __APP_MQTT_V2_H__

#include <stddef.h>
#include <stdint.h>

/* MQTT 连接状态。 */
typedef enum {
    MQTT_STATE_DISCONNECTED = 0,   /* 已断开 */
    MQTT_STATE_CONNECTING,          /* 连接中 */
    MQTT_STATE_CONNECTED,           /* 已连接 */
    MQTT_STATE_RECONNECTING,        /* 重连中 */
    MQTT_STATE_FAILED               /* 连接失败 */
} MqttState;

/* MQTT QoS 级别。 */
typedef enum {
    MQTT_QOS_0 = 0,
    MQTT_QOS_1 = 1,
    MQTT_QOS_2 = 2
} MqttQos;

/* MQTT 配置。 */
typedef struct MqttConfig {
    char broker_url[256];           /* MQTT服务器地址: tcp://主机:端口 */
    char client_id[128];            /* 客户端ID */
    char username[128];             /* 用户名(可选) */
    char password[128];             /* 密码(可选) */
    int keepalive_interval;         /* 心跳间隔(秒) */
    int clean_session;              /* 是否清除会话 */
    MqttQos default_qos;            /* 默认QoS */
    
    /* 重连配置 */
    int auto_reconnect;             /* 是否自动重连 */
    int reconnect_min_interval;     /* 最小重连间隔(秒) */
    int reconnect_max_interval;     /* 最大重连间隔(秒) */
    int reconnect_max_attempts;     /* 最大重连次数(0=无限) */
} MqttConfig;

/* MQTT 消息。 */
typedef struct MqttMessage {
    char topic[256];
    uint8_t *payload;
    size_t payload_len;
    MqttQos qos;
    int retained;
    uint64_t message_id;            /* 消息ID(用于持久化) */
    int64_t timestamp;              /* 时间戳 */
} MqttMessage;

/* MQTT 客户端。 */
typedef struct MqttClient {
    MqttConfig config;
    MqttState state;
    void *internal;                 /* 内部实现指针 */
    int reconnect_attempts;         /* 当前重连次数 */
    void *user_data;                /* 用户数据指针 */
    int64_t last_connect_time;      /* 上次连接时间 */
    
    /* 回调函数 */
    void (*on_connected)(struct MqttClient *client);
    void (*on_disconnected)(struct MqttClient *client);
    void (*on_message)(struct MqttClient *client, const char *topic, 
                       const void *payload, size_t len);
    void (*on_state_changed)(struct MqttClient *client, MqttState new_state);
} MqttClient;

/* ========== 初始化与销毁 ========== */

/**
 * @brief 初始化MQTT客户端
 * @param client 客户端指针
 * @param config 配置
 * @return 0成功, -1失败
 */
int mqtt_init(MqttClient *client, const MqttConfig *config);

/**
 * @brief 使用默认配置初始化
 * @param client 客户端指针
 * @param broker_url Broker地址
 * @param client_id 客户端ID
 * @return 0成功, -1失败
 */
int mqtt_init_default(MqttClient *client, const char *broker_url, const char *client_id);

/**
 * @brief 销毁MQTT客户端
 * @param client 客户端指针
 */
void mqtt_destroy(MqttClient *client);

/* ========== 连接管理 ========== */

/**
 * @brief 连接到Broker
 * @param client 客户端指针
 * @return 0成功, -1失败
 */
int mqtt_connect(MqttClient *client);

/**
 * @brief 断开连接
 * @param client 客户端指针
 */
void mqtt_disconnect(MqttClient *client);

/**
 * @brief 获取连接状态
 * @param client 客户端指针
 * @return 连接状态
 */
MqttState mqtt_get_state(MqttClient *client);

/**
 * @brief 检查是否已连接
 * @param client 客户端指针
 * @return 1已连接, 0未连接
 */
int mqtt_is_connected(MqttClient *client);

/* ========== 消息发布 ========== */

/**
 * @brief 发布消息
 * @param client 客户端指针
 * @param topic 主题
 * @param payload 消息内容
 * @param len 消息长度
 * @param qos QoS级别
 * @return 0成功, -1失败
 */
int mqtt_publish(MqttClient *client, const char *topic, 
                 const void *payload, size_t len, MqttQos qos);

/**
 * @brief 发布字符串消息
 * @param client 客户端指针
 * @param topic 主题
 * @param message 字符串消息
 * @param qos QoS级别
 * @return 0成功, -1失败
 */
int mqtt_publish_string(MqttClient *client, const char *topic, 
                        const char *message, MqttQos qos);

/* ========== 订阅管理 ========== */

/**
 * @brief 订阅主题
 * @param client 客户端指针
 * @param topic 主题
 * @param qos QoS级别
 * @return 0成功, -1失败
 */
int mqtt_subscribe(MqttClient *client, const char *topic, MqttQos qos);

/**
 * @brief 取消订阅
 * @param client 客户端指针
 * @param topic 主题
 * @return 0成功, -1失败
 */
int mqtt_unsubscribe(MqttClient *client, const char *topic);

/* ========== 回调设置 ========== */

/**
 * @brief 设置连接成功回调
 */
void mqtt_on_connected(MqttClient *client, void (*callback)(MqttClient *));

/**
 * @brief 设置断开连接回调
 */
void mqtt_on_disconnected(MqttClient *client, void (*callback)(MqttClient *));

/**
 * @brief 设置消息接收回调
 */
void mqtt_on_message(MqttClient *client, 
                     void (*callback)(MqttClient *, const char *, const void *, size_t));

/**
 * @brief 设置状态变化回调
 */
void mqtt_on_state_changed(MqttClient *client, void (*callback)(MqttClient *, MqttState));

/* ========== 主循环 ========== */

/**
 * @brief MQTT主循环(需要在单独线程中调用)
 * @param client 客户端指针
 */
void mqtt_loop(MqttClient *client);

/**
 * @brief 启动MQTT后台线程
 * @param client 客户端指针
 * @return 0成功, -1失败
 */
int mqtt_start(MqttClient *client);

/**
 * @brief 停止MQTT后台线程
 * @param client 客户端指针
 */
void mqtt_stop(MqttClient *client);

#endif /* __APP_MQTT_V2_H__ */
