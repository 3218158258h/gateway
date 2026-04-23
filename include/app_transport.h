/**
 * @file app_transport.h
 * @brief 网络协议层 - 封装 MQTT / DDS 的连接、发布、订阅与回调
 *
 * 功能：
 * - 统一的发布/订阅接口
 * - 统一的连接状态与回调转发
 * - 统一的默认主题管理
 * - 协议配置与运行时操作分离
 */

#ifndef __APP_TRANSPORT_H__
#define __APP_TRANSPORT_H__

#include <stddef.h>
#include <stdint.h>

/* 传输类型 */
typedef enum {
    TRANSPORT_TYPE_MQTT = 0,
    TRANSPORT_TYPE_DDS = 1,
    TRANSPORT_TYPE_AUTO = 2    /* 自动选择 */
} TransportType;

/* 传输状态 */
typedef enum {
    TRANSPORT_STATE_DISCONNECTED = 0,
    TRANSPORT_STATE_CONNECTING,
    TRANSPORT_STATE_CONNECTED,
    TRANSPORT_STATE_ERROR
} TransportState;

/* 传输配置 */
typedef struct TransportConfig {
    TransportType type;
    char mqtt_broker[256];
    char mqtt_client_id[128];
    int mqtt_keepalive;
    
    int dds_domain_id;
    char dds_participant_name[64];

    /* 发布话题（设备→云端，上报数据） */
    char publish_topic[128];        /* MQTT话题名 */
    char dds_publish_topic[128];    /* DDS话题名 */
    char dds_publish_type[64];      /* DDS类型名 */
    
    /* 订阅话题（云端→设备，接收指令） */
    char subscribe_topic[128];      /* MQTT话题名 */
    char dds_subscribe_topic[128];  /* DDS话题名 */
    char dds_subscribe_type[64];    /* DDS类型名 */

    int default_qos;
} TransportConfig;

/* 传输管理器 */
typedef struct TransportManager {
    TransportConfig config;
    TransportState state;
    TransportType active_type;
    void *mqtt_client;
    void *dds_manager;
    int is_initialized;
    
    /* 回调 */
    void (*on_message)(struct TransportManager *manager, const char *topic,
                       const void *data, size_t len);
    void (*on_state_changed)(struct TransportManager *manager, TransportState state);
} TransportManager;

/* ========== 初始化与销毁 ========== */

/**
 * @brief 初始化传输管理器
 * @param manager 管理器指针
 * @param config 配置
 * @return 0成功, -1失败
 */
int transport_init(TransportManager *manager, const TransportConfig *config);

/**
 * @brief 从配置文件加载协议配置
 * @param config 配置输出
 * @param config_file 配置文件路径
 * @return 0成功, -1失败
 */
int transport_load_config(TransportConfig *config, const char *config_file);

/**
 * @brief 从配置文件初始化
 * @param manager 管理器指针
 * @param config_file 配置文件路径
 * @return 0成功, -1失败
 */
int transport_init_from_config(TransportManager *manager, const char *config_file);

/**
 * @brief 关闭传输管理器
 * @param manager 管理器指针
 */
void transport_close(TransportManager *manager);

/* ========== 连接管理 ========== */

/**
 * @brief 连接
 * @param manager 管理器指针
 * @return 0成功, -1失败
 */
int transport_connect(TransportManager *manager);

/**
 * @brief 断开连接
 * @param manager 管理器指针
 */
void transport_disconnect(TransportManager *manager);

/**
 * @brief 获取状态
 * @param manager 管理器指针
 * @return 状态
 */
TransportState transport_get_state(TransportManager *manager);

/**
 * @brief 检查是否已连接
 * @param manager 管理器指针
 * @return 1已连接, 0未连接
 */
int transport_is_connected(TransportManager *manager);

/* ========== 发布/订阅 ========== */

/**
 * @brief 发布消息
 * @param manager 管理器指针
 * @param topic 主题
 * @param data 数据
 * @param len 数据长度
 * @return 0成功, -1失败
 */
int transport_publish(TransportManager *manager, const char *topic,
                      const void *data, size_t len);

/**
 * @brief 发布字符串消息
 * @param manager 管理器指针
 * @param topic 主题
 * @param message 字符串消息
 * @return 0成功, -1失败
 */
int transport_publish_string(TransportManager *manager, const char *topic,
                             const char *message);

/**
 * @brief 订阅主题
 * @param manager 管理器指针
 * @param topic 主题
 * @return 0成功, -1失败
 */
int transport_subscribe(TransportManager *manager, const char *topic);

/**
 * @brief 取消订阅
 * @param manager 管理器指针
 * @param topic 主题
 * @return 0成功, -1失败
 */
int transport_unsubscribe(TransportManager *manager, const char *topic);

/* ========== 切换传输类型 ========== */

/**
 * @brief 切换传输类型
 * @param manager 管理器指针
 * @param type 传输类型
 * @return 0成功, -1失败
 */
int transport_switch_type(TransportManager *manager, TransportType type);

/**
 * @brief 获取当前传输类型
 * @param manager 管理器指针
 * @return 传输类型
 */
TransportType transport_get_type(TransportManager *manager);

/* ========== 回调设置 ========== */

/**
 * @brief 设置消息接收回调
 */
void transport_on_message(TransportManager *manager,
                          void (*callback)(TransportManager *, const char *, const void *, size_t));

/**
 * @brief 设置状态变化回调
 */
void transport_on_state_changed(TransportManager *manager,
                                void (*callback)(TransportManager *, TransportState));

/* ========== 工具函数 ========== */

/**
 * @brief 将传输类型转换为字符串
 */
const char *transport_type_to_string(TransportType type);

/**
 * @brief 将字符串转换为传输类型
 */
TransportType transport_string_to_type(const char *str);

/**
 * @brief 订阅默认下发话题（自动适配MQTT/DDS）
 * @param manager 管理器指针
 * @return 0成功, -1失败
 */
int transport_subscribe_default(TransportManager *manager);

/**
 * @brief 发布到默认上报话题（自动适配MQTT/DDS）
 * @param manager 管理器指针
 * @param data 数据
 * @param len 数据长度
 * @return 0成功, -1失败
 */
int transport_publish_default(TransportManager *manager,
                               const void *data, size_t len);

/**
 * @brief 获取当前发布话题名
 * @param manager 管理器指针
 * @return 发布话题名称
 */
const char *transport_get_publish_topic(TransportManager *manager);

/**
 * @brief 获取当前订阅话题名
 * @param manager 管理器指针
 * @return 订阅话题名称
 */
const char *transport_get_subscribe_topic(TransportManager *manager);

#endif /* __APP_TRANSPORT_H__ */
