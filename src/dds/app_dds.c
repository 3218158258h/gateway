/**
 * @file app_dds.c
 * @brief DDS通信模块实现 - 基于Cyclone DDS
 * 
 * 功能说明：
 * - DDS（Data Distribution Service）通信封装
 * - 支持发布/订阅模式
 * - 话题注册与管理
 * - QoS策略配置
 * - 自动发现和数据回调
 */

#include "app_dds.h"
#include "../thirdparty/log.c/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dlfcn.h>

/* 编译期默认启用 Cyclone DDS；未定义时保留桩实现用于兼容测试环境。 */
#ifdef USE_CYCLONE_DDS
#include <dds/dds.h>
#define DDS_ENABLED 1
#include "GatewayData.h"  // 接口描述语言生成的数据类型
#define MAX_DATA_LEN 4096
#else
#define DDS_ENABLED 0
#endif

/**
 * @brief DDS话题信息结构体
 * 
 * 存储单个话题的配置和DDS实体句柄。
 */
typedef struct DdsTopicInfo {
    char name[128];           // 话题名称
    char type_name[64];       // 数据类型名称
    DdsQosPolicy qos;         // 服务质量策略
#ifdef USE_CYCLONE_DDS
    dds_entity_t topic;       // 话题实体
    dds_entity_t writer;      // 写入器实体
    dds_entity_t reader;      // 读取器实体
#else
    void *topic;              // 占位指针
    void *writer;
    void *reader;
#endif
    int is_registered;        // 是否已注册
} DdsTopicInfo;

/* 最大话题数量 */
#define MAX_TOPICS 32

/**
 * @brief DDS内部管理结构体
 * 
 * 存储DDS参与者和所有话题信息。
 */
typedef struct DdsInternal {
    DdsTopicInfo topics[MAX_TOPICS];  // 话题数组
    int topic_count;                   // 话题计数
#ifdef USE_CYCLONE_DDS
    dds_entity_t participant;          // 参与者实体
    dds_entity_t publisher;            // 发布者实体
    dds_entity_t subscriber;           // 订阅者实体
#endif
} DdsInternal;

/**
 * @brief 获取默认QoS策略
 * 
 * @return 默认QoS配置
 */
DdsQosPolicy dds_qos_default(void)
{
    DdsQosPolicy qos = {
        .reliability = 0,      // 尽力传输
        .durability = 0,       // 易失性
        .history_depth = 1,    // 历史深度1
        .deadline_ms = 0,      // 无截止时间
        .lifespan_ms = 0       // 无生命周期限制
    };
    return qos;
}

/**
 * @brief 获取可靠QoS策略
 * 
 * @return 可靠QoS配置
 */
DdsQosPolicy dds_qos_reliable(void)
{
    DdsQosPolicy qos = {
        .reliability = 1,      // 可靠传输
        .durability = 1,       // 持久性
        .history_depth = 10,   // 历史深度10
        .deadline_ms = 1000,   // 截止时间1秒
        .lifespan_ms = 60000   // 生命周期60秒
    };
    return qos;
}

int dds_is_compiled_enabled(void)
{
#if DDS_ENABLED
    return 1;
#else
    return 0;
#endif
}

/**
 * @brief 查找话题信息
 * 
 * 根据话题名称查找已注册的话题。
 * 
 * @param manager DDS管理器指针
 * @param name 话题名称
 * @return 话题信息指针，未找到返回NULL
 */
static DdsTopicInfo *find_topic(DdsManager *manager, const char *name)
{
    DdsInternal *internal = (DdsInternal *)manager->participant;
    if (!internal) return NULL;
    
    // 遍历话题数组查找匹配项
    for (int i = 0; i < internal->topic_count; i++) {
        if (strcmp(internal->topics[i].name, name) == 0) {
            return &internal->topics[i];
        }
    }
    return NULL;
}

/**
 * @brief 取消订阅话题
 * 
 * @param manager DDS管理器指针
 * @param topic_name 话题名称
 * @return 0成功，-1失败
 */
int dds_unsubscribe(DdsManager *manager, const char *topic_name)
{
    if (!manager || !topic_name) return -1;
    
#if DDS_ENABLED
    DdsInternal *internal = (DdsInternal *)manager->participant;
    DdsTopicInfo *info = find_topic(manager, topic_name);
    
    if (!info) {
        log_warn("Topic not registered: %s", topic_name);
        return -1;
    }
    
    // 删除读取器
    if (info->reader) {
        dds_delete(info->reader);
        info->reader = NULL;
    }
#endif
    
    log_info("DDS unsubscribed from: %s", topic_name);
    return 0;
}

/**
 * @brief 初始化DDS管理器
 * 
 * 创建DDS参与者、发布者和订阅者。
 * 
 * @param manager DDS管理器指针
 * @param config 配置参数，NULL使用默认配置
 * @return 0成功，-1失败
 */
int dds_init(DdsManager *manager, const DdsConfig *config)
{
    if (!manager) return -1;
    
    // 清零结构体
    memset(manager, 0, sizeof(DdsManager));
    
    // 加载配置
    if (config) {
        memcpy(&manager->config, config, sizeof(DdsConfig));
    } else {
        manager->config.domain_id = 0;
        snprintf(manager->config.participant_name, sizeof(manager->config.participant_name), "%s", "gateway");
        manager->config.default_qos = dds_qos_default();
        manager->config.auto_discovery = 1;
    }
    
#if DDS_ENABLED
    // 分配内部结构
    DdsInternal *internal = calloc(1, sizeof(DdsInternal));
    if (!internal) {
        log_error("Failed to allocate DDS internal structure");
        return -1;
    }
    
    // 创建DDS参与者
    internal->participant = dds_create_participant(
        manager->config.domain_id, NULL, NULL);
    
    if (internal->participant < 0) {
        log_error("Failed to create DDS participant");
        free(internal);
        return -1;
    }
    
    // 创建发布者和订阅者
    internal->publisher = dds_create_publisher(internal->participant, NULL, NULL);
    internal->subscriber = dds_create_subscriber(internal->participant, NULL, NULL);
    
    manager->participant = internal;
    manager->publisher = &internal->publisher;
    manager->subscriber = &internal->subscriber;
    
    log_info("DDS initialized: domain=%d", manager->config.domain_id);
#else
    log_warn("DDS not enabled");
    manager->participant = NULL;
#endif
    
    manager->state = DDS_STATE_ENABLED;
    manager->is_initialized = 1;
    
    return 0;
}

/**
 * @brief 使用默认配置初始化DDS管理器
 * 
 * @param manager DDS管理器指针
 * @param domain_id 域ID
 * @return 0成功，-1失败
 */
int dds_init_default(DdsManager *manager, int domain_id)
{
    DdsConfig config = {
        .domain_id = domain_id,
        .auto_discovery = 1
    };
    snprintf(config.participant_name, sizeof(config.participant_name), "%s", "gateway");
    config.default_qos = dds_qos_default();
    
    return dds_init(manager, &config);
}

/**
 * @brief 注册DDS话题
 * 
 * 创建话题实体并添加到管理器。
 * 
 * @param manager DDS管理器指针
 * @param topic 话题配置
 * @return 0成功，-1失败
 */
int dds_register_topic(DdsManager *manager, const DdsTopic *topic)
{
    if (!manager || !topic) return -1;
    
    DdsInternal *internal = (DdsInternal *)manager->participant;
    if (!internal) return -1;
    
    // 检查话题数量限制
    if (internal->topic_count >= MAX_TOPICS) {
        log_error("Max topics reached");
        return -1;
    }
    
    // 添加话题信息
    DdsTopicInfo *info = &internal->topics[internal->topic_count];
    strncpy(info->name, topic->name, sizeof(info->name) - 1);
    strncpy(info->type_name, topic->type_name, sizeof(info->type_name) - 1);
    info->qos = topic->qos;
    info->is_registered = 1;
    info->writer = 0;
    info->reader = 0;
    
#if DDS_ENABLED
    // 根据话题名称选择类型描述符
    dds_topic_descriptor_t *desc = NULL;
    if (strcmp(topic->name, "GatewayData") == 0) {
        desc = (dds_topic_descriptor_t*)&Gateway_DataType_desc;
    } else if (strcmp(topic->name, "GatewayCommand") == 0) {
        desc = (dds_topic_descriptor_t*)&Gateway_CommandType_desc;
    }
    
    // 创建DDS话题
    if (desc) {
        info->topic = dds_create_topic(internal->participant, desc, topic->name, NULL, NULL);
        if (info->topic < 0) {
            log_error("Failed to create DDS topic: %s", topic->name);
            info->topic = 0;
        } else {
            log_debug("DDS topic created: %s", topic->name);
        }
    } else {
        log_warn("Unknown topic type: %s", topic->name);
        info->topic = 0;
    }
#else
    info->topic = NULL;
#endif
    
    internal->topic_count++;
    
    log_info("DDS topic registered: %s", topic->name);
    return 0;
}

/**
 * @brief 注销DDS话题
 * 
 * 删除话题相关的所有实体。
 * 
 * @param manager DDS管理器指针
 * @param topic_name 话题名称
 * @return 0成功，-1失败
 */
int dds_unregister_topic(DdsManager *manager, const char *topic_name)
{
    if (!manager || !topic_name) return -1;
    
    DdsInternal *internal = (DdsInternal *)manager->participant;
    if (!internal) return -1;
    
    // 查找并删除话题
    for (int i = 0; i < internal->topic_count; i++) {
        if (strcmp(internal->topics[i].name, topic_name) == 0) {
#if DDS_ENABLED
            // 删除DDS实体
            if (internal->topics[i].writer) {
                dds_delete(internal->topics[i].writer);
            }
            if (internal->topics[i].reader) {
                dds_delete(internal->topics[i].reader);
            }
            if (internal->topics[i].topic) {
                dds_delete(internal->topics[i].topic);
            }
#endif
            // 移动后面的元素填补空位
            for (int j = i; j < internal->topic_count - 1; j++) {
                internal->topics[j] = internal->topics[j + 1];
            }
            internal->topic_count--;
            log_info("DDS topic unregistered: %s", topic_name);
            return 0;
        }
    }
    return -1;
}

/**
 * @brief 设置数据到达回调
 */
void dds_on_data_available(DdsManager *manager,
                           void (*callback)(DdsManager *, const char *, const void *, size_t))
{
    if (manager) {
        manager->on_data_available = callback;
    }
}

/**
 * @brief 设置订阅者匹配回调
 */
void dds_on_subscriber_matched(DdsManager *manager,
                               void (*callback)(DdsManager *, const char *))
{
    if (manager) {
        manager->on_subscriber_matched = callback;
    }
}

/**
 * @brief 设置状态变化回调
 */
void dds_on_state_changed(DdsManager *manager,
                          void (*callback)(DdsManager *, DdsState))
{
    if (manager) {
        manager->on_state_changed = callback;
    }
}

/**
 * @brief 关闭DDS管理器
 * 
 * 释放所有DDS资源。
 * 
 * @param manager DDS管理器指针
 */
void dds_close(DdsManager *manager)
{
    if (!manager || !manager->is_initialized) return;
    
#if DDS_ENABLED
    DdsInternal *internal = (DdsInternal *)manager->participant;
    if (internal) {
        // 删除参与者（会级联删除所有子实体）
        if (internal->participant > 0) {
            dds_delete(internal->participant);
        }
        free(internal);
    }
#endif
    
    manager->is_initialized = 0;
    manager->state = DDS_STATE_DISABLED;
    
    log_info("DDS closed");
}

/**
 * @brief 发布数据到指定话题
 * 
 * @param manager DDS管理器指针
 * @param topic_name 话题名称
 * @param data 数据内容
 * @param len 数据长度
 * @return 0成功，-1失败
 */
int dds_publish(DdsManager *manager, const char *topic_name,
                const void *data, size_t len)
{
    if (!manager || !topic_name || !data) return -1;
    
    // 检查初始化状态
    if (!manager->is_initialized || manager->state != DDS_STATE_ENABLED) {
        log_warn("DDS not enabled");
        return -1;
    }
    
#if DDS_ENABLED
    DdsInternal *internal = (DdsInternal *)manager->participant;
    DdsTopicInfo *info = find_topic(manager, topic_name);
    
    if (!info) {
        log_error("Topic not registered: %s", topic_name);
        return -1;
    }
    
    // 延迟创建写入器
    if (!info->writer) {
        info->writer = dds_create_writer(internal->publisher, info->topic, NULL, NULL);
        if (info->writer < 0) {
            log_error("Failed to create writer");
            return -1;
        }
    }
    
    // 封装数据到IDL结构体
    Gateway_DataType msg;
    memset(&msg, 0, sizeof(msg));
    msg.length = (len > MAX_DATA_LEN) ? MAX_DATA_LEN : (uint32_t)len; 
    memcpy(msg.data, data, msg.length);
    
    // 发送数据
    int ret = dds_write(info->writer, &msg);
    if (ret != DDS_RETCODE_OK) {
        log_error("Failed to write to topic: %s", topic_name);
        return -1;
    }
    
    log_trace("DDS published to %s: %zu bytes", topic_name, len);
    return 0;
#else
    // DDS未启用时的桩函数
    log_trace("DDS publish (stub): topic=%s, len=%zu", topic_name, len);
    return 0;
#endif
}

#if DDS_ENABLED
/**
 * @brief 根据读取器查找话题名称
 * 
 * @param manager DDS管理器指针
 * @param reader 读取器实体
 * @return 话题名称
 */
static const char* find_topic_name_by_reader(DdsManager *manager, dds_entity_t reader)
{
    DdsInternal *internal = (DdsInternal *)manager->participant;
    
    for (int i = 0; i < internal->topic_count; i++) {
        if (internal->topics[i].reader == reader) {
            return internal->topics[i].name;
        }
    }
    return "unknown";
}

/**
 * @brief 数据到达回调（DDS内部线程调用）
 * 
 * 当读取器收到数据时被DDS线程调用。
 * 
 * @param reader 读取器实体
 * @param arg 用户参数（DDS管理器指针）
 */
static void on_dds_data_available(dds_entity_t reader, void *arg)
{
    DdsManager *manager = (DdsManager *)arg;
    Gateway_DataType msg;
    void *samples[1] = { &msg };
    dds_sample_info_t infos[1];
    
    // 取出数据样本
    int n = dds_take(reader, samples, infos, 1, 1);
    if (n > 0 && infos[0].valid_data) {
        const char *topic_name = find_topic_name_by_reader(manager, reader);
        
        log_info("DDS received from %s: %u bytes", topic_name, msg.length);
        
        // 调用用户注册的回调函数
        if (manager->on_data_available) {
            manager->on_data_available(manager, topic_name, msg.data, msg.length);
        }
    }
}

typedef dds_listener_t *(*dds_create_listener_fn_t)(void *);
typedef dds_return_t (*dds_lset_data_available_fn_t)(dds_listener_t *, dds_on_data_available_fn);
typedef dds_entity_t (*dds_create_reader_fn_t)(dds_entity_t, dds_entity_t, const dds_qos_t *, const dds_listener_t *);
typedef void (*dds_delete_listener_fn_t)(dds_listener_t *);
typedef const char *(*dds_strretcode_fn_t)(dds_return_t);

static void *dds_resolve_symbol(const char *name)
{
    if (!name || name[0] == '\0') {
        return NULL;
    }
    return dlsym(RTLD_DEFAULT, name);
}

static int dds_create_reader_compat(DdsManager *manager,
                                    dds_entity_t subscriber,
                                    dds_entity_t topic,
                                    dds_entity_t *out_reader)
{
    if (!manager || !out_reader) {
        return -1;
    }

    dds_create_reader_fn_t p_create_reader =
        (dds_create_reader_fn_t)dds_resolve_symbol("dds_create_reader");
    if (!p_create_reader) {
        log_error("DDS symbol missing: dds_create_reader");
        return -1;
    }

    dds_create_listener_fn_t p_create_listener =
        (dds_create_listener_fn_t)dds_resolve_symbol("dds_create_listener");
    dds_lset_data_available_fn_t p_lset_data_available =
        (dds_lset_data_available_fn_t)dds_resolve_symbol("dds_lset_data_available");
    dds_delete_listener_fn_t p_delete_listener =
        (dds_delete_listener_fn_t)dds_resolve_symbol("dds_delete_listener");
    dds_strretcode_fn_t p_strretcode =
        (dds_strretcode_fn_t)dds_resolve_symbol("dds_strretcode");

    dds_listener_t *listener = NULL;
    if (p_create_listener && p_lset_data_available) {
        listener = p_create_listener(manager);
        if (listener) {
            p_lset_data_available(listener, on_dds_data_available);
        }
    } else {
        log_warn("DDS listener symbols missing, fallback to reader without listener callback");
    }

    dds_entity_t reader = p_create_reader(subscriber, topic, NULL, listener);
    if (reader < 0) {
        if (listener && p_delete_listener) {
            p_delete_listener(listener);
        }
        if (p_strretcode) {
            log_error("Failed to create reader: %s", p_strretcode((dds_return_t)reader));
        } else {
            log_error("Failed to create reader, ret=%d", (int)reader);
        }
        return -1;
    }

    *out_reader = reader;
    return 0;
}
#endif

/**
 * @brief 订阅话题
 * 
 * 创建读取器并设置数据到达回调。
 * 
 * @param manager DDS管理器指针
 * @param topic_name 话题名称
 * @return 0成功，-1失败
 */
int dds_subscribe(DdsManager *manager, const char *topic_name)
{
    if (!manager || !topic_name) return -1;
    
#if DDS_ENABLED
    DdsInternal *internal = (DdsInternal *)manager->participant;
    DdsTopicInfo *info = find_topic(manager, topic_name);
    
    if (!info) {
        log_error("Topic not registered: %s", topic_name);
        return -1;
    }
    
    // 延迟创建读取器
    if (!info->reader) {
        if (dds_create_reader_compat(manager, internal->subscriber, info->topic, &info->reader) != 0) {
            return -1;
        }
        log_debug("DDS reader created for: %s", topic_name);
    }
#endif
    
    log_info("DDS subscribed to: %s", topic_name);
    return 0;
}

/**
 * @brief 获取当前DDS状态
 * 
 * @param manager DDS管理器指针
 * @return 状态枚举值
 */
DdsState dds_get_state(DdsManager *manager)
{
    return manager ? manager->state : DDS_STATE_DISABLED;
}

/**
 * @brief 检查DDS是否已启用
 * 
 * @param manager DDS管理器指针
 * @return 1已启用，0未启用
 */
int dds_is_enabled(DdsManager *manager)
{
    return manager && manager->is_initialized && manager->state == DDS_STATE_ENABLED;
}

/**
 * @brief 注册默认话题（发布和订阅）
 * 
 * 根据配置注册默认的发布和订阅话题。
 * 
 * @param manager DDS管理器指针
 * @return 0成功，-1失败
 */
int dds_register_default_topics(DdsManager *manager)
{
    if (!manager || !manager->is_initialized) return -1;
    
    DdsConfig *cfg = &manager->config;
    
    // 注册发布话题
    if (cfg->default_publish_topic[0]) {
        DdsTopic topic = {0};
        strncpy(topic.name, cfg->default_publish_topic, sizeof(topic.name) - 1);
        strncpy(topic.type_name, cfg->default_publish_type[0] ? 
                cfg->default_publish_type : "GatewayData", sizeof(topic.type_name) - 1);
        topic.qos = dds_qos_reliable();  // 使用可靠QoS
        
        if (dds_register_topic(manager, &topic) != 0) {
            log_warn("Failed to register DDS publish topic: %s", topic.name);
        } else {
            log_info("DDS publish topic registered: %s (type: %s)", 
                     topic.name, topic.type_name);
        }
    }
    
    // 注册订阅话题
    if (cfg->default_subscribe_topic[0]) {
        DdsTopic topic = {0};
        strncpy(topic.name, cfg->default_subscribe_topic, sizeof(topic.name) - 1);
        strncpy(topic.type_name, cfg->default_subscribe_type[0] ? 
                cfg->default_subscribe_type : "GatewayCommand", sizeof(topic.type_name) - 1);
        topic.qos = dds_qos_reliable();  // 使用可靠QoS
        
        if (dds_register_topic(manager, &topic) != 0) {
            log_warn("Failed to register DDS subscribe topic: %s", topic.name);
        } else {
            log_info("DDS subscribe topic registered: %s (type: %s)", 
                     topic.name, topic.type_name);
        }
    }
    
    return 0;
}

/**
 * @brief 订阅默认话题
 * 
 * @param manager DDS管理器指针
 * @return 0成功，-1失败
 */
int dds_subscribe_default(DdsManager *manager)
{
    if (!manager || !manager->is_initialized) return -1;
    
    const char *topic = manager->config.default_subscribe_topic;
    if (!topic[0]) {
        log_warn("DDS subscribe topic not configured");
        return -1;
    }
    
    log_info("DDS subscribing to: %s", topic);
    return dds_subscribe(manager, topic);
}

/**
 * @brief 发布到默认话题
 * 
 * @param manager DDS管理器指针
 * @param data 数据内容
 * @param len 数据长度
 * @return 0成功，-1失败
 */
int dds_publish_default(DdsManager *manager, const void *data, size_t len)
{
    if (!manager || !data) return -1;
    
    const char *topic = manager->config.default_publish_topic;
    if (!topic[0]) {
        log_warn("DDS publish topic not configured");
        return -1;
    }
    
    return dds_publish(manager, topic, data, len);
}
