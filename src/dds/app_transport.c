/**
 * @file app_transport.c
 * @brief 通信抽象层实现 - 统一MQTT与DDS接口
 * 
 * 功能说明：
 * - 统一封装MQTT和DDS通信接口
 * - 支持运行时协议切换
 * - 从配置文件加载通信参数
 * - 自动管理连接状态和重连
 * - 提供统一的发布/订阅接口
 */

#include "app_transport.h"
#include "app_config.h"
#include "../thirdparty/log.c/log.h"
#include "app_mqtt_v2.h"
#include "app_dds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* 传输类型字符串数组 */
static const char *type_strings[] = {
    "mqtt", "dds", "auto"
};

/**
 * @brief 传输类型枚举转字符串
 * 
 * @param type 传输类型枚举值
 * @return 类型字符串
 */
const char *transport_type_to_string(TransportType type)
{
    if (type >= TRANSPORT_TYPE_MQTT && type <= TRANSPORT_TYPE_AUTO) {
        return type_strings[type];
    }
    return "unknown";
}

/**
 * @brief 字符串转传输类型枚举
 * 
 * @param str 类型字符串
 * @return 传输类型枚举值
 */
TransportType transport_string_to_type(const char *str)
{
    if (!str) return TRANSPORT_TYPE_MQTT;
    
    // 遍历匹配类型字符串
    for (int i = 0; i <= TRANSPORT_TYPE_AUTO; i++) {
        if (strcasecmp(str, type_strings[i]) == 0) {
            return (TransportType)i;
        }
    }
    return TRANSPORT_TYPE_MQTT;
}

/**
 * @brief MQTT消息回调转发
 * 
 * 将MQTT客户端的消息回调转发到传输管理器的统一回调接口。
 * 
 * @param client MQTT客户端指针
 * @param topic 消息主题
 * @param payload 消息内容
 * @param len 消息长度
 */
static void mqtt_message_callback(MqttClient *client, const char *topic,
                                  const void *payload, size_t len)
{
    TransportManager *manager = (TransportManager *)client->user_data;
    if (manager && manager->on_message) {
        manager->on_message(manager, topic, payload, len);
    }
}

/**
 * @brief MQTT状态回调转发
 * 
 * 将MQTT客户端的状态变化转发到传输管理器的统一回调接口。
 * 
 * @param client MQTT客户端指针
 * @param state MQTT状态
 */
static void mqtt_state_callback(MqttClient *client, MqttState state)
{
    TransportManager *manager = (TransportManager *)client->user_data;
    if (!manager) return;
    
    // 映射MQTT状态到传输层状态
    TransportState tstate;
    switch (state) {
        case MQTT_STATE_CONNECTED:    tstate = TRANSPORT_STATE_CONNECTED; break;
        case MQTT_STATE_CONNECTING:   tstate = TRANSPORT_STATE_CONNECTING; break;
        case MQTT_STATE_RECONNECTING: tstate = TRANSPORT_STATE_CONNECTING; break;
        default:                      tstate = TRANSPORT_STATE_DISCONNECTED; break;
    }
    
    manager->state = tstate;
    
    // 触发上层状态变化回调
    if (manager->on_state_changed) {
        manager->on_state_changed(manager, tstate);
    }
}

/**
 * @brief DDS数据回调转发
 * 
 * 将DDS管理器的数据回调转发到传输管理器的统一回调接口。
 * 
 * @param dds DDS管理器指针
 * @param topic 话题名
 * @param data 数据内容
 * @param len 数据长度
 */
static void dds_data_callback(DdsManager *dds, const char *topic,
                              const void *data, size_t len)
{
    TransportManager *manager = (TransportManager *)dds->user_data;
    if (manager && manager->on_message) {
        manager->on_message(manager, topic, data, len);
    }
}

/**
 * @brief 初始化传输管理器
 * 
 * 根据配置初始化MQTT或DDS客户端。
 * 
 * @param manager 传输管理器指针
 * @param config 配置参数，NULL使用默认配置
 * @return 0成功，-1失败
 */
int transport_init(TransportManager *manager, const TransportConfig *config)
{
    if (!manager) return -1;
    
    // 清零结构体
    memset(manager, 0, sizeof(TransportManager));
    
    // 加载配置
    if (config) {
        memcpy(&manager->config, config, sizeof(TransportConfig));
    } else {
        // 使用默认配置
        manager->config.type = TRANSPORT_TYPE_MQTT;
        strcpy(manager->config.mqtt_broker, "tcp://localhost:1883");
        strcpy(manager->config.mqtt_client_id, "gateway");
        manager->config.mqtt_keepalive = 60;
        manager->config.dds_domain_id = 0;
        manager->config.default_qos = 1;
        
        // 默认话题配置 - MQTT
        strcpy(manager->config.publish_topic, "gateway/data");
        strcpy(manager->config.subscribe_topic, "gateway/command");
        
        // 默认话题配置 - DDS
        strcpy(manager->config.dds_publish_topic, "GatewayData");
        strcpy(manager->config.dds_publish_type, "GatewayDataType");
        strcpy(manager->config.dds_subscribe_topic, "GatewayCommand");
        strcpy(manager->config.dds_subscribe_type, "GatewayCommandType");
    }
    
    manager->state = TRANSPORT_STATE_DISCONNECTED;
    manager->active_type = manager->config.type;
    
    // 根据传输类型初始化对应客户端
    if (manager->config.type == TRANSPORT_TYPE_MQTT || 
        manager->config.type == TRANSPORT_TYPE_AUTO) {
        // 初始化MQTT客户端
        MqttConfig mqtt_cfg = {0};
        strncpy(mqtt_cfg.broker_url, manager->config.mqtt_broker, sizeof(mqtt_cfg.broker_url) - 1);
        strncpy(mqtt_cfg.client_id, manager->config.mqtt_client_id, sizeof(mqtt_cfg.client_id) - 1);
        mqtt_cfg.keepalive_interval = manager->config.mqtt_keepalive;
        mqtt_cfg.auto_reconnect = 1;
        mqtt_cfg.reconnect_min_interval = 1;
        mqtt_cfg.reconnect_max_interval = 60;
        mqtt_cfg.default_qos = manager->config.default_qos;
    
        MqttClient *mqtt = malloc(sizeof(MqttClient));
        if (mqtt && mqtt_init(mqtt, &mqtt_cfg) == 0) {
            mqtt->user_data = manager;  // 用于回调转发
            mqtt_on_message(mqtt, mqtt_message_callback);
            mqtt_on_state_changed(mqtt, mqtt_state_callback);
            manager->mqtt_client = mqtt;
            log_debug("MQTT transport initialized");
        } else {
            log_warn("Failed to initialize MQTT transport");
            if (mqtt) free(mqtt);
        }
    }
    
    // 初始化DDS客户端
    if (manager->config.type == TRANSPORT_TYPE_DDS) {
        DdsManager *dds = malloc(sizeof(DdsManager));
        if (dds) {
            DdsConfig dds_cfg = {0};
            dds_cfg.domain_id = manager->config.dds_domain_id;
            strncpy(dds_cfg.participant_name, manager->config.dds_participant_name, 
                    sizeof(dds_cfg.participant_name) - 1);
            dds_cfg.default_qos = dds_qos_default();
            dds_cfg.auto_discovery = 1;
        
            // 配置DDS话题
            strncpy(dds_cfg.default_publish_topic, manager->config.dds_publish_topic,
                sizeof(dds_cfg.default_publish_topic) - 1);
            strncpy(dds_cfg.default_publish_type, manager->config.dds_publish_type,
                sizeof(dds_cfg.default_publish_type) - 1);
            strncpy(dds_cfg.default_subscribe_topic, manager->config.dds_subscribe_topic,
                sizeof(dds_cfg.default_subscribe_topic) - 1);
            strncpy(dds_cfg.default_subscribe_type, manager->config.dds_subscribe_type,
                sizeof(dds_cfg.default_subscribe_type) - 1);
        
            if (dds_init(dds, &dds_cfg) == 0) {
                dds->user_data = manager;
                dds_on_data_available(dds, dds_data_callback);
            
                // 注册DDS默认话题
                dds_register_default_topics(dds);
            
                manager->dds_manager = dds;
                log_debug("DDS transport initialized");
            } 
            else {
                log_warn("DDS transport not available");
                free(dds);
                manager->dds_manager = NULL;
            }
        }
    }
    
    manager->is_initialized = 1;
    
    log_info("Transport initialized: type=%s", transport_type_to_string(manager->active_type));
    return 0;
}

/**
 * @brief 从配置文件初始化传输管理器
 * 
 * 读取配置文件中的传输层参数并初始化。
 * 
 * @param manager 传输管理器指针
 * @param config_file 配置文件路径
 * @return 0成功，-1失败
 */
int transport_init_from_config(TransportManager *manager, const char *config_file)
{
    if (!manager) return -1;
    
    ConfigManager config;
    if (config_init(&config, config_file) != 0) {
        log_warn("Failed to load config file, using defaults");
        return transport_init(manager, NULL);
    }
    
    config_load(&config);
    
    TransportConfig tconfig = {0};
    
    // 读取传输类型
    char type_str[32] = "mqtt";
    config_get_string(&config, "transport", "type", "mqtt", type_str, sizeof(type_str));
    tconfig.type = transport_string_to_type(type_str);
    
    // MQTT配置
    config_get_string(&config, "mqtt", "server", "tcp://localhost:1883",
                     tconfig.mqtt_broker, sizeof(tconfig.mqtt_broker));
    config_get_string(&config, "mqtt", "client_id", "gateway",
                     tconfig.mqtt_client_id, sizeof(tconfig.mqtt_client_id));
    tconfig.mqtt_keepalive = config_get_int(&config, "mqtt", "keepalive", 60);
    
    // DDS配置
    tconfig.dds_domain_id = config_get_int(&config, "dds", "domain_id", 0);
    config_get_string(&config, "dds", "participant_name", "gateway",
                     tconfig.dds_participant_name, sizeof(tconfig.dds_participant_name));
    
    // ========== 话题配置 ==========
    
    // MQTT话题
    config_get_string(&config, "mqtt", "publish_topic", "gateway/data",
                     tconfig.publish_topic, sizeof(tconfig.publish_topic));
    config_get_string(&config, "mqtt", "subscribe_topic", "gateway/command",
                     tconfig.subscribe_topic, sizeof(tconfig.subscribe_topic));
    
    // DDS话题
    config_get_string(&config, "dds", "publish_topic", "GatewayData",
                     tconfig.dds_publish_topic, sizeof(tconfig.dds_publish_topic));
    config_get_string(&config, "dds", "publish_type", "GatewayDataType",
                     tconfig.dds_publish_type, sizeof(tconfig.dds_publish_type));
    config_get_string(&config, "dds", "subscribe_topic", "GatewayCommand",
                     tconfig.dds_subscribe_topic, sizeof(tconfig.dds_subscribe_topic));
    config_get_string(&config, "dds", "subscribe_type", "GatewayCommandType",
                     tconfig.dds_subscribe_type, sizeof(tconfig.dds_subscribe_type));
    
    // QoS配置
    tconfig.default_qos = config_get_int(&config, "transport", "default_qos", 1);
    
    config_destroy(&config);
    
    return transport_init(manager, &tconfig);
}

/**
 * @brief 关闭传输管理器
 * 
 * 释放所有资源，关闭MQTT和DDS客户端。
 * 
 * @param manager 传输管理器指针
 */
void transport_close(TransportManager *manager)
{
    if (!manager) return;
    
    // 断开连接
    transport_disconnect(manager);
    
    // 释放MQTT客户端
    if (manager->mqtt_client) {
        mqtt_destroy((MqttClient *)manager->mqtt_client);
        free(manager->mqtt_client);
        manager->mqtt_client = NULL;
    }
    
    // 释放DDS管理器
    if (manager->dds_manager) {
        dds_close((DdsManager *)manager->dds_manager);
        free(manager->dds_manager);
        manager->dds_manager = NULL;
    }
    
    manager->is_initialized = 0;
    log_info("Transport closed");
}

/**
 * @brief 连接到通信服务
 * 
 * 根据当前活动类型连接MQTT Broker或启动DDS。
 * 
 * @param manager 传输管理器指针
 * @return 0成功，-1失败
 */
int transport_connect(TransportManager *manager)
{
    if (!manager || !manager->is_initialized) return -1;
    
    manager->state = TRANSPORT_STATE_CONNECTING;
    
    // 根据活动类型执行连接
    if (manager->active_type == TRANSPORT_TYPE_MQTT && manager->mqtt_client) {
        return mqtt_start((MqttClient *)manager->mqtt_client);
    } else if (manager->active_type == TRANSPORT_TYPE_DDS && manager->dds_manager) {
        // DDS不需要显式连接，直接设置为已连接状态
        manager->state = TRANSPORT_STATE_CONNECTED;
        return 0;
    }
    
    return -1;
}

/**
 * @brief 断开通信连接
 * 
 * @param manager 传输管理器指针
 */
void transport_disconnect(TransportManager *manager)
{
    if (!manager) return;
    
    // 停止MQTT客户端
    if (manager->mqtt_client) {
        mqtt_stop((MqttClient *)manager->mqtt_client);
    }
    
    manager->state = TRANSPORT_STATE_DISCONNECTED;
}

/**
 * @brief 获取当前连接状态
 * 
 * @param manager 传输管理器指针
 * @return 连接状态枚举值
 */
TransportState transport_get_state(TransportManager *manager)
{
    return manager ? manager->state : TRANSPORT_STATE_DISCONNECTED;
}

/**
 * @brief 检查是否已连接
 * 
 * @param manager 传输管理器指针
 * @return 1已连接，0未连接
 */
int transport_is_connected(TransportManager *manager)
{
    if (!manager) return 0;
    
    // 根据活动类型检查连接状态
    if (manager->active_type == TRANSPORT_TYPE_MQTT && manager->mqtt_client) {
        return mqtt_is_connected((MqttClient *)manager->mqtt_client);
    } else if (manager->active_type == TRANSPORT_TYPE_DDS && manager->dds_manager) {
        return dds_is_enabled((DdsManager *)manager->dds_manager);
    }
    
    return 0;
}

/**
 * @brief 发布消息
 * 
 * 向指定主题发布消息，自动选择当前活动传输类型。
 * 
 * @param manager 传输管理器指针
 * @param topic 主题名
 * @param data 消息数据
 * @param len 数据长度
 * @return 0成功，-1失败
 */
int transport_publish(TransportManager *manager, const char *topic,
                      const void *data, size_t len)
{
    if (!manager || !topic || !data) return -1;
    
    // 根据活动类型调用对应发布接口
    if (manager->active_type == TRANSPORT_TYPE_MQTT && manager->mqtt_client) {
        return mqtt_publish((MqttClient *)manager->mqtt_client, topic, 
                           data, len, manager->config.default_qos);
    } else if (manager->active_type == TRANSPORT_TYPE_DDS && manager->dds_manager) {
        return dds_publish((DdsManager *)manager->dds_manager, topic, data, len);
    }
    
    log_error("No active transport");
    return -1;
}

/**
 * @brief 发布字符串消息
 * 
 * @param manager 传输管理器指针
 * @param topic 主题名
 * @param message 字符串消息
 * @return 0成功，-1失败
 */
int transport_publish_string(TransportManager *manager, const char *topic,
                             const char *message)
{
    if (!message) return -1;
    return transport_publish(manager, topic, message, strlen(message));
}

/**
 * @brief 订阅主题
 * 
 * @param manager 传输管理器指针
 * @param topic 主题名
 * @return 0成功，-1失败
 */
int transport_subscribe(TransportManager *manager, const char *topic)
{
    if (!manager || !topic) return -1;
    
    // 根据活动类型调用对应订阅接口
    if (manager->active_type == TRANSPORT_TYPE_MQTT && manager->mqtt_client) {
        return mqtt_subscribe((MqttClient *)manager->mqtt_client, 
                             topic, manager->config.default_qos);
    } else if (manager->active_type == TRANSPORT_TYPE_DDS && manager->dds_manager) {
        return dds_subscribe((DdsManager *)manager->dds_manager, topic);
    }
    
    return -1;
}

/**
 * @brief 取消订阅主题
 * 
 * @param manager 传输管理器指针
 * @param topic 主题名
 * @return 0成功，-1失败
 */
int transport_unsubscribe(TransportManager *manager, const char *topic)
{
    if (!manager || !topic) return -1;
    
    // 根据活动类型调用对应取消订阅接口
    if (manager->active_type == TRANSPORT_TYPE_MQTT && manager->mqtt_client) {
        return mqtt_unsubscribe((MqttClient *)manager->mqtt_client, topic);
    } else if (manager->active_type == TRANSPORT_TYPE_DDS && manager->dds_manager) {
        return dds_unsubscribe((DdsManager *)manager->dds_manager, topic);
    }
    
    return -1;
}

/**
 * @brief 切换传输类型
 * 
 * 在MQTT和DDS之间切换，会断开当前连接并重新连接。
 * 
 * @param manager 传输管理器指针
 * @param type 目标传输类型
 * @return 0成功，-1失败
 */
int transport_switch_type(TransportManager *manager, TransportType type)
{
    if (!manager || !manager->is_initialized) return -1;
    
    // 类型相同则无需切换
    if (type == manager->active_type) {
        return 0;
    }
    
    log_info("Switching transport from %s to %s",
             transport_type_to_string(manager->active_type),
             transport_type_to_string(type));
    
    // 断开当前连接
    transport_disconnect(manager);
    
    // 切换活动类型
    manager->active_type = type;
    
    // 重新连接
    return transport_connect(manager);
}

/**
 * @brief 获取当前传输类型
 * 
 * @param manager 传输管理器指针
 * @return 当前传输类型
 */
TransportType transport_get_type(TransportManager *manager)
{
    return manager ? manager->active_type : TRANSPORT_TYPE_MQTT;
}

/**
 * @brief 设置消息回调
 * 
 * @param manager 传输管理器指针
 * @param callback 回调函数指针
 */
void transport_on_message(TransportManager *manager,
                          void (*callback)(TransportManager *, const char *, const void *, size_t))
{
    if (manager) {
        manager->on_message = callback;
    }
}

/**
 * @brief 设置状态变化回调
 * 
 * @param manager 传输管理器指针
 * @param callback 回调函数指针
 */
void transport_on_state_changed(TransportManager *manager,
                                void (*callback)(TransportManager *, TransportState))
{
    if (manager) {
        manager->on_state_changed = callback;
    }
}

/**
 * @brief 订阅默认命令主题
 * 
 * 使用配置文件中的订阅主题，用于接收云端下发命令。
 * 
 * @param manager 传输管理器指针
 * @return 0成功，-1失败
 */
int transport_subscribe_default(TransportManager *manager)
{
    if (!manager || !manager->is_initialized) return -1;
    
    if (manager->active_type == TRANSPORT_TYPE_MQTT && manager->mqtt_client) {
        // MQTT订阅命令主题
        const char *topic = manager->config.subscribe_topic;
        if (topic[0]) {
            log_info("MQTT subscribing to command topic: %s", topic);
            return mqtt_subscribe((MqttClient *)manager->mqtt_client, 
                                  topic, manager->config.default_qos);
        }
    } else if (manager->active_type == TRANSPORT_TYPE_DDS && manager->dds_manager) {
        // DDS订阅默认话题
        return dds_subscribe_default((DdsManager *)manager->dds_manager);
    }
    
    return -1;
}

/**
 * @brief 发布到默认数据主题
 * 
 * 使用配置文件中的发布主题，用于上报设备数据。
 * 
 * @param manager 传输管理器指针
 * @param data 消息数据
 * @param len 数据长度
 * @return 0成功，-1失败
 */
int transport_publish_default(TransportManager *manager,
                               const void *data, size_t len)
{
    if (!manager || !data) return -1;
    
    if (manager->active_type == TRANSPORT_TYPE_MQTT && manager->mqtt_client) {
        // MQTT发布到数据主题
        const char *topic = manager->config.publish_topic[0] ? 
                            manager->config.publish_topic : "gateway/data";

        return mqtt_publish((MqttClient *)manager->mqtt_client, topic, 
                           data, len, manager->config.default_qos);
    } else if (manager->active_type == TRANSPORT_TYPE_DDS && manager->dds_manager) {
        // DDS发布到默认话题
        return dds_publish_default((DdsManager *)manager->dds_manager, data, len);
    }
    
    return -1;
}

/**
 * @brief 获取当前发布主题名
 * 
 * @param manager 传输管理器指针
 * @return 发布主题名
 */
const char *transport_get_publish_topic(TransportManager *manager)
{
    if (!manager) return "gateway/data";
    
    // 根据活动类型返回对应主题
    if (manager->active_type == TRANSPORT_TYPE_MQTT) {
        return manager->config.publish_topic[0] ? 
               manager->config.publish_topic : "gateway/data";
    } else {
        return manager->config.dds_publish_topic[0] ? 
               manager->config.dds_publish_topic : "GatewayData";
    }
}

/**
 * @brief 获取当前订阅主题名
 * 
 * @param manager 传输管理器指针
 * @return 订阅主题名
 */
const char *transport_get_subscribe_topic(TransportManager *manager)
{
    if (!manager) return "gateway/command";
    
    // 根据活动类型返回对应主题
    if (manager->active_type == TRANSPORT_TYPE_MQTT) {
        return manager->config.subscribe_topic[0] ? 
               manager->config.subscribe_topic : "gateway/command";
    } else {
        return manager->config.dds_subscribe_topic[0] ? 
               manager->config.dds_subscribe_topic : "GatewayCommand";
    }
}
