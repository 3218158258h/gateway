/**
 * @file app_dds.h
 * @brief DDS数据分发服务抽象层
 * 
 * 功能：
 * - 统一的发布/订阅接口
 * - 支持与MQTT切换
 * - QoS策略配置
 * - 自动发现节点
 * 
 * @author Gateway Team
 * @version 2.0
 */

#ifndef __APP_DDS_H__
#define __APP_DDS_H__

#include <stddef.h>
#include <stdint.h>

/* DDS QoS策略 */
typedef struct DdsQosPolicy {
    int reliability;            /* 0=best_effort, 1=reliable */
    int durability;             /* 0=volatile, 1=transient_local, 2=persistent */
    int history_depth;          /* 历史数据深度 */
    int deadline_ms;            /* 截止时间(毫秒) */
    int lifespan_ms;            /* 生命周期(毫秒) */
} DdsQosPolicy;

/* DDS配置 */
typedef struct DdsConfig {
    int domain_id;              /* 域ID */
    char participant_name[64];  /* 参与者名称 */
    DdsQosPolicy default_qos;   /* 默认QoS */
    int auto_discovery;         /* 是否自动发现 */

    char default_publish_topic[128];    /* 默认发布话题 */
    char default_publish_type[64];      /* 默认发布类型 */
    char default_subscribe_topic[128];  /* 默认订阅话题 */
    char default_subscribe_type[64];    /* 默认订阅类型 */
} DdsConfig;

/* DDS主题 */
typedef struct DdsTopic {
    char name[128];
    char type_name[64];
    DdsQosPolicy qos;
} DdsTopic;

/* DDS消息 */
typedef struct DdsMessage {
    char topic[128];
    void *data;
    size_t data_len;
    int64_t timestamp;
    uint64_t sequence_id;
} DdsMessage;

/* DDS连接状态 */
typedef enum {
    DDS_STATE_DISABLED = 0,
    DDS_STATE_INITIALIZING,
    DDS_STATE_ENABLED,
    DDS_STATE_ERROR
} DdsState;

/* DDS管理器 */
typedef struct DdsManager {
    DdsConfig config;
    DdsState state;
    void *participant;          /* DomainParticipant */
    void *publisher;
    void *subscriber;
    int is_initialized;
    void *user_data;            /* 用户数据，用于回调上下文 */
    /* 回调 */
    void (*on_data_available)(struct DdsManager *manager, const char *topic,
                              const void *data, size_t len);
    void (*on_subscriber_matched)(struct DdsManager *manager, const char *topic);
    void (*on_state_changed)(struct DdsManager *manager, DdsState new_state);
} DdsManager;

/* ========== 初始化与销毁 ========== */

/**
 * @brief 初始化DDS管理器
 * @param manager 管理器指针
 * @param config 配置
 * @return 0成功, -1失败
 */
int dds_init(DdsManager *manager, const DdsConfig *config);

/**
 * @brief 使用默认配置初始化
 * @param manager 管理器指针
 * @param domain_id 域ID
 * @return 0成功, -1失败
 */
int dds_init_default(DdsManager *manager, int domain_id);

/**
 * @brief 关闭DDS管理器
 * @param manager 管理器指针
 */
void dds_close(DdsManager *manager);

/* ========== 主题管理 ========== */

/**
 * @brief 注册主题
 * @param manager 管理器指针
 * @param topic 主题配置
 * @return 0成功, -1失败
 */
int dds_register_topic(DdsManager *manager, const DdsTopic *topic);

/**
 * @brief 注销主题
 * @param manager 管理器指针
 * @param topic_name 主题名称
 * @return 0成功, -1失败
 */
int dds_unregister_topic(DdsManager *manager, const char *topic_name);

/* ========== 发布/订阅 ========== */

/**
 * @brief 发布数据
 * @param manager 管理器指针
 * @param topic_name 主题名称
 * @param data 数据
 * @param len 数据长度
 * @return 0成功, -1失败
 */
int dds_publish(DdsManager *manager, const char *topic_name,
                const void *data, size_t len);

/**
 * @brief 订阅主题
 * @param manager 管理器指针
 * @param topic_name 主题名称
 * @return 0成功, -1失败
 */
int dds_subscribe(DdsManager *manager, const char *topic_name);

/**
 * @brief 取消订阅
 * @param manager 管理器指针
 * @param topic_name 主题名称
 * @return 0成功, -1失败
 */
int dds_unsubscribe(DdsManager *manager, const char *topic_name);

/* ========== 回调设置 ========== */

/**
 * @brief 设置数据接收回调
 */
void dds_on_data_available(DdsManager *manager,
                           void (*callback)(DdsManager *, const char *, const void *, size_t));

/**
 * @brief 设置订阅匹配回调
 */
void dds_on_subscriber_matched(DdsManager *manager,
                               void (*callback)(DdsManager *, const char *));

/**
 * @brief 设置状态变化回调
 */
void dds_on_state_changed(DdsManager *manager,
                          void (*callback)(DdsManager *, DdsState));

/* ========== 状态查询 ========== */

/**
 * @brief 获取DDS状态
 * @param manager 管理器指针
 * @return 状态
 */
DdsState dds_get_state(DdsManager *manager);

/**
 * @brief 检查DDS是否可用
 * @param manager 管理器指针
 * @return 1可用, 0不可用
 */
int dds_is_enabled(DdsManager *manager);

/* ========== 工具函数 ========== */

/**
 * @brief 创建默认QoS策略
 * @return QoS策略
 */
DdsQosPolicy dds_qos_default(void);

/**
 * @brief 创建可靠QoS策略
 * @return QoS策略
 */
DdsQosPolicy dds_qos_reliable(void);

/**
 * @brief 注册默认话题（发布和订阅）
 * @param manager 管理器指针
 * @return 0成功, -1失败
 */
int dds_register_default_topics(DdsManager *manager);

/**
 * @brief 订阅默认话题
 * @param manager 管理器指针
 * @return 0成功, -1失败
 */
int dds_subscribe_default(DdsManager *manager);

/**
 * @brief 发布到默认话题
 * @param manager 管理器指针
 * @param data 数据
 * @param len 数据长度
 * @return 0成功, -1失败
 */
int dds_publish_default(DdsManager *manager, const void *data, size_t len);
#endif /* __APP_DDS_H__ */
